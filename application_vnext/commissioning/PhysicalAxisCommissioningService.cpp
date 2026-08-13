// ============================================================================
// PhysicalAxisCommissioningService.cpp —— 阶段3：单轴物理调试服务实现
// ============================================================================
#include "application_vnext/commissioning/PhysicalAxisCommissioningService.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <thread>

#include "application_vnext/commissioning/JogSession.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"

namespace application_vnext::commissioning {
namespace {

// 已冻结拓扑 ABI 事实常量（与 fake/PlcFixtureBuilder.h 及验证脚本一致）。
constexpr int32_t kTopologyMagic = static_cast<int32_t>(0x013527C6);
constexpr int16_t kSchemaVersion = 1;

// 运动/参数读回容差（EU）。用于“位移发生”与“读回一致”判定。
constexpr float kPositionTolerance = 0.01f;

// 定位等待与轮询参数。
constexpr int kDefaultMoveTimeoutMs = 5000;
constexpr int kDefaultPollMs = 50;

// D128 运动状态：0 未使能 / 1 空闲 / 2 正向点动 / 3 反向点动 / 4 绝对定位 / 5 相对定位。
constexpr int16_t kMotionStateIdle = 1;

/// 按命令类别构造对应参数写命令（仅支持四类：手动速度/定位速度/绝对目标/相对目标）。
plc_vnext::contracts::PlcAxisCommand parameterCommand(
    plc_vnext::contracts::PlcAxisCommandKind kind, float v) {
    using plc_vnext::contracts::PlcAxisCommand;
    switch (kind) {
        case plc_vnext::contracts::PlcAxisCommandKind::SetManualSpeed:
            return PlcAxisCommand::makeSetManualSpeed(v);
        case plc_vnext::contracts::PlcAxisCommandKind::SetPositioningSpeed:
            return PlcAxisCommand::makeSetPositioningSpeed(v);
        case plc_vnext::contracts::PlcAxisCommandKind::SetAbsTarget:
            return PlcAxisCommand::makeSetAbsTarget(v);
        case plc_vnext::contracts::PlcAxisCommandKind::SetRelTarget:
            return PlcAxisCommand::makeSetRelTarget(v);
        default:
            return PlcAxisCommand::makeSetManualSpeed(v);
    }
}

}  // namespace

PhysicalAxisCommissioningService::PhysicalAxisCommissioningService(
    plc_vnext::IPlcRuntimeGateway& gateway)
    : gateway_(gateway) {}

// ============================================================================
// 写入闸门（Step 3.0）
// ============================================================================
PhysicalAxisCommissioningService::GateResult
PhysicalAxisCommissioningService::evaluateGate(
    plc_vnext::contracts::PlcAxisSlot slot, CommissioningOperation op) const {
    // 0. slot 白名单：本阶段仅允许 slot 0 / slot 1。
    if (!slotAllowed(slot)) return {CommissioningGateReason::SlotNotAllowed};
    // 1. PLC 已连接。
    if (!gateway_.connectionState().connected) return {CommissioningGateReason::Disconnected};
    // 2. Topology 读取成功，Magic/SchemaVersion 正确，ConfigValid=true。
    auto topo = gateway_.readTopology();
    if (!topo.hasValue()) return {CommissioningGateReason::TopologyInvalid};
    const auto& h = topo.value().header;
    if (h.magic != kTopologyMagic || h.schemaVersion != kSchemaVersion ||
        !h.configValid) {
        return {CommissioningGateReason::TopologyInvalid};
    }
    // 3. Revision 未变化。
    if (lastRevision_.has_value() && h.revision != *lastRevision_) {
        return {CommissioningGateReason::RevisionChanged};
    }
    lastRevision_ = h.revision;
    // 4. Runtime trusted，且目标轴无报警。
    auto runtime = gateway_.readRuntime();
    if (!runtime.hasValue()) return {CommissioningGateReason::RuntimeUntrusted};
    const auto& ax = runtime.value().axes[static_cast<std::size_t>(slot.value())];
    if (!ax.trusted) return {CommissioningGateReason::RuntimeUntrusted};
    if (ax.alarmWord != 0) return {CommissioningGateReason::AxisAlarm};
    // 5. Safety trusted，M224=false。
    auto safety = gateway_.readSafety();
    if (!safety.hasValue()) return {CommissioningGateReason::SafetyUnknown};
    if (safety.value().emergencyStop) return {CommissioningGateReason::EmergencyStop};
    // 6. 无其它点动会话。
    if (jogActive_.load()) return {CommissioningGateReason::Busy};

    (void)op;  // 普通操作统一按上述闸门评估；ReleaseEmergencyStop 不进入本评估。
    return {CommissioningGateReason::Ok};
}

plc_vnext::contracts::CommunicationResult
PhysicalAxisCommissioningService::write(
    plc_vnext::contracts::PlcAxisSlot slot,
    const plc_vnext::contracts::PlcAxisCommand& cmd, CommissioningOperation op,
    bool confirmWrite, CommissioningGateReason& reason, std::string& diag) const {
    const auto gate = evaluateGate(slot, op);
    if (!gate.allow()) {
        reason = gate.reason;
        diag = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return plc_vnext::contracts::CommunicationResult::disconnected(diag);
    }
    if (!confirmWrite) {
        reason = CommissioningGateReason::Ok;
        diag = "confirm-write not granted";
        return plc_vnext::contracts::CommunicationResult::disconnected(diag);
    }
    // 直接写物理 slot（不经 SystemManagerVnext 的 X1/X2 龙门逻辑轴路由）。
    reason = CommissioningGateReason::Ok;
    return gateway_.writeAxis(slot, cmd);
}

// ============================================================================
// 只读辅助
// ============================================================================
bool PhysicalAxisCommissioningService::readAxisRuntime(
    plc_vnext::contracts::PlcAxisSlot slot,
    plc_vnext::contracts::AxisRuntimeSnapshot& out) const {
    auto rt = gateway_.readRuntime();
    if (!rt.hasValue()) return false;
    const auto& ax = rt.value().axes[static_cast<std::size_t>(slot.value())];
    if (!ax.trusted) return false;
    out = ax;
    return true;
}

bool PhysicalAxisCommissioningService::readAxisParameter(
    plc_vnext::contracts::PlcAxisSlot slot,
    plc_vnext::contracts::AxisParameterSnapshot& out) const {
    auto p = gateway_.readAxisParameters(slot);
    if (!p.hasValue()) return false;
    out = p.value();
    return out.trusted;
}

bool PhysicalAxisCommissioningService::currentAbsPosition(
    plc_vnext::contracts::PlcAxisSlot slot, float& pos) const {
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (!readAxisRuntime(slot, ax)) return false;
    pos = ax.absPosition;
    return true;
}

bool PhysicalAxisCommissioningService::readParameterValue(
    plc_vnext::contracts::PlcAxisSlot slot,
    plc_vnext::contracts::PlcAxisCommandKind kind, float& value) const {
    using plc_vnext::contracts::PlcAxisCommandKind;
    switch (kind) {
        case PlcAxisCommandKind::SetManualSpeed: {
            plc_vnext::contracts::AxisRuntimeSnapshot ax;
            if (!readAxisRuntime(slot, ax)) return false;
            value = ax.manualSpeed;
            return true;
        }
        case PlcAxisCommandKind::SetPositioningSpeed: {
            plc_vnext::contracts::AxisRuntimeSnapshot ax;
            if (!readAxisRuntime(slot, ax)) return false;
            value = ax.positioningSpeed;
            return true;
        }
        case PlcAxisCommandKind::SetAbsTarget: {
            plc_vnext::contracts::AxisParameterSnapshot p;
            if (!readAxisParameter(slot, p)) return false;
            value = p.absMoveDistance;
            return true;
        }
        case PlcAxisCommandKind::SetRelTarget: {
            plc_vnext::contracts::AxisParameterSnapshot p;
            if (!readAxisParameter(slot, p)) return false;
            value = p.relMoveDistance;
            return true;
        }
        default:
            return false;
    }
}

bool PhysicalAxisCommissioningService::waitUntilIdle(
    plc_vnext::contracts::PlcAxisSlot slot, int timeoutMs, int pollMs) const {
    using clock = std::chrono::steady_clock;
    const auto deadline = clock::now() + std::chrono::milliseconds(timeoutMs);
    while (clock::now() < deadline) {
        plc_vnext::contracts::AxisRuntimeSnapshot ax;
        if (readAxisRuntime(slot, ax) && ax.motionState == kMotionStateIdle) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    return false;
}

// ============================================================================
// 参数写入 / 恢复（Step 3.2）
// ============================================================================
PhysicalAxisCommissioningService::ParameterVerifyOutcome
PhysicalAxisCommissioningService::verifyParameter(
    plc_vnext::contracts::PlcAxisSlot slot,
    plc_vnext::contracts::PlcAxisCommandKind kind, float testValue,
    bool confirmWrite) const {
    ParameterVerifyOutcome out;
    if (!confirmWrite) {
        out.diagnostic = "confirm-write required";
        return out;
    }
    const auto gate = evaluateGate(slot, CommissioningOperation::ParameterWrite);
    if (!gate.allow()) {
        out.gateReason = gate.reason;
        out.diagnostic = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return out;
    }
    // 1. 读取原值并保存。
    if (!readParameterValue(slot, kind, out.originalValue)) {
        out.diagnostic = "read original value failed";
        return out;
    }
    out.writtenValue = testValue;
    out.restoredValue = out.originalValue;

    // 2. 写入低风险测试值。
    auto r = gateway_.writeAxis(slot, parameterCommand(kind, testValue));
    if (!r.ok()) {
        out.diagnostic = "write test value failed: " + r.diagnostic;
        return out;
    }
    // 3. 重新读取并确认。
    if (!readParameterValue(slot, kind, out.readBackWritten)) {
        out.diagnostic = "read-back after write failed";
        return out;
    }
    out.writtenConfirmed =
        std::fabs(out.readBackWritten - testValue) <= kPositionTolerance;

    // 4. 恢复原值。
    r = gateway_.writeAxis(slot, parameterCommand(kind, out.originalValue));
    if (!r.ok()) {
        out.diagnostic = "restore value failed: " + r.diagnostic;
        return out;
    }
    // 5. 再次读取确认恢复。
    if (!readParameterValue(slot, kind, out.readBackRestored)) {
        out.diagnostic = "read-back after restore failed";
        return out;
    }
    out.restoredConfirmed =
        std::fabs(out.readBackRestored - out.originalValue) <= kPositionTolerance;

    out.ok = out.writtenConfirmed && out.restoredConfirmed;
    if (!out.ok) out.diagnostic = "parameter read-back mismatch";
    return out;
}

// ============================================================================
// 使能（Step 3.3）
// ============================================================================
PhysicalAxisCommissioningService::EnableOutcome
PhysicalAxisCommissioningService::enableAxis(
    plc_vnext::contracts::PlcAxisSlot slot, bool on, bool confirmWrite) const {
    EnableOutcome out;
    CommissioningGateReason reason = CommissioningGateReason::Ok;
    std::string diag;
    const auto r = write(slot, plc_vnext::contracts::PlcAxisCommand::makeEnableAxis(on),
                         CommissioningOperation::Enable, confirmWrite, reason, diag);
    out.gateReason = reason;
    if (!r.ok()) {
        out.diagnostic = diag.empty() ? r.diagnostic : diag;
        return out;
    }
    // 读回确认（motionState 反映轴控使能后的状态）。
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.motionStateAfter = ax.motionState;
    out.ok = true;
    return out;
}

PhysicalAxisCommissioningService::EnableOutcome
PhysicalAxisCommissioningService::enableMotor(
    plc_vnext::contracts::PlcAxisSlot slot, bool on, bool confirmWrite) const {
    EnableOutcome out;
    CommissioningGateReason reason = CommissioningGateReason::Ok;
    std::string diag;
    const auto r = write(slot, plc_vnext::contracts::PlcAxisCommand::makeEnableMotor(on),
                         CommissioningOperation::Enable, confirmWrite, reason, diag);
    out.gateReason = reason;
    if (!r.ok()) {
        out.diagnostic = diag.empty() ? r.diagnostic : diag;
        return out;
    }
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.motionStateAfter = ax.motionState;
    out.ok = true;
    return out;
}

// ============================================================================
// 点动（Step 3.4，经 JogSession 维护心跳）
// ============================================================================
PhysicalAxisCommissioningService::JogOutcome
PhysicalAxisCommissioningService::jog(
    plc_vnext::contracts::PlcAxisSlot slot, bool forward, int durationMs,
    bool confirmWrite, bool confirmMotion, int heartbeatPeriodMs) const {
    JogOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!confirmMotion) { out.diagnostic = "confirm-motion required"; return out; }
    const auto gate = evaluateGate(slot, CommissioningOperation::Jog);
    if (!gate.allow()) {
        out.gateReason = gate.reason;
        out.diagnostic = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return out;
    }
    if (jogActive_.exchange(true)) {
        out.gateReason = CommissioningGateReason::Busy;
        out.diagnostic = "another jog session active";
        return out;
    }
    struct ClearJog { std::atomic<bool>& flag; ~ClearJog() { flag.store(false); } } clear{jogActive_};

    // 1. 使能轴控 + 电机。jog() 已在上层闸门通过一次，这里直接写物理 slot，
    //    不再重复过闸门（避免把自身当成“另一个点动会话”而误判 Busy）。
    {
        auto r1 = gateway_.writeAxis(
            slot, plc_vnext::contracts::PlcAxisCommand::makeEnableAxis(true));
        if (!r1.ok()) { out.diagnostic = "enableAxis write failed: " + r1.diagnostic; return out; }
        auto r2 = gateway_.writeAxis(
            slot, plc_vnext::contracts::PlcAxisCommand::makeEnableMotor(true));
        if (!r2.ok()) {
            gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeEnableAxis(false));
            out.diagnostic = "enableMotor write failed: " + r2.diagnostic;
            return out;
        }
    }

