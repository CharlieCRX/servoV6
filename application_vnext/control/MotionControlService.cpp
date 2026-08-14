// ============================================================================
// MotionControlService.cpp -- Phase 1: unique control coordinator implementation
// ============================================================================
// Per <MotionControlService -- unified control coordination layer phase doc> S5.1/5.2.
//
// Phase 1 scope:
//   - thread-safe command queue + submit() returns Queued + operationId immediately;
//   - queryOperation() returns operation entry;
//   - tick() skeleton, order guaranteed (P0):
//       drainQueue -> handleUrgent (estop/release ABSOLUTELY first, before any read) ->
//       ensureBootedIfNeeded (read topology only, NO runtime read; exponential backoff on
//       failure so a transient boot failure is retried, never permanently locked) ->
//       readFeedbackAndSafety (the SINGLE runtime+safety+connection read this tick) ->
//       updateDomainAndLock (inject domain + global lock; only unlock after boot Ok +
//       first trusted read) -> expireCommands (TTL) -> publishSnapshot.
//     Arbitration/execution/session-tick left to Phase 3.
//
// Why not sysManager_->boot(): old boot() internally poll()s (a second runtime read via
// IPlcDriver). The service instead reads topology itself (driver_.readTopology(), no
// runtime) and calls the no-I/O SystemManagerVnext::bootFromTopology(). This keeps
// "estop before synchronous read" and "one runtime snapshot per tick" intact.
//
// Thread-safety note: submit() (UI/UDP/joystick threads) mutates queue_ and operations_
// under qMtx_. tick() (single control-loop thread) touches operations_ under the same
// qMtx_. handleUrgent() does NOT hold qMtx_ during the PLC write (Modbus IO may
// block/timeout); it collects operations under lock, writes outside the lock, then
// re-locks to record the result.
// ============================================================================
#include "application_vnext/control/MotionControlService.h"

#include <cstdint>
#include <string>
#include <utility>

#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionApi.h"
#include "application_vnext/policy/GantryMotionApi.h"
#include "domain_vnext/gateway/IPlcDriver.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace application_vnext::control {

namespace {

/// command source -> short prefix (for nextOperationId, e.g. "udp-42" / "ui-3").
const char* idPrefix(ControlSource src) {
    switch (src) {
        case ControlSource::Ui:         return "ui";
        case ControlSource::Joystick:   return "joystick";
        case ControlSource::Udp:        return "udp";
        case ControlSource::Maintenance:return "maint";
    }
    return "op";
}

/// action -> operation kind (for OperationEntry.kind).
OperationKind kindOf(ControlAction a) {
    switch (a) {
        case ControlAction::StartJogForward:
        case ControlAction::StartJogBackward:
        case ControlAction::StopJog:
            return OperationKind::Jog;
        case ControlAction::GantryEnableAndCouple:
        case ControlAction::GantryDecoupleAndDisable:
            return OperationKind::GantryLifecycle;
        default:
            return OperationKind::Positioning;
    }
}

/// whether it is "estop / release / stop": not dropped by TTL, handled before read.
bool isUrgent(ControlAction a) {
    switch (a) {
        case ControlAction::EmergencyStop:
        case ControlAction::ReleaseEmergencyStop:
        case ControlAction::StopMotion:
        case ControlAction::StopJog:
            return true;
        default:
            return false;
    }
}

}  // namespace

MotionControlService::MotionControlService(
    domain_vnext::gateway::IPlcDriver& driver, IControlRuntime& runtime)
    : sysManager_(std::make_unique<application_vnext::SystemManagerVnext>(driver)),
      axisApi_(std::make_unique<application_vnext::policy::AxisMotionApi>(*sysManager_)),
      gantryApi_(std::make_unique<application_vnext::policy::GantryMotionApi>(*sysManager_)),
      driver_(driver),
      runtime_(runtime) {}

MotionControlService::~MotionControlService() = default;

std::string MotionControlService::nextOperationId(ControlSource src) {
    return std::string(idPrefix(src)) + "-" + std::to_string(++idCounter_);
}

void MotionControlService::ensureBootedIfNeeded() {
    if (bootOk_) return;
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    if (now < bootRetryDeadline_) return;  // within backoff window, skip this tick
    // Read topology only (NO runtime read); the coordinator owns the single runtime
    // read per tick in readFeedbackAndSafety().
    const auto topoRes = driver_.readTopology();
    if (topoRes.hasValue() && sysManager_ && sysManager_->bootFromTopology(*topoRes)) {
        bootOk_ = true;
        bootRetryCount_ = 0;
        return;
    }
    // Transient failure: exponential backoff (50ms,100ms,200ms,... cap 1600ms) so a
    // later tick retries; never permanently locks the service.
    ++bootRetryCount_;
    const unsigned shift = bootRetryCount_ > 5 ? 5 : bootRetryCount_;
    bootRetryDeadline_ = now + std::chrono::milliseconds(50 * (1 << shift));
}

