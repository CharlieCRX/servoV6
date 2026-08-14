// ============================================================================
// SystemManagerVnext.h —— P5 app: 组合根门面（SystemManager 迁移到 domain_vnext）
// ============================================================================
// 设计稿 §8 P5：把旧 application/SystemManager + UseCase 迁移到 domain_vnext。
// 持有 AxisSystem + gateway::IPlcDriver。单轴用例走完整链路（§5.2）：
//   AxisSystem.find -> AxisStateMachine.submit -> Outbox.drain
//     -> CommandMapper.map -> 使能入口路由 -> driver.writeAxis
// 急停：安全状态机产生 EStopCommand，本门面暴露 popPendingEStop 供上层落地；
//   反馈经 applyEmergencyStopFeedback 注入。龙门：poll 注入 GantryStatusSnapshot
//   -> 联动状态机；requestCouple/Decouple/Reset -> driver.submitGantryRequest。
// 纯 C++：不依赖旧 domain/*、ISystemDriver、Qt、Modbus。
// ============================================================================
#pragma once

#include <optional>
#include <variant>

#include "application_vnext/AppVnextError.h"
#include "domain_vnext/command/CommandMapper.h"
#include "domain_vnext/gateway/IPlcDriver.h"
#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/model/GantryParam.h"
#include "domain_vnext/system/AxisSystem.h"
#include "domain_vnext/system/FeedbackDispatcher.h"
#include "domain_vnext/system/SystemBoot.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext {

/// 组合根门面：应用层对 domain_vnext 的唯一入口。
class SystemManagerVnext {
public:
    explicit SystemManagerVnext(domain_vnext::gateway::IPlcDriver& driver) : driver_(&driver) {}

    domain_vnext::system::AxisSystem& system() { return sys_; }
    const domain_vnext::system::AxisSystem& system() const { return sys_; }
    bool booted() const { return booted_; }

    // ---- boot / poll ----
    bool boot();  // 读拓扑 -> 动态建轴/分组/HmiVisible；随后读一次运行快照注入反馈
    bool poll();  // 读运行快照 -> 注入轴反馈前7项 + 各龙门组状态
    void applyParameters(
        const std::array<plc_vnext::contracts::AxisParameterSnapshot,
                         plc_vnext::contracts::kRuntimeAxisCount>& params);

    // ---- 单轴用例（默认 A 组，g=0）----
    AppVnextResult enableAxis(domain_vnext::model::AxisFunction fn, bool on);
    AppVnextResult enableMotor(domain_vnext::model::AxisFunction fn, bool on);
    AppVnextResult jog(domain_vnext::model::AxisFunction fn, bool forward, bool on);
    AppVnextResult stopJog(domain_vnext::model::AxisFunction fn, bool forward);
    /// 点动心跳（保持电平线圈）：周期写 ON 维持，停止时补写 OFF。供 JogPolicy 使用。
    AppVnextResult jogHeartbeat(domain_vnext::model::AxisFunction fn, bool on);
    AppVnextResult setManualSpeed(domain_vnext::model::AxisFunction fn, float v);
    AppVnextResult setPositioningSpeed(domain_vnext::model::AxisFunction fn, float v);
    AppVnextResult setAbsTarget(domain_vnext::model::AxisFunction fn, float v);
    AppVnextResult setRelTarget(domain_vnext::model::AxisFunction fn, float v);
    AppVnextResult triggerAbsMove(domain_vnext::model::AxisFunction fn);
    AppVnextResult triggerRelMove(domain_vnext::model::AxisFunction fn);
    /// 停止：发出绝对/相对终止（PLC 自复位，只写 ON）。
    AppVnextResult stop(domain_vnext::model::AxisFunction fn);
    AppVnextResult clearRelZero(domain_vnext::model::AxisFunction fn);
    AppVnextResult setRelZero(domain_vnext::model::AxisFunction fn);
    AppVnextResult clearAbsPosition(domain_vnext::model::AxisFunction fn);