    // 2. 方向命令（正 M80/M81，反 M96/M97；停止写 OFF）。
    const auto dirOn = forward ? plc_vnext::contracts::PlcAxisCommand::makeJogForward(true)
                               : plc_vnext::contracts::PlcAxisCommand::makeJogBackward(true);
    const auto dirOff = forward ? plc_vnext::contracts::PlcAxisCommand::makeJogForward(false)
                                : plc_vnext::contracts::PlcAxisCommand::makeJogBackward(false);

    // 3. 点动心跳会话（Step 3.1）：心跳线程 + 失败回调（关方向线圈）。
    JogSession::Config jcfg;
    jcfg.heartbeatPeriodMs = heartbeatPeriodMs;
    JogSession session(
        [this, slot]() -> bool {
            return gateway_.writeAxis(slot,
                plc_vnext::contracts::PlcAxisCommand::makeJogHeartbeat(true)).ok();
        },
        [this, slot]() -> bool {
            return gateway_.writeAxis(slot,
                plc_vnext::contracts::PlcAxisCommand::makeJogHeartbeat(false)).ok();
        },
        [this, slot, &dirOff]() { gateway_.writeAxis(slot, dirOff); },  // 心跳失败 → 关方向
        jcfg);
    if (!session.start()) { out.diagnostic = "jog session start failed"; return out; }