std::string MotionControlService::submit(ControlCommand cmd) {
    // Enqueue + register atomically under qMtx_ (id generation / queue / operations_
    // are all thread-safe). Called from UI/UDP/joystick threads concurrently.
    std::lock_guard<std::mutex> lock(qMtx_);
    if (cmd.operationId.empty()) cmd.operationId = nextOperationId(cmd.source);
    const std::string opId = cmd.operationId;  // copy before moving entry below
    OperationEntry entry;
    entry.operationId = opId;
    entry.source = cmd.source;
    entry.axis = axisTargetName(cmd.target);
    entry.kind = kindOf(cmd.action);
    entry.state = OperationState::Queued;
    entry.updatedAt = std::chrono::steady_clock::now();
    queue_.push_back(std::move(cmd));
    operations_[opId] = std::move(entry);
    return opId;
}

std::optional<OperationEntry> MotionControlService::queryOperation(
    const std::string& operationId) const {
    {
        // operations_ is shared with submit(); read under the same mutex.
        std::lock_guard<std::mutex> lock(qMtx_);
        const auto it = operations_.find(operationId);
        if (it != operations_.end()) return it->second;
    }
    return store_.findOperation(operationId);
}

std::size_t MotionControlService::queuedCount() const {
    std::lock_guard<std::mutex> lock(qMtx_);
    return queue_.size();
}

void MotionControlService::drainQueue(std::vector<ControlCommand>& out) {
    std::lock_guard<std::mutex> lock(qMtx_);
    while (!queue_.empty()) {
        out.push_back(std::move(queue_.front()));
        queue_.pop_front();
    }
}

void MotionControlService::handleUrgent(const std::vector<ControlCommand>& cmds) {
    using plc_vnext::contracts::CommunicationResult;
    using clock = std::chrono::steady_clock;

    // Lock only to collect (operationId, action) to write; never do IO under the lock,
    // so a blocking/timing-out Modbus write cannot stall submit()/queryOperation().
    struct Pending { std::string operationId; ControlAction action; };
    std::vector<Pending> pending;
    {
        std::lock_guard<std::mutex> lock(qMtx_);
        for (const auto& cmd : cmds) {
            if (cmd.action == ControlAction::EmergencyStop ||
                cmd.action == ControlAction::ReleaseEmergencyStop) {
                pending.push_back({cmd.operationId, cmd.action});
            }
        }
    }

    const auto now = clock::now();
    for (const auto& p : pending) {
        // Run the PLC write outside the lock (estop write first, before any read).
        const CommunicationResult res = (p.action == ControlAction::EmergencyStop)
            ? runtime_.triggerEmergencyStop()
            : runtime_.requestEmergencyStopRelease();

        // State from the write result, never unconditional Accepted:
        //   ok() (Sent)            -> Accepted
        //   retryable (Timeout/Busy) -> CommitUncertain (written, outcome unknown)
        //   otherwise              -> Failed (keep diagnostic)
        OperationState st;
        std::string diag;
        if (res.ok()) {
            st = OperationState::Accepted;
            diag = (p.action == ControlAction::EmergencyStop)
                ? "software estop request submitted (M224)"
                : "estop release request submitted (M225)";
        } else if (res.retryable()) {
            st = OperationState::CommitUncertain;
            diag = res.diagnostic.empty()
                ? "estop write timed out / busy (commit uncertain)" : res.diagnostic;
        } else {
            st = OperationState::Failed;
            diag = res.diagnostic.empty()
                ? "estop write failed" : res.diagnostic;
        }

        // Re-lock to record the result into OperationEntry.
        std::lock_guard<std::mutex> lock(qMtx_);
        const auto it = operations_.find(p.operationId);
        if (it != operations_.end()) {
            it->second.state = st;
            it->second.diag = diag;
            it->second.updatedAt = now;
        }
    }
}

void MotionControlService::readFeedbackAndSafety() {
    // The SINGLE runtime read per tick via IControlRuntime; IPlcDriver::readRuntime()
    // lands on the same serial channel, duplicate reads forbidden (S5.2 constraint).
    const auto rt = runtime_.readRuntime();
    if (rt.hasValue()) {
        lastRuntime_ = rt.value();
    } else {
        lastRuntime_.reset();
    }

    const auto sf = runtime_.readSafety();
    if (sf.hasValue()) {
        lastSafety_ = sf.value();
    } else {
        // Read failure: mark untrusted snapshot (must not pass defaults as normal).
        lastSafety_ = plc_vnext::contracts::SafetySnapshot{};
        lastSafety_.trusted = false;
        lastSafety_.diagnostic = sf.diagnostic();
    }

    lastConn_ = runtime_.connectionState();
}