    // ---- 安全（全局急停）----
    AppVnextResult requestEmergencyStop();      // EStopCommand{true}  (M224 锁存)
    AppVnextResult requestReleaseEmergencyStop();  // EStopCommand{false} (M225 沿解除)
    bool hasPendingEStop() const { return sys_.safety().hasPendingCommand(); }
    domain_vnext::state::EStopCommand popPendingEStop() { return sys_.safety().popPendingCommand(); }
    void applyEmergencyStopFeedback(bool plcEmergencyStopped) {
        sys_.safety().applyFeedback(plcEmergencyStopped);
    }
    bool isSystemLocked() const { return sys_.safety().isSystemLocked(); }

    // ---- 龙门联动 ----
    AppVnextResult gantryCouple(plc_vnext::contracts::PlcGroupIndex g);
    AppVnextResult gantryDecouple(plc_vnext::contracts::PlcGroupIndex g);
    AppVnextResult gantryReset(plc_vnext::contracts::PlcGroupIndex g);
    void applyGantryConfig(plc_vnext::contracts::PlcGroupIndex g,
                           const domain_vnext::model::GantryParamModel& cfg) {
        sys_.group(g).gantryCoupling().applyConfig(cfg);
    }

private:
    // 单轴意图 -> 校验 -> drain -> 映射 -> 路由 -> 写入
    AppVnextResult submitAndFlush(int g, domain_vnext::model::AxisFunction fn,
                                  const domain_vnext::model::AxisCommand& intent);
    AppVnextResult writeAxisFor(plc_vnext::contracts::PlcGroupIndex g,
                                domain_vnext::model::AxisFunction fn,
                                const domain_vnext::model::AxisCommand& cmd);
    // 龙门请求事务落地
    AppVnextResult flushGantry(plc_vnext::contracts::PlcGroupIndex g,
                               domain_vnext::state::GantryCouplingStateMachine& sm);

    AppVnextResult submitCoil(int g, domain_vnext::model::AxisFunction fn,
                              domain_vnext::model::AxisCommandKind k, bool level) {
        return submitAndFlush(g, fn, domain_vnext::model::AxisCommand{k, 0.f, level});
    }
    AppVnextResult submitParam(int g, domain_vnext::model::AxisFunction fn,
                               domain_vnext::model::AxisCommandKind k, float v) {
        return submitAndFlush(g, fn, domain_vnext::model::AxisCommand{k, v, false});
    }
    AppVnextResult submitPulse(int g, domain_vnext::model::AxisFunction fn,
                               domain_vnext::model::AxisCommandKind k) {
        return submitAndFlush(g, fn, domain_vnext::model::AxisCommand{k, 0.f, false});
    }

    domain_vnext::system::AxisSystem sys_;
    domain_vnext::gateway::IPlcDriver* driver_ = nullptr;
    bool booted_ = false;
};

// ============================================================================
// 内联实现
// ============================================================================
inline bool SystemManagerVnext::boot() {
    if (!driver_) return false;
    auto topoRes = driver_->readTopology();
    if (!topoRes.hasValue()) {
        booted_ = false;
        return false;
    }
    const auto bootRes = domain_vnext::system::SystemBoot::initialize(sys_, *topoRes);
    booted_ = true;          // 已初始化（是否可控制由 sys_.isReady() 表达）
    poll();                  // 注入运行反馈 + 龙门状态
    return bootRes.ok;
}

inline bool SystemManagerVnext::poll() {
    if (!booted_ || !driver_) return false;
    auto res = driver_->readRuntime();
    if (!res.hasValue()) return false;
    domain_vnext::system::FeedbackDispatcher::dispatch(sys_, *res);
    return true;
}

inline void SystemManagerVnext::applyParameters(
    const std::array<plc_vnext::contracts::AxisParameterSnapshot,
                     plc_vnext::contracts::kRuntimeAxisCount>& params) {
    domain_vnext::system::FeedbackDispatcher::dispatchParameters(sys_, params);
}

inline AppVnextResult SystemManagerVnext::requestEmergencyStop() {
    if (!booted_) return AppNotBooted{};
    const auto rej = sys_.safety().requestEmergencyStop();
    if (rej != domain_vnext::state::SafetyRejection::None) return SafetyRejected{rej};
    return std::monostate{};
}