    // 4. 方向 ON。
    {
        auto r = gateway_.writeAxis(slot, dirOn);
        if (!r.ok()) {
            session.stop(true);
            out.diagnostic = "direction write failed: " + r.diagnostic;
            return out;
        }
    }

    // 5. 记录起点。
    if (!currentAbsPosition(slot, out.startPos)) out.startPos = 0.f;

    // 6. 监控循环（点动期间持续检查退出条件）。
    const int n = (durationMs <= 0) ? 0 : std::max(1, durationMs / kDefaultPollMs);
    for (int i = 0; i < n && session.active(); ++i) {
        auto safety = gateway_.readSafety();
        if (safety.hasValue() && safety.value().emergencyStop) {
            out.diagnostic = "emergency stop during jog";
            break;
        }
        auto rt = gateway_.readRuntime();
        if (rt.hasValue()) {
            const auto& ax = rt.value().axes[static_cast<std::size_t>(slot.value())];
            if (ax.alarmWord != 0) { out.diagnostic = "axis alarm during jog"; break; }
            if (ax.motionLimit != 0) { out.diagnostic = "limit reached during jog"; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kDefaultPollMs));
    }

    // 7. 方向 OFF（用户释放点动 / 会话结束）。
    gateway_.writeAxis(slot, dirOff);
    // 8. 停止心跳线程并补写心跳 OFF。
    session.stop(true);