void MotionControlService::updateDomainAndLock() {
    using plc_vnext::contracts::SnapshotQuality;

    // Inject the single unique runtime snapshot into domain state (no duplicate read).
    // applyRuntimeSnapshot returns false when not booted; it participates in the lock.
    bool injected = false;
    if (sysManager_ && lastRuntime_) {
        injected = sysManager_->applyRuntimeSnapshot(*lastRuntime_);
    }
    // Inject safety feedback into the domain estop state machine.
    if (sysManager_) {
        sysManager_->applyEmergencyStopFeedback(
            lastSafety_.trusted && lastSafety_.emergencyStop);
    }

    // Only unlock when boot succeeded AND the same snapshot was injected AND
    // runtime/safety/connection are all trusted. Without boot (topology read/validation
    // failed), keep locked even if runtime/safety look trusted.
    bool locked = true;
    if (!bootOk_ || !injected) {
        locked = true;
    } else if (!lastConn_.connected) {
        locked = true;
    } else if (!lastSafety_.trusted || lastSafety_.emergencyStop) {
        locked = true;
    } else if (!lastRuntime_) {
        locked = true;
    } else if (lastRuntime_->quality != SnapshotQuality::Trusted) {
        locked = true;
    } else {
        locked = false;
    }
    globallyLocked_ = locked;
}

void MotionControlService::expireCommands(std::vector<ControlCommand>& cmds) {
    std::lock_guard<std::mutex> lock(qMtx_);  // protect operations_ (shared with submit)
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();
    for (const auto& cmd : cmds) {
        if (isUrgent(cmd.action)) continue;  // estop/stop not dropped by TTL
        if (now >= cmd.createdAt + cmd.ttl) {
            const auto it = operations_.find(cmd.operationId);
            if (it != operations_.end()) {
                it->second.state = OperationState::TimedOut;
                it->second.diag = "TTL expired before arbitration (Phase 1)";
                it->second.updatedAt = now;
            }
        }
    }
}

void MotionControlService::tick() {
    std::vector<ControlCommand> cmds;
    drainQueue(cmds);

    handleUrgent(cmds);        // estop/release ABSOLUTELY first (before any read)
    ensureBootedIfNeeded();    // read topology only (no runtime); retry with backoff
    readFeedbackAndSafety();   // the single runtime+safety+connection read this tick
    updateDomainAndLock();     // inject domain + global lock (boot-gated)
    expireCommands(cmds);      // handle expired ordinary commands

    // ---- Phase 3: arbitrate + execute + tickSessions ----
    // for (auto& c : cmds) arbitrate(c);
    // tickSessions();

    publishSnapshot();
}

void MotionControlService::publishSnapshot() {
    using plc_vnext::contracts::kRuntimeAxisCount;
    using plc_vnext::contracts::kRuntimeGroupCount;

    ControlStateSnapshot snap;
    snap.connection = lastConn_;
    snap.safety = lastSafety_;

    if (lastRuntime_) {
        const auto& rt = *lastRuntime_;
        for (std::size_t i = 0; i < kRuntimeAxisCount; ++i) {
            auto& a = snap.axes[i];
            const auto& src = rt.axes[i];
            a.slot = static_cast<int16_t>(i);
            a.trusted = src.trusted;
            a.manualSpeed = src.manualSpeed;
            a.positioningSpeed = src.positioningSpeed;
            a.absPosition = src.absPosition;
            a.relPosition = src.relPosition;
            a.motionState = src.motionState;
            a.motionLimit = src.motionLimit;
            a.alarmWord = src.alarmWord;
            // If booted, resolve slot -> (group, role, hmiVisible, bound) via topology.
            if (const auto* axis = sysManager_->system().findBySlot(
                    *plc_vnext::contracts::PlcAxisSlot::tryCreate(static_cast<int>(i)))) {
                a.group = axis->key().group;
                a.role = axis->key().function;
                a.hmiVisible = axis->hmiVisible();
                a.bound = true;
            }
        }
        for (std::size_t g = 0; g < kRuntimeGroupCount; ++g) {
            auto& gu = snap.gantries[g];
            const auto& src = rt.gantry[g];
            gu.trusted = src.trusted;
            gu.state = src.state;
            gu.internalStep = src.internalStep;
            gu.commandResult = src.commandResult;
            gu.commandErrorCode = src.commandErrorCode;
            gu.logicalControlAllowed = src.logicalControlAllowed;
            gu.memberControlAllowed = src.memberControlAllowed;
            gu.readyToCouple = src.readyToCouple;
            gu.readyToDecouple = src.readyToDecouple;
            gu.x1InGear = src.x1InGear;
            gu.x2InGear = src.x2InGear;
            gu.logicalPosition = src.logicalPosition;
            gu.skew = src.skew;
            gu.fault = src.fault;
            gu.faultCode = src.faultCode;
        }
    }

    {
        // Protect operations_ (shared with submit).
        std::lock_guard<std::mutex> lock(qMtx_);
        for (const auto& kv : operations_) snap.operations.push_back(kv.second);
    }
    store_.publish(snap);
}

}  // namespace application_vnext::control