inline AppVnextResult SystemManagerVnext::requestReleaseEmergencyStop() {
    if (!booted_) return AppNotBooted{};
    const auto rej = sys_.safety().requestReleaseEmergencyStop();
    if (rej != domain_vnext::state::SafetyRejection::None) return SafetyRejected{rej};
    return std::monostate{};
}

inline AppVnextResult SystemManagerVnext::gantryCouple(plc_vnext::contracts::PlcGroupIndex g) {
    if (!booted_) return AppNotBooted{};
    auto& sm = sys_.group(g).gantryCoupling();
    const auto rr = sm.requestCouple();
    if (rr != domain_vnext::state::GantryCouplingStateMachine::RequestResult::Accepted) {
        return GantryRequestRejected{rr};
    }
    return flushGantry(g, sm);
}

inline AppVnextResult SystemManagerVnext::gantryDecouple(plc_vnext::contracts::PlcGroupIndex g) {
    if (!booted_) return AppNotBooted{};
    auto& sm = sys_.group(g).gantryCoupling();
    const auto rr = sm.requestDecouple();
    if (rr != domain_vnext::state::GantryCouplingStateMachine::RequestResult::Accepted) {
        return GantryRequestRejected{rr};
    }
    return flushGantry(g, sm);
}

inline AppVnextResult SystemManagerVnext::gantryReset(plc_vnext::contracts::PlcGroupIndex g) {
    if (!booted_) return AppNotBooted{};
    auto& sm = sys_.group(g).gantryCoupling();
    const auto rr = sm.requestReset();
    if (rr != domain_vnext::state::GantryCouplingStateMachine::RequestResult::Accepted) {
        return GantryRequestRejected{rr};
    }
    return flushGantry(g, sm);
}

inline AppVnextResult SystemManagerVnext::submitAndFlush(
    int g, domain_vnext::model::AxisFunction fn,
    const domain_vnext::model::AxisCommand& intent) {
    if (!booted_) return AppNotBooted{};
    const auto group = plc_vnext::contracts::PlcGroupIndex(g);
    auto& gm = sys_.group(group);
    if (!gm.isReady()) return AxisNotReady{fn};

    auto* axis = sys_.find({group, fn});
    if (!axis) return AxisNotFound{fn};

    const auto sr = axis->stateMachine().submit(intent, axis->outbox());
    if (sr != domain_vnext::state::AxisStateMachine::SubmitResult::Accepted) {
        return SubmitRejectedState{sr};
    }

    for (const auto& c : axis->outbox().drain()) {
        auto r = writeAxisFor(group, fn, c);
        if (!appResultOk(r)) return r;
    }
    return std::monostate{};
}

inline AppVnextResult SystemManagerVnext::writeAxisFor(
    plc_vnext::contracts::PlcGroupIndex g, domain_vnext::model::AxisFunction fn,
    const domain_vnext::model::AxisCommand& cmd) {
    auto* axis = sys_.find({g, fn});
    if (!axis) return AxisNotFound{fn};

    const auto mapped = domain_vnext::command::CommandMapper::mapAxis(cmd);
    if (!mapped.ok()) return CommandUnsupported{cmd.kind};

    // A 组使能入口路由（§4.6a）：龙门成员 X1/X2 的 Enable* 改写为组内逻辑轴槽位。
    auto* logical = sys_.find({g, domain_vnext::model::AxisFunction::X});
    std::optional<plc_vnext::contracts::PlcAxisSlot> logicalSlot =
        logical ? std::optional<plc_vnext::contracts::PlcAxisSlot>(logical->slot())
                : std::nullopt;
    const auto slot = domain_vnext::command::CommandMapper::effectiveEnableSlot(
        fn, mapped.cmd.kind, axis->slot(), logicalSlot);

    const auto comm = driver_->writeAxis(slot, mapped.cmd);
    if (!comm.ok()) return CommFailed{comm};
    return std::monostate{};
}