    if (!currentAbsPosition(slot, out.endPos)) out.endPos = out.startPos;
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.finalMotionState = ax.motionState;

    out.heartbeatTimedOut = session.failed();
    if (out.heartbeatTimedOut && out.diagnostic.empty())
        out.diagnostic = "heartbeat write failed during jog";
    out.ok = out.diagnostic.empty();
    return out;
}



// ============================================================================
// 运动（Step 3.5 相对 / Step 3.6 绝对）与停止（Step 3.7）
// ============================================================================
PhysicalAxisCommissioningService::MoveOutcome
PhysicalAxisCommissioningService::moveRelative(
    plc_vnext::contracts::PlcAxisSlot slot, float delta, bool confirmWrite,
    bool confirmMotion) const {
    MoveOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!confirmMotion) { out.diagnostic = "confirm-motion required"; return out; }
    const auto gate = evaluateGate(slot, CommissioningOperation::Move);
    if (!gate.allow()) {
        out.gateReason = gate.reason;
        out.diagnostic = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return out;
    }
    out.target = delta;
    if (!currentAbsPosition(slot, out.startPos)) { out.diagnostic = "read start pos failed"; return out; }

    // 写很小的相对目标，读回确认。
    auto r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeSetRelTarget(delta));
    if (!r.ok()) { out.diagnostic = "write rel target failed: " + r.diagnostic; return out; }
    plc_vnext::contracts::AxisParameterSnapshot p;
    if (!readAxisParameter(slot, p) ||
        std::fabs(p.relMoveDistance - delta) > kPositionTolerance) {
        out.diagnostic = "rel target read-back mismatch";
        return out;
    }
    // 触发型线圈只写 ON，不手动写 OFF，等待 PLC 自动复位。
    r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeTriggerRelMove());
    if (!r.ok()) { out.diagnostic = "trigger rel move failed: " + r.diagnostic; return out; }

    out.completed = waitUntilIdle(slot, kDefaultMoveTimeoutMs, kDefaultPollMs);
    if (!currentAbsPosition(slot, out.endPos)) { out.diagnostic = "read end pos failed"; return out; }
    out.moved = std::fabs(out.endPos - out.startPos) > kPositionTolerance;
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.finalMotionState = ax.motionState;

    out.ok = out.completed && out.moved;
    if (!out.ok) out.diagnostic = out.completed ? "no motion observed" : "move not completed/timeout";
    return out;
}


