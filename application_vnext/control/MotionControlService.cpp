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
#include <algorithm>
#include <optional>
#include <sstream>
#include <string_view>


#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionApi.h"
#include "application_vnext/policy/GantryMotionApi.h"
#include "domain_vnext/gateway/IPlcDriver.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "application_vnext/control/SessionAdapter.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "infrastructure/logger/Logger.h"


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

std::string topologyAxisMap(const application_vnext::SystemManagerVnext& manager) {
    using plc_vnext::contracts::PlcAxisSlot;
    std::ostringstream oss;
    bool first = true;
    for (int slotValue = 0; slotValue < 16; ++slotValue) {
        const auto slot = PlcAxisSlot::tryCreate(slotValue);
        if (!slot) continue;
        const auto* axis = manager.system().findBySlot(*slot);
        if (!axis) continue;
        if (!first) oss << ", ";
        first = false;
        const auto group = axis->key().group.value() == 0 ? "A" : "B";
        oss << group << "."
            << domain_vnext::model::axisFunctionName(axis->key().function)
            << "(slot=" << slotValue
            << ",hmi=" << (axis->hmiVisible() ? 1 : 0)
            << ")";
    }
    return first ? "(none)" : oss.str();
}

LogContext axisTargetLogContext(const AxisTarget& target, const std::string& opId) {
    return LogContext{
        target.group.value() == 0 ? "A" : "B",
        std::string(domain_vnext::model::axisFunctionName(target.function)),
        opId.empty() ? "op" : opId
    };
}

