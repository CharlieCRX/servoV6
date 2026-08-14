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
#include <string_view>


#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionApi.h"
#include "application_vnext/policy/GantryMotionApi.h"
#include "domain_vnext/gateway/IPlcDriver.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "application_vnext/control/SessionAdapter.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/system/AxisRegistry.h"


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
        lastTopo_ = *topoRes;
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

        // Software estop request: clear all ordinary local sessions (Phase 3).
        if (p.action == ControlAction::EmergencyStop) {
            cancelAllSessions("emergency stop");
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
        case ControlAction::EnableAxis:
        case ControlAction::EnableMotor:
            return true;
        default:
            return false;
    }
}

void MotionControlService::setOpState(const std::string& id, OperationState st, std::string diag) {
    std::lock_guard<std::mutex> lock(qMtx_);  // operations_ shared with submit()
    const auto it = operations_.find(id);
    if (it == operations_.end()) return;
    it->second.state = st;
    if (!diag.empty()) it->second.diag = std::move(diag);
    it->second.updatedAt = std::chrono::steady_clock::now();
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

    // 龙门生命周期在 Phase 7 才开放安全取消；Phase 3 一律拒绝，绝不创建龙门会话。
    if (cmd.action == ControlAction::GantryEnableAndCouple ||
        cmd.action == ControlAction::GantryDecoupleAndDisable) {
        setOpState(cmd.operationId, OperationState::Rejected,
                   "gantry lifecycle deferred to Phase 7");
        return;
    }

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
    const bool logical = (fn == AxisFunction::X);   // 逻辑轴 X 走龙门 API（含组资源）

    // 一次性写入（Set*/Enable*）：直接落地，成功即 Succeeded（无会话、无租约）。
    if (isOneShotAction(cmd.action)) {
        AppVnextResult r{std::monostate{}};
        switch (cmd.action) {
            case ControlAction::EnableAxis:          r = sysManager_->enableAxis(g, fn, cmd.level); break;
            case ControlAction::EnableMotor:         r = sysManager_->enableMotor(g, fn, cmd.level); break;
            case ControlAction::SetManualSpeed:      r = sysManager_->setManualSpeed(g, fn, cmd.value); break;
            case ControlAction::SetPositioningSpeed: r = sysManager_->setPositioningSpeed(g, fn, cmd.value); break;
            case ControlAction::SetAbsTarget:        r = sysManager_->setAbsTarget(g, fn, cmd.value); break;
            case ControlAction::SetRelTarget:        r = sysManager_->setRelTarget(g, fn, cmd.value); break;
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
            const float target = cmd.motion ? cmd.motion->target : 0.0f;
            const float speed  = cmd.motion ? cmd.motion->speed : 0.0f;
            const bool abs = (cmd.action == ControlAction::StartAbsMove);
            if (logical) {
                gantryApi_->setPositioningSpeed(g, speed);
                if (abs) {
                    gantryApi_->setAbsTarget(g, target);
                    auto p = gantryApi_->beginAbs(g);
                    p.setVerifyTarget(target);
                    session = std::make_shared<
                        session_adapter::PositioningSession<application_vnext::policy::AbsMovePolicy>>(
                        *sysManager_, std::move(p), cmd.operationId, cmd.source,
                        cmd.target, required, OperationKind::Positioning);
                } else {
                    gantryApi_->setRelTarget(g, target);
                    auto p = gantryApi_->beginRel(g);
                    p.setVerifyTarget(startPos(cmd) + target);
                    session = std::make_shared<
                        session_adapter::PositioningSession<application_vnext::policy::RelMovePolicy>>(
                        *sysManager_, std::move(p), cmd.operationId, cmd.source,
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
                auto p = gantryApi_->beginJog(g, forward, 0, kHeartbeatMs);
                session = std::make_shared<session_adapter::JogSession>(
                    std::move(p), cmd.operationId, cmd.source, cmd.target,
                    required, OperationKind::Jog);
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
        case ControlAction::GantryDecoupleAndDisable:
            // Phase 3 不开放龙门：arbitrate 已拒绝，此处兜底拒绝（不创建会话）。
            setOpState(cmd.operationId, OperationState::Rejected,
                       "gantry lifecycle deferred to Phase 7");
            releaseLeaseFor(cmd.operationId);
            return;
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