PhysicalAxisCommissioningService::MoveOutcome
PhysicalAxisCommissioningService::moveAbsolute(
    plc_vnext::contracts::PlcAxisSlot slot, float target, bool confirmWrite,
    bool confirmMotion) const {
    MoveOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!confirmMotion) { out.diagnostic = "confirm-motion required"; return out; }
    const auto gate = evaluateGate(slot, CommissioningOperation::Move);
    if (!gate.allow()) {
        out.gateReason = gate.reason;
        out.diagnostic = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return out;
    }
    out.target = target;
    if (!currentAbsPosition(slot, out.startPos)) { out.diagnostic = "read start pos failed"; return out; }

    // 写绝对目标 P1，读回确认。
    auto r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeSetAbsTarget(target));
    if (!r.ok()) { out.diagnostic = "write abs target failed: " + r.diagnostic; return out; }
    plc_vnext::contracts::AxisParameterSnapshot p;
    if (!readAxisParameter(slot, p) ||
        std::fabs(p.absMoveDistance - target) > kPositionTolerance) {
        out.diagnostic = "abs target read-back mismatch";
        return out;
    }
    // 触发型线圈只写 ON，等待 PLC 自动复位。
    r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeTriggerAbsMove());
    if (!r.ok()) { out.diagnostic = "trigger abs move failed: " + r.diagnostic; return out; }

    out.completed = waitUntilIdle(slot, kDefaultMoveTimeoutMs, kDefaultPollMs);
    if (!currentAbsPosition(slot, out.endPos)) { out.diagnostic = "read end pos failed"; return out; }
    out.moved = std::fabs(out.endPos - target) <= kPositionTolerance;
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.finalMotionState = ax.motionState;

    out.ok = out.completed && out.moved;
    if (!out.ok) out.diagnostic = out.completed ? "abs target not reached" : "move not completed/timeout";
    return out;
}