LogContext axisNameLogContext(const std::string& axis, const std::string& opId) {
    const auto dot = axis.find('.');
    if (dot != std::string::npos && dot > 0 && dot + 1 < axis.size()) {
        return LogContext{axis.substr(0, dot), axis.substr(dot + 1),
                          opId.empty() ? "op" : opId};
    }
    return LogContext{"APP", axis.empty() ? "MotionControl" : axis,
                      opId.empty() ? "op" : opId};
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

const char* operationStateName(OperationState s) {
    switch (s) {
        case OperationState::Queued:          return "Queued";
        case OperationState::Accepted:        return "Accepted";
        case OperationState::Rejected:        return "Rejected";
        case OperationState::Running:         return "Running";
        case OperationState::Succeeded:       return "Succeeded";
        case OperationState::Failed:          return "Failed";
        case OperationState::Cancelled:       return "Cancelled";
        case OperationState::TimedOut:        return "TimedOut";
        case OperationState::CommitUncertain: return "CommitUncertain";
    }
    return "?";
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

void MotionControlService::applyGantryConfig(
    plc_vnext::contracts::PlcGroupIndex g,
    const domain_vnext::model::GantryParamModel& cfg) {
    // 龙门参数注入（只读 D1600 区，非 PLC 写）：configValid 是 GantryCouplingStateMachine::
    // requestCouple 的准入来源。Phase 7 打开龙门生命周期后，组合根/探针在触发 couple 前调用。
    if (sysManager_) sysManager_->applyGantryConfig(g, cfg);
}

std::string MotionControlService::nextOperationId(ControlSource src) {
    return std::string(idPrefix(src)) + "-" + std::to_string(++idCounter_);
}

void MotionControlService::ensureBootedIfNeeded() {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now();

    if (bootOk_) {
        if (now < topologyRefreshDeadline_) return;
        topologyRefreshDeadline_ = now + std::chrono::seconds(1);

        const auto topoRes = driver_.readTopology();
        if (!topoRes.hasValue()) return;
        if ((*topoRes).header.revision == lastTopo_.header.revision) return;

        const auto oldRevision = lastTopo_.header.revision;
        cancelAllSessions("topology revision changed");
        if (sysManager_ && sysManager_->bootFromTopology(*topoRes)) {
            lastTopo_ = *topoRes;
            LOG_INFO(LogLayer::APP, "MotionControl",
                     "topology revision changed old=" + std::to_string(oldRevision)
                     + " new=" + std::to_string(lastTopo_.header.revision)
                     + "; rebuilt axis topology axes="
                     + topologyAxisMap(*sysManager_));
        } else {
            bootOk_ = false;
            globallyLocked_ = true;
            LOG_INFO(LogLayer::APP, "MotionControl",
                     "topology revision changed old=" + std::to_string(oldRevision)
                     + " but rebuild failed; global lock enabled");
        }
        return;
    }

    if (now < bootRetryDeadline_) return;  // within backoff window, skip this tick
    // Read topology only (NO runtime read); the coordinator owns the single runtime
    // read per tick in readFeedbackAndSafety().
    const auto topoRes = driver_.readTopology();
    if (topoRes.hasValue() && sysManager_ && sysManager_->bootFromTopology(*topoRes)) {
        lastTopo_ = *topoRes;
        bootOk_ = true;
        bootRetryCount_ = 0;
        topologyRefreshDeadline_ = now + std::chrono::seconds(1);
        LOG_INFO(LogLayer::APP, "MotionControl",
                 "axis topology booted revision="
                 + std::to_string(lastTopo_.header.revision)
                 + " axes=" + topologyAxisMap(*sysManager_));
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
    const std::string sourceName = controlSourceName(cmd.source);
    const std::string actionName = controlActionName(cmd.action);
    const std::string targetName = axisTargetName(cmd.target);
    std::string opId;
    std::size_t queued = 0;
    {
        std::lock_guard<std::mutex> lock(qMtx_);
        if (cmd.operationId.empty()) cmd.operationId = nextOperationId(cmd.source);
        opId = cmd.operationId;  // copy before moving entry below
        OperationEntry entry;
        entry.operationId = opId;
        entry.source = cmd.source;
        entry.axis = targetName;
        entry.kind = kindOf(cmd.action);
        entry.state = OperationState::Queued;
        entry.updatedAt = std::chrono::steady_clock::now();
        queue_.push_back(std::move(cmd));
        queued = queue_.size();
        operations_[opId] = std::move(entry);
    }
    Logger::logWithContext(LogLevel::INFO, LogLayer::APP, "MotionControl",
                           axisTargetLogContext(cmd.target, opId),
                           "queued opId=" + opId
                           + " source=" + sourceName
                           + " action=" + actionName
                           + " target=" + targetName
                           + " queueSize=" + std::to_string(queued));
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

        // Software estop request: clear all ordinary local sessions (Phase 3).
        if (p.action == ControlAction::EmergencyStop) {
            cancelAllSessions("emergency stop");
        }
        // Drive the domain safety state machine so it can actually leave the
        // latched EmergencyStopped state. EmergencyStopped->applyFeedback() is a
        // deliberate no-op (safety latch must be explicitly released), so feedback
        // alone can NEVER return the machine to Running. Without this, after an
        // estop+release, isSystemLocked() stays true forever and every jog is
        // aborted with "safety locked, aborted" (Phase 6 regression: the old
        // UseCase path called requestEmergencyStop/requestReleaseEmergencyStop).
        if (sysManager_) {
            if (p.action == ControlAction::EmergencyStop)
                sysManager_->requestEmergencyStop();           // Running -> EmergencyStopping
            else
                sysManager_->requestReleaseEmergencyStop();    // EmergencyStopped -> ReleasingEmergencyStop
        }



        // Re-lock to record the result into OperationEntry.
        std::string axisForLog;
        bool logged = false;
        {
            std::lock_guard<std::mutex> lock(qMtx_);
            const auto it = operations_.find(p.operationId);
            if (it != operations_.end()) {
                it->second.state = st;
                it->second.diag = diag;
                it->second.updatedAt = now;
                axisForLog = it->second.axis;
                logged = true;
            }
        }
        if (logged) {
            Logger::logWithContext(LogLevel::INFO, LogLayer::APP, "MotionControl",
                                   axisNameLogContext(axisForLog, p.operationId),
                                   "opId=" + p.operationId
                                   + " axis=" + axisForLog
                                   + " state=Queued->" + operationStateName(st)
                                   + " diag=" + diag);
        }
    }

    // Stop / StopJog：高优先级终止匹配的目标会话（StopJog 按 owner 过滤，不误停他轴）。
    for (const auto& cmd : cmds) {
        if (cmd.action == ControlAction::StopMotion) {
            stopSessionsForTarget(cmd, /*ownerFiltered=*/false);
            setOpState(cmd.operationId, OperationState::Accepted, "stop requested");
        } else if (cmd.action == ControlAction::StopJog) {
            stopSessionsForTarget(cmd, /*ownerFiltered=*/true);
            setOpState(cmd.operationId, OperationState::Accepted, "stop jog requested");
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

// ============================================================================
// Phase 3：OperationEntry 回写 / 租约 / 会话终止辅助
// ============================================================================

bool MotionControlService::isOneShotAction(ControlAction a) {
    switch (a) {
        case ControlAction::SetManualSpeed:
        case ControlAction::SetPositioningSpeed:
        case ControlAction::SetAbsTarget:
        case ControlAction::SetRelTarget:
        case ControlAction::SetRelZero:
        case ControlAction::ClearRelZero:
        case ControlAction::EnableAxis:
        case ControlAction::EnableMotor:
        case ControlAction::ClearAlarmWord:
            return true;
        default:
            return false;
    }
}

void MotionControlService::setOpState(const std::string& id, OperationState st, std::string diag) {
    OperationState oldState = OperationState::Queued;
    std::string axis;
    std::string finalDiag;
    bool shouldLog = false;
    {
        std::lock_guard<std::mutex> lock(qMtx_);  // operations_ shared with submit()
        const auto it = operations_.find(id);
        if (it == operations_.end()) return;
        oldState = it->second.state;
        const std::string oldDiag = it->second.diag;
        it->second.state = st;
        if (!diag.empty()) it->second.diag = std::move(diag);
        it->second.updatedAt = std::chrono::steady_clock::now();
        axis = it->second.axis;
        finalDiag = it->second.diag;
        shouldLog = oldState != st || oldDiag != finalDiag;
    }
    if (shouldLog) {
        std::ostringstream oss;
        oss << "opId=" << id
            << " axis=" << axis
            << " state=" << operationStateName(oldState)
            << "->" << operationStateName(st);
        if (!finalDiag.empty()) oss << " diag=" << finalDiag;
        Logger::logWithContext(LogLevel::INFO, LogLayer::APP, "MotionControl",
                               axisNameLogContext(axis, id), oss.str());
    }
}

void MotionControlService::setOpMotion(const std::string& id, int16_t motionState, float position) {
    std::lock_guard<std::mutex> lock(qMtx_);
    const auto it = operations_.find(id);
    if (it == operations_.end()) return;
    it->second.motionState = motionState;
    it->second.position = position;
}

/// 任一资源已被「其它 operationId」占用 → 冲突（不抢占）。ownerOpId 用于幂等自比较。
bool MotionControlService::leaseConflict(const std::vector<ControlResource>& res,
                                         const std::string& ownerOpId) const {
    for (const auto& r : res) {
        const auto it = resourceIndex_.find(r.key);
        if (it != resourceIndex_.end() && it->second != ownerOpId) return true;
    }
    return false;
}

void MotionControlService::registerLease(const ControlCommand& cmd,
                                         const std::vector<ControlResource>& res) {
    OperationLease lease;
    lease.operationId = cmd.operationId;
    lease.owner = cmd.source;
    lease.resources = res;
    lease.target = cmd.target;
    lease.kind = kindOf(cmd.action);
    leases_.push_back(std::move(lease));
    for (const auto& r : res) resourceIndex_[r.key] = cmd.operationId;
}

void MotionControlService::releaseLeaseFor(const std::string& opId) {
    for (auto it = resourceIndex_.begin(); it != resourceIndex_.end();) {
        if (it->second == opId) it = resourceIndex_.erase(it);
        else ++it;
    }
    leases_.erase(std::remove_if(leases_.begin(), leases_.end(),
        [&](const OperationLease& l) { return l.operationId == opId; }),
        leases_.end());
}

void MotionControlService::cancelAllSessions(const char* reason) {
    for (auto& kv : sessions_) kv.second->cancel(reason);
}

void MotionControlService::stopSessionsForTarget(const ControlCommand& cmd, bool ownerFiltered) {
    const ControlResource want = ControlResource::ofAxis(cmd.target);
    for (auto& kv : sessions_) {
        auto& s = kv.second;
        // owner 过滤（StopJog）：只停本来源创建的点动会话，不误停其他轴/来源。
        if (ownerFiltered) {
            if (auto* b = dynamic_cast<const session_adapter::SessionBase*>(s.get())) {
                if (b->owner() != cmd.source) continue;
            }
        }
        const auto res = s->resources();
        const bool hit = std::any_of(res.begin(), res.end(),
            [&](const ControlResource& r) { return r.key == want.key; });
        if (hit) s->requestStop();
    }
}

// ============================================================================
// Phase 3：仲裁 / 执行 / 会话推进
// ============================================================================

void MotionControlService::mirrorToChildren(const std::string& parentId, OperationState st,
                                            const std::string& diag) {
    // 重复点动并入父会话的子 operation：镜像父会话的运行/终局状态，
    // 避免子 operation 永久停留在 Accepted（评审 P0）。
    std::lock_guard<std::mutex> lock(qMtx_);  // operations_ shared with submit()
    for (auto& kv : operations_) {
        if (kv.second.parentOperationId == parentId) {
            kv.second.state = st;
            if (!diag.empty()) kv.second.diag = diag;
            kv.second.updatedAt = std::chrono::steady_clock::now();
        }
    }
}

void MotionControlService::arbitrate(ControlCommand& cmd) {
    // 已被 handleUrgent / expireCommands 处理（非 Queued）的命令跳过。
    {
        std::lock_guard<std::mutex> lock(qMtx_);
        const auto it = operations_.find(cmd.operationId);
        if (it == operations_.end() || it->second.state != OperationState::Queued) return;
    }

    // 全局锁定（安全失败 / 断线 / 不可信 / 未 boot / Revision 变化）：
    // 普通控制一律拒绝；急停/释放/Stop 已在 handleUrgent 优先处理，不受此限制。
    if (globallyLocked_) {
        setOpState(cmd.operationId, OperationState::Rejected,
                   "globally locked (safety / disconnect / not booted)");
        return;
    }

    // 龙门生命周期（GantryEnableAndCouple / GantryDecoupleAndDisable）从 Phase 7 起开放：
    // 走下方「会话类动作」资源租约仲裁（requiredResources 返回 {gantry:A:0, X, X1, X2}，
    // 同组独占由资源集合重叠天然保证）。不再在此提前拒绝。

    // 一次性写入（Set*/Enable*）：不创建会话；但仍需尊重目标轴资源占用，防止
    // 他来源在轴运动中改速度 / EnableMotor=false（改变运行安全性）。
    if (isOneShotAction(cmd.action)) {
        const auto res = requiredResources(cmd, lastTopo_);
        if (leaseConflict(res, cmd.operationId)) {
            setOpState(cmd.operationId, OperationState::Rejected,
                       "target axis leased by another operation");
            return;
        }
        setOpState(cmd.operationId, OperationState::Accepted);
        execute(cmd);
        return;
    }

    // ---- 会话类动作：资源粒度仲裁 ----
    const auto required = requiredResources(cmd, lastTopo_);

    // 同源重复点动 ON：幂等——已存在同源同资源的点动会话则并入刷新，不重复占用。
    if (cmd.action == ControlAction::StartJogForward ||
        cmd.action == ControlAction::StartJogBackward) {
        for (const auto& kv : sessions_) {
            auto* b = dynamic_cast<const session_adapter::SessionBase*>(kv.second.get());
            if (!b || b->kind() != OperationKind::Jog || b->owner() != cmd.source) continue;
            const auto res = b->resources();
            const bool overlap = std::any_of(res.begin(), res.end(),
                [&](const ControlResource& r) {
                    return std::any_of(required.begin(), required.end(),
                        [&](const ControlResource& q) { return r.key == q.key; });
                });
            if (overlap) {
                // 幂等刷新：并入现有父会话，记录 parentOperationId 以镜像其最终状态。
                {
                    std::lock_guard<std::mutex> lock(qMtx_);
                    const auto it = operations_.find(cmd.operationId);
                    if (it != operations_.end()) it->second.parentOperationId = b->operationId();
                }
                setOpState(cmd.operationId, OperationState::Accepted,
                           "idempotent jog refresh (folded into " + b->operationId() + ")");
                return;
            }
        }
    }

    // 资源冲突：已有其它 operationId 占用任一资源 → 整体拒绝，不抢占。
    if (leaseConflict(required, cmd.operationId)) {
        setOpState(cmd.operationId, OperationState::Rejected,
                   "resource in use by another operation");
        return;
    }

    // 全部空闲：原子占用全部资源并登记索引，随后执行落地到策略。
    setOpState(cmd.operationId, OperationState::Accepted);
    registerLease(cmd, required);
    execute(cmd);
}

void MotionControlService::execute(ControlCommand& cmd) {
    using domain_vnext::model::AxisKey;
    using domain_vnext::model::AxisFunction;
    using plc_vnext::contracts::PlcAxisSlot;

    const auto g = cmd.target.group;
    const auto fn = cmd.target.function;
    // X 是龙门逻辑轴（A 组解析为 slot13）：必须先建立联动（State=3/InternalStep=80/
    // LogicalControlAllowed=ON/InGear 等）后，它才以"单轴"名义被控制。因此龙门 X 的
    // 点动/定位必须走 GantryMotionApi（PowerOwnership::LifecycleManaged +
    // GantryMotionGuard 严格准入），与 plc_vnext_motion_probe gantry-run-jog 链路一致；
    // 绝不能走单轴自管理路径（axisApi_ SelfManaged 会自行使能电机 13 且不校验逻辑许可）。
    // requiredResources() 已把 function==X 的运动命令判为龙门资源集合，这里必须保持一致。
    const bool logical = (fn == AxisFunction::X);

    Logger::logWithContext(LogLevel::INFO, LogLayer::APP, "MotionControl",
                           axisTargetLogContext(cmd.target, cmd.operationId),
                           "[execute] opId=" + cmd.operationId
                           + " action=" + controlActionName(cmd.action)
                           + " target=" + axisTargetName(cmd.target)
                           + " route=" + std::string(logical ? "gantry" : "single-axis")
                           + " group=" + std::to_string(g.value()));

    // 一次性写入（Set*/Enable*）：直接落地，成功即 Succeeded（无会话、无租约）。
    if (isOneShotAction(cmd.action)) {
        AppVnextResult r{std::monostate{}};
        switch (cmd.action) {
            case ControlAction::EnableAxis:          r = sysManager_->enableAxis(g, fn, cmd.level); break;
            case ControlAction::EnableMotor:         r = sysManager_->enableMotor(g, fn, cmd.level); break;
            case ControlAction::SetManualSpeed:      r = sysManager_->setManualSpeed(g, fn, cmd.value); break;
            case ControlAction::SetPositioningSpeed:
                // 与运动命令一致：定位速度必须为正，任何来源都不能把定位速度写成 0/负。
                if (cmd.value <= 0.0f) {
                    setOpState(cmd.operationId, OperationState::Failed,
                               "positioning speed must be positive");
                    return;
                }
                r = sysManager_->setPositioningSpeed(g, fn, cmd.value);
                break;
            case ControlAction::SetAbsTarget:
                r = sysManager_->setAbsTarget(g, fn, cmd.value);
                // 记录预填目标供摇杆/UDP/UI 触发 Start*Move 读取（§5.3）。
                presetTargets_[{g.value(), static_cast<int>(fn)}][0] = cmd.value;
                break;
            case ControlAction::SetRelTarget:
                r = sysManager_->setRelTarget(g, fn, cmd.value);
                presetTargets_[{g.value(), static_cast<int>(fn)}][1] = cmd.value;
                break;
            case ControlAction::SetRelZero:       r = sysManager_->setRelZero(g, fn); break;
            case ControlAction::ClearRelZero:     r = sysManager_->clearRelZero(g, fn); break;
            case ControlAction::ClearAlarmWord:   r = sysManager_->clearAlarmWord(g, fn); break;
            default: break;
        }
        if (appResultOk(r)) setOpState(cmd.operationId, OperationState::Succeeded);
        else setOpState(cmd.operationId, OperationState::Failed, "one-shot write failed");
        return;
    }

    // (组, 功能) -> 物理槽位；未绑定返回空（逻辑轴 X 由 GantryMotionApi 内部解析）。
    auto resolveSlot = [this](const ControlCommand& c) -> std::optional<PlcAxisSlot> {
        const auto* axis = sysManager_->system().find(
            AxisKey{c.target.group, c.target.function});
        if (!axis) return std::nullopt;
        return axis->slot();
    };
    auto startPos = [this](const ControlCommand& c) -> float {
        const auto* axis = sysManager_->system().find(
            AxisKey{c.target.group, c.target.function});
        return axis ? axis->feedback().absPosition : 0.0f;
    };
    auto failAndRelease = [&](const char* why) {
        setOpState(cmd.operationId, OperationState::Failed, why);
        releaseLeaseFor(cmd.operationId);
    };

    std::shared_ptr<ISessionPolicy> session;
    const auto required = requiredResources(cmd, lastTopo_);
    switch (cmd.action) {
        case ControlAction::StartAbsMove:
        case ControlAction::StartRelMove: {
            // 权威校验（§5.3 + Phase 4 P0）：定位必须携带正速度。绝不允许缺失或 0，
            // 否则会以 0 覆盖 PLC 定位速度造成不运动/无效参数。失败即 Failed 且释放租约，
            // 不进入会话、不占资源 —— 任何来源（UI/摇杆/UDP）都无法写 0 速度。
            if (!cmd.motion || cmd.motion->speed <= 0.0f) {
                failAndRelease("positioning speed must be positive");
                return;
            }
            // 权威拦截：目标轴已到达任意限位（motionLimit != 0）时禁止定位（位置移动）。
            // 限位后只能通过点动撤离方向脱困；此拦截对 UI/摇杆/UDP 一律生效，
            // 避免任何入口在限位状态下发起定位。
            const auto* limAxis = sysManager_->system().find(
                AxisKey{cmd.target.group, cmd.target.function});
            if (limAxis && limAxis->feedback().motionLimit != 0) {
                failAndRelease("axis at limit: jog away before positioning");
                return;
            }
            const float target = cmd.motion->target;
            const float speed  = cmd.motion->speed;
            const bool abs = (cmd.action == ControlAction::StartAbsMove);
            if (logical) {
                // 龙门逻辑轴位置移动：与点动对称的组合闭环（复刻 gantry-run-jog 语义），
                // 顺序驱动「建立联动+使能(couple -> Ready) → 定位(move) → 解除+掉电(decouple -> Done)」。
                // 修复原实现缺陷：只调用 beginAbs/beginRel（LifecycleManaged 跳过使能 + 依赖
                // guard 已联动），未联动/未使能时位置移动直接失败（"gantry not coupled"/未使能）。
                // 本闭环在 Moving 前由 GantryLifecyclePolicy 完成使能轴控+电机、Couple -> Ready，
                // 运动完成后 Decouple + 掉电；已联动时 Coupling 段幂等跳过（与 GantryAutoJogSession 一致）。
                gantryApi_->setPositioningSpeed(g, speed);
                auto couple   = gantryApi_->beginEnableAndCouple(g);
                auto decouple = gantryApi_->beginDecoupleAndDisable(g);
                if (abs) {
                    gantryApi_->setAbsTarget(g, target);
                    auto p = gantryApi_->beginAbs(g);
                    p.setVerifyTarget(target);
                    session = std::make_shared<session_adapter::GantryAutoMoveSession<
                        application_vnext::policy::AbsMovePolicy>>(
                        *sysManager_, g, std::move(couple), std::move(p), std::move(decouple),
                        cmd.operationId, cmd.source,
                        cmd.target, required, OperationKind::Positioning);
                } else {
                    gantryApi_->setRelTarget(g, target);
                    auto p = gantryApi_->beginRel(g);
                    p.setVerifyTarget(startPos(cmd) + target);
                    session = std::make_shared<session_adapter::GantryAutoMoveSession<
                        application_vnext::policy::RelMovePolicy>>(
                        *sysManager_, g, std::move(couple), std::move(p), std::move(decouple),
                        cmd.operationId, cmd.source,
                        cmd.target, required, OperationKind::Positioning);
                }
            } else {
                const auto slot = resolveSlot(cmd);
                if (!slot) { failAndRelease("axis not bound in topology"); return; }
                sysManager_->setPositioningSpeed(g, fn, speed);
                if (abs) {
                    axisApi_->setAbsTarget(*slot, target);
                    auto p = axisApi_->beginAbs(*slot);
                    p.setVerifyTarget(target);
                    session = std::make_shared<
                        session_adapter::PositioningSession<application_vnext::policy::AbsMovePolicy>>(
                        *sysManager_, std::move(p), cmd.operationId, cmd.source,
                        cmd.target, required, OperationKind::Positioning);
                } else {
                    axisApi_->setRelTarget(*slot, target);
                    auto p = axisApi_->beginRel(*slot);
                    p.setVerifyTarget(startPos(cmd) + target);
                    session = std::make_shared<
                        session_adapter::PositioningSession<application_vnext::policy::RelMovePolicy>>(
                        *sysManager_, std::move(p), cmd.operationId, cmd.source,
                        cmd.target, required, OperationKind::Positioning);
                }
            }
            break;
        }
        case ControlAction::StartJogForward:
        case ControlAction::StartJogBackward: {
            const bool forward = (cmd.action == ControlAction::StartJogForward);
            constexpr int kHeartbeatMs = 500;
            if (logical) {
                // 复刻 gantry-run-jog 组合闭环：建立联动+使能 -> 点动 -> 解除+掉电，逐段顺序驱动。
                auto couple    = gantryApi_->beginEnableAndCouple(g);
                auto jog       = gantryApi_->beginJog(g, forward, 0, kHeartbeatMs);
                auto decouple  = gantryApi_->beginDecoupleAndDisable(g);
                session = std::make_shared<session_adapter::GantryAutoJogSession>(
                    *sysManager_, g, std::move(couple), std::move(jog), std::move(decouple),
                    cmd.operationId, cmd.source, cmd.target, required, OperationKind::Jog);
            } else {
                const auto slot = resolveSlot(cmd);
                if (!slot) { failAndRelease("axis not bound in topology"); return; }
                auto p = axisApi_->beginJog(*slot, forward, 0, kHeartbeatMs);
                session = std::make_shared<session_adapter::JogSession>(
                    std::move(p), cmd.operationId, cmd.source, cmd.target,
                    required, OperationKind::Jog);
            }
            break;
        }
        case ControlAction::GantryEnableAndCouple:
        case ControlAction::GantryDecoupleAndDisable: {
            // Phase 7：走 GantryMotionApi 生命周期（拥有电源），包成龙门生命周期会话。
            // 建立 = beginEnableAndCouple（使能虚轴→等 ms=2→Couple→Ready）；
            // 解除 = beginDecoupleAndDisable（确保停止→Decouple→掉电→Done）。
            const bool couple = (cmd.action == ControlAction::GantryEnableAndCouple);
            auto p = couple ? gantryApi_->beginEnableAndCouple(g)
                            : gantryApi_->beginDecoupleAndDisable(g);
            if (p.hasError()) {
                // 逻辑轴未绑定等：创建即 Error，立即失败并释放租约（failAndRelease 语义）。
                setOpState(cmd.operationId, OperationState::Failed, p.diag());
                releaseLeaseFor(cmd.operationId);
                return;
            }
            session = std::make_shared<session_adapter::GantryLifecycleSession>(
                std::move(p), cmd.operationId, cmd.source, cmd.target,
                required, OperationKind::GantryLifecycle);
            break;
        }
        default:
            // 不应到达：紧急 / Stop / 一次性动作已在上游处理。
            setOpState(cmd.operationId, OperationState::Rejected, "unsupported action");
            releaseLeaseFor(cmd.operationId);
            return;
    }

    if (!session) { failAndRelease("session create failed"); return; }

    // 创建即 Error（如逻辑轴未绑定 / 槽位未注册）：立即失败并释放租约，不留空会话。
    if (session->hasError()) {
        setOpState(cmd.operationId, OperationState::Failed, session->diag());
        releaseLeaseFor(cmd.operationId);
        return;
    }
    sessions_[cmd.operationId] = std::move(session);
}

void MotionControlService::tickSessions() {
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        const std::string opId = it->first;
        auto& s = it->second;

        // 全局锁定（安全失败 / 断线 / 不可信 / Revision 变化）：终止本地会话，
        // 绝不在重连后自动重放运动。
        if (globallyLocked_) s->cancel("global lock / safety / disconnect");

        s->tick();

        // 回写 PLC 反馈（motionState / position）到 OperationEntry。
        if (auto* b = dynamic_cast<session_adapter::SessionBase*>(s.get())) {
            const auto t = b->target();
            if (const auto* axis = sysManager_->system().find(
                    domain_vnext::model::AxisKey{t.group, t.function})) {
                setOpMotion(opId, axis->feedback().motionState, axis->feedback().absPosition);
            }
        }

        if (s->isDone() || s->hasError()) {
            OperationState final;
            std::string diag = s->diag();
            if (s->hasError()) {
                final = OperationState::Failed;
            } else if (s->isStopping()) {
                final = OperationState::Cancelled;
                if (diag.empty()) diag = "cancelled / stopped";
            } else {
                final = OperationState::Succeeded;
            }
            setOpState(opId, final, diag);
            mirrorToChildren(opId, final, diag);
            releaseLeaseFor(opId);
            it = sessions_.erase(it);
        } else {
            setOpState(opId, OperationState::Running);
            mirrorToChildren(opId, OperationState::Running, {});
            ++it;
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
    for (auto& c : cmds) arbitrate(c);   // 仲裁通过即执行；占用 / 拒绝 / 幂等刷新
    tickSessions();                      // 推进会话、更新 OperationEntry、终止即释放租约

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
            a.relZeroRecord = rt.params[i].relZeroRecord;
            a.relZeroTrusted = rt.params[i].trusted;
            a.motionState = src.motionState;
            a.motionLimit = src.motionLimit;
            a.alarmWord = src.alarmWord;
            // 软限位（参数区，与运行反馈同帧读取）。
            a.softNegLimit = rt.params[i].softNegLimit;
            a.softPosLimit = rt.params[i].softPosLimit;
            a.softLimitControl = rt.params[i].softLimitControl;
            a.softLimitTrusted = rt.params[i].trusted;
            // If booted, resolve slot -> (group, role, hmiVisible, bound) via topology.
            if (const auto* axis = sysManager_->system().findBySlot(
                    *plc_vnext::contracts::PlcAxisSlot::tryCreate(static_cast<int>(i)))) {
                a.group = axis->key().group;
                a.role = axis->key().function;
                a.hmiVisible = axis->hmiVisible();
                a.bound = true;
                // 投影资源租约：该轴是否被某操作占用（统一协调层核心价值：
                // UI 应能显示“当前由 UDP / 摇杆控制”）。
                const std::string key = ControlResource::ofAxis(
                    AxisTarget{axis->key().group, axis->key().function}).key;
                const auto idx = resourceIndex_.find(key);
                if (idx != resourceIndex_.end()) {
                    a.leased = true;
                    a.leaseOperationId = idx->second;
                    for (const auto& l : leases_) {
                        if (l.operationId == idx->second) {
                            a.leaseOwner = l.owner;
                            a.leaseOwnerName = controlSourceName(l.owner);
                            break;
                        }
                    }
                }
                // 投影定位目标预填值（SetAbsTarget/SetRelTarget 缓存，§5.3）。
                const auto pt = presetTargets_.find(
                    {axis->key().group.value(), static_cast<int>(axis->key().function)});
                if (pt != presetTargets_.end()) {
                    a.absMoveTarget = pt->second[0];
                    a.relMoveTarget = pt->second[1];
                }
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
            // 投影龙门组资源租约（Phase 3 龙门未开放，恒为 false；Phase 7 启用）。
            const auto gidx = resourceIndex_.find(ControlResource::ofGantry(
                plc_vnext::contracts::PlcGroupIndex(static_cast<int>(g))).key);
            if (gidx != resourceIndex_.end()) {
                gu.lifecycleLeased = true;
                gu.lifecycleOperationId = gidx->second;
            }
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