inline AppVnextResult SystemManagerVnext::flushGantry(
    plc_vnext::contracts::PlcGroupIndex g,
    domain_vnext::state::GantryCouplingStateMachine& sm) {
    if (sm.hasPendingRequest()) {
        const auto req = sm.popPendingRequest();
        const auto res = driver_->submitGantryRequest(g, req);
        if (!res.ok()) return GantryCommFailed{res};
    }
    return std::monostate{};
}

// ---- 单轴用例实现 ----

inline AppVnextResult SystemManagerVnext::enableAxis(domain_vnext::model::AxisFunction fn, bool on) {
    return submitCoil(0, fn, domain_vnext::model::AxisCommandKind::EnableAxis, on);
}
inline AppVnextResult SystemManagerVnext::enableMotor(domain_vnext::model::AxisFunction fn, bool on) {
    return submitCoil(0, fn, domain_vnext::model::AxisCommandKind::EnableMotor, on);
}
inline AppVnextResult SystemManagerVnext::jog(domain_vnext::model::AxisFunction fn, bool forward, bool on) {
    return submitCoil(0, fn,
        forward ? domain_vnext::model::AxisCommandKind::JogForward
                : domain_vnext::model::AxisCommandKind::JogBackward, on);
}
inline AppVnextResult SystemManagerVnext::stopJog(domain_vnext::model::AxisFunction fn, bool forward) {
    return submitCoil(0, fn,
        forward ? domain_vnext::model::AxisCommandKind::JogForward
                : domain_vnext::model::AxisCommandKind::JogBackward, false);
}
inline AppVnextResult SystemManagerVnext::jogHeartbeat(domain_vnext::model::AxisFunction fn, bool on) {
    return submitCoil(0, fn, domain_vnext::model::AxisCommandKind::JogHeartbeat, on);
}
inline AppVnextResult SystemManagerVnext::setManualSpeed(domain_vnext::model::AxisFunction fn, float v) {
    return submitParam(0, fn, domain_vnext::model::AxisCommandKind::SetManualSpeed, v);
}
inline AppVnextResult SystemManagerVnext::setPositioningSpeed(domain_vnext::model::AxisFunction fn, float v) {
    return submitParam(0, fn, domain_vnext::model::AxisCommandKind::SetPositioningSpeed, v);
}
inline AppVnextResult SystemManagerVnext::setAbsTarget(domain_vnext::model::AxisFunction fn, float v) {
    return submitParam(0, fn, domain_vnext::model::AxisCommandKind::SetAbsDistance, v);
}
inline AppVnextResult SystemManagerVnext::setRelTarget(domain_vnext::model::AxisFunction fn, float v) {
    return submitParam(0, fn, domain_vnext::model::AxisCommandKind::SetRelDistance, v);
}
inline AppVnextResult SystemManagerVnext::triggerAbsMove(domain_vnext::model::AxisFunction fn) {
    return submitPulse(0, fn, domain_vnext::model::AxisCommandKind::TriggerAbsMove);
}
inline AppVnextResult SystemManagerVnext::triggerRelMove(domain_vnext::model::AxisFunction fn) {
    return submitPulse(0, fn, domain_vnext::model::AxisCommandKind::TriggerRelMove);
}
inline AppVnextResult SystemManagerVnext::stop(domain_vnext::model::AxisFunction fn) {
    auto a = submitPulse(0, fn, domain_vnext::model::AxisCommandKind::StopAbsMove);
    return appResultOk(a) ? submitPulse(0, fn,
        domain_vnext::model::AxisCommandKind::StopRelMove) : a;
}
inline AppVnextResult SystemManagerVnext::clearRelZero(domain_vnext::model::AxisFunction fn) {
    return submitPulse(0, fn, domain_vnext::model::AxisCommandKind::ClearRelZero);
}
inline AppVnextResult SystemManagerVnext::setRelZero(domain_vnext::model::AxisFunction fn) {
    return submitPulse(0, fn, domain_vnext::model::AxisCommandKind::SetRelZero);
}
inline AppVnextResult SystemManagerVnext::clearAbsPosition(domain_vnext::model::AxisFunction fn) {
    return submitPulse(0, fn, domain_vnext::model::AxisCommandKind::ClearAbsPosition);
}

}  // namespace application_vnext