PhysicalAxisCommissioningService::MoveOutcome
PhysicalAxisCommissioningService::stopMove(
    plc_vnext::contracts::PlcAxisSlot slot, bool confirmWrite,
    bool confirmMotion) const {
    MoveOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!confirmMotion) { out.diagnostic = "confirm-motion required"; return out; }
    const auto gate = evaluateGate(slot, CommissioningOperation::Stop);
    if (!gate.allow()) {
        out.gateReason = gate.reason;
        out.diagnostic = std::string("gate rejected: ") + gateReasonText(gate.reason);
        return out;
    }
    // 停止线圈为触发型：只写 ON，由 PLC 自动复位，不手动写 OFF。
    auto r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeStopAbsMove());
    if (!r.ok()) { out.diagnostic = "stop abs failed: " + r.diagnostic; return out; }
    r = gateway_.writeAxis(slot, plc_vnext::contracts::PlcAxisCommand::makeStopRelMove());
    if (!r.ok()) { out.diagnostic = "stop rel failed: " + r.diagnostic; return out; }

    out.completed = waitUntilIdle(slot, kDefaultMoveTimeoutMs, kDefaultPollMs);
    if (!currentAbsPosition(slot, out.endPos)) { out.diagnostic = "read end pos failed"; return out; }
    plc_vnext::contracts::AxisRuntimeSnapshot ax;
    if (readAxisRuntime(slot, ax)) out.finalMotionState = ax.motionState;
    out.ok = out.completed;
    if (!out.ok) out.diagnostic = "stop: axis not idle";
    return out;
}

// ============================================================================
// 急停（Step 3.8）
// ============================================================================
PhysicalAxisCommissioningService::EStopOutcome
PhysicalAxisCommissioningService::triggerEmergencyStop(bool confirmWrite) const {
    EStopOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!gateway_.connectionState().connected) { out.diagnostic = "Disconnected"; return out; }
    const auto r = gateway_.triggerEmergencyStop();
    out.ok = r.ok();
    out.diagnostic = r.diagnostic;
    return out;
}

PhysicalAxisCommissioningService::EStopOutcome
PhysicalAxisCommissioningService::requestReleaseEmergencyStop(bool confirmWrite) const {
    EStopOutcome out;
    if (!confirmWrite) { out.diagnostic = "confirm-write required"; return out; }
    if (!gateway_.connectionState().connected) { out.diagnostic = "Disconnected"; return out; }
    // 急停期间唯一允许的写：M225=ON（PLC 自复位）。
    const auto r = gateway_.requestEmergencyStopRelease();
    out.ok = r.ok();
    out.diagnostic = r.diagnostic;
    return out;
}


}  // namespace application_vnext::commissioning

