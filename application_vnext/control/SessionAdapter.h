// ============================================================================
// SessionAdapter.h —— Phase 3：具体运动策略 -> ISessionPolicy 会话适配器
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.3 与 Phase 3。
//
// MotionControlService 通过适配器把 AbsMovePolicy / RelMovePolicy / JogPolicy /
// GantryLifecyclePolicy 包成 ISessionPolicy，统一收进 sessions_，屏蔽各策略差异：
//   - requestStop()：优雅停止（Jog 走 requestStop；定位写 stop() 让 PLC 自然回 idle）；
//   - cancel(reason) ：强制取消并保留原因（急停 / 失联 / Revision 变化 / 全局锁定）；
//   - resources()    ：本会话占用的资源集合（租约/互斥用）；
//   - tick()/isDone()/hasError()/diag()/currentStepName()：唯一 tick 推进与终结判定。
//
// 只依赖 application_vnext + domain_vnext + plc_vnext contracts，无 Qt / Modbus。
// ============================================================================
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/OperationState.h"
#include "application_vnext/control/SessionPolicy.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/GantryLifecyclePolicy.h"
#include "application_vnext/policy/JogPolicy.h"
#include "application_vnext/policy/RelMovePolicy.h"
#include "application_vnext/SystemManagerVnext.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::control::session_adapter {

// 策略类位于 application_vnext::policy，此处收敛便于本层统一引用。
using application_vnext::policy::AbsMovePolicy;
using application_vnext::policy::RelMovePolicy;
using application_vnext::policy::JogPolicy;
using application_vnext::policy::GantryLifecyclePolicy;

/// 会话公共底座：持有 operationId / owner / target / resources / kind，
/// 以及「受控停止/取消」的通用状态。具体策略差异由子类收敛。
class SessionBase : public ISessionPolicy {
public:
    SessionBase(std::string opId, ControlSource owner, AxisTarget target,
                std::vector<ControlResource> res, OperationKind kind)
        : opId_(std::move(opId)), owner_(owner), target_(target),
          resources_(std::move(res)), kind_(kind) {}

    // ---- ISessionPolicy：公共实现 ----
    bool isStopping() const override { return stopping_; }
    std::vector<ControlResource> resources() const override { return resources_; }

    // ---- 供协调层查询 ----
    const std::string& operationId() const { return opId_; }
    ControlSource owner() const { return owner_; }
    AxisTarget target() const { return target_; }
    OperationKind kind() const { return kind_; }
    const std::string& cancelReason() const { return cancelReason_; }

protected:
    std::string opId_;
    ControlSource owner_;
    AxisTarget target_;
    std::vector<ControlResource> resources_;
    OperationKind kind_;
    bool stopping_ = false;        // 已请求停止/取消（终止后 OperationEntry -> Cancelled）
    std::string cancelReason_;
};

/// 定位会话（绝对 / 相对）：requestStop / cancel 写一次 stop()（自复位脉冲），
/// PLC 停止运动后策略自然走到 Done；不用后台线程，完全由唯一 tick 驱动。
template <typename Policy>
class PositioningSession : public SessionBase {
public:
    PositioningSession(SystemManagerVnext& m, Policy p, std::string opId,
                       ControlSource owner, AxisTarget target,
                       std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          m_(&m), policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        if (policy_.isDone() || policy_.hasError()) return;
        issueStop();
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        if (policy_.isDone() || policy_.hasError()) return;
        issueStop();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override { return policy_.isDone(); }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return Policy::stepName(policy_.currentStep());
    }

private:
    void issueStop() {
        if (stopIssued_) return;
        stopIssued_ = true;
        m_->stop(target_.group, target_.function);
    }

    SystemManagerVnext* m_;
    Policy policy_;
    bool stopIssued_ = false;
};

/// 点动会话：requestStop / cancel 委托给 JogPolicy::requestStop()
/// （方向 OFF + 心跳 OFF + 掉电，均为策略内收尾，无后台线程）。
class JogSession : public SessionBase {
public:
    JogSession(JogPolicy p, std::string opId, ControlSource owner, AxisTarget target,
               std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        if (policy_.isDone() || policy_.hasError()) return;
        policy_.forceStopNow();
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        if (policy_.isDone() || policy_.hasError()) return;
        policy_.forceStopNow();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override { return policy_.isDone(); }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return JogPolicy::stepName(policy_.currentStep());
    }

private:
    JogPolicy policy_;
};

/// 龙门生命周期会话：具体 GantryLifecyclePolicy 无 requestStop/cancel 原语。
/// 优雅停止由「解除（Decouple）」流程自然完成；强制取消在此仅记录原因，
/// 让生命周期策略自行推进到 Done/Error（龙门急停收口留 Phase 7）。
class GantryLifecycleSession : public SessionBase {
public:
    GantryLifecycleSession(GantryLifecyclePolicy p, std::string opId,
                           ControlSource owner, AxisTarget target,
                           std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          policy_(std::move(p)) {}

    void tick() override { policy_.tick(); }

    void requestStop() override {
        policy_.cancel();   // 停止 = 生命周期安全取消（急停/断线同样走 cancel）
        stopping_ = true;
    }
    void cancel(std::string_view reason) override {
        policy_.cancel();
        stopping_ = true;
        cancelReason_ = std::string(reason);
    }

    bool isDone() const override {
        // 龙门生命周期成功终态：couple 停在 Ready（isReady()，经 PLC 侧
        // State=3/Step=80/CommandResult=2 确认）、decouple 停在 Done（isDone()）。
        // 二者都是「操作成功结束」：必须让 tickSessions() 据此释放 gantry:A:0 租约并
        // 移除会话，否则 couple 会话会永久停在 Running、租约永不释放（Phase 7）。
        return policy_.isDone() || policy_.isReady();
    }
    bool hasError() const override { return policy_.hasError(); }
    std::string diag() const override { return policy_.diag(); }
    std::string currentStepName() const override {
        return GantryLifecyclePolicy::stepName(policy_.currentStep());
    }

private:
    GantryLifecyclePolicy policy_;
};

/// 龙门自动点动会话：复刻 plc_vnext_motion_probe 的 gantry-run-jog 组合闭环。
/// 顺序驱动三段策略：建立联动+使能（couple -> Ready）→ 点动（jog）→ 解除+掉电（decouple -> Done）。
/// 每次"按下"从 couple 开始（已联动则跳过直接点动）；"松开/停止/取消"在点动结束后进入 decouple，
/// 无论运动成败都安全解除（与探针 [3] 一致）。要求：目标为龙门逻辑轴 X，资源占完整龙门集合。
class GantryAutoJogSession : public SessionBase {
public:
    GantryAutoJogSession(SystemManagerVnext& m, plc_vnext::contracts::PlcGroupIndex g,
                         GantryLifecyclePolicy couple, JogPolicy jog,
                         GantryLifecyclePolicy decouple, std::string opId,
                         ControlSource owner, AxisTarget target,
                         std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          m_(&m), g_(g), couple_(std::move(couple)),
          jog_(std::move(jog)), decouple_(std::move(decouple)) {}

    void tick() override {
        if (phase_ == Phase::Done) return;
        switch (phase_) {
        case Phase::Coupling:
            if (!stateLogged_) {
                stateLogged_ = true;
                const auto& gs = m_->gantryStatus(g_);
                LOG_WARN(LogLayer::APP, "GantryAutoJog",
                         "[gantry] auto-jog start group=" + std::to_string(g_.value())
                         + " gantryState=" + std::to_string(gs.rawState) + " "
                         + domain_vnext::model::gantryCouplingStateName(gs.coupling)
                         + " trusted=" + (gs.trusted ? "1" : "0"));
            }
            // 已联动则跳过建立（幂等：couple 策略要求 state!=1 会报错，故直接点动）。
            if (m_->gantryStatus(g_).rawState == 3) { phase_ = Phase::Jogging; break; }
            couple_.tick();
            if (couple_.hasError()) { error_ = couple_.diag(); phase_ = Phase::Done; return; }
            if (couple_.isDone()) { phase_ = Phase::Done; return; }   // 建立中被打断（已取消）：不点动、不解除
            if (couple_.isReady()) phase_ = Phase::Jogging;           // 联动成功 -> 点动
            break;
        case Phase::Jogging:
            jog_.tick();
            if (jog_.hasError()) error_ = jog_.diag();                // 运动异常：记录，仍进解除收口
            if (jog_.isDone()) phase_ = Phase::Decoupling;            // 点动结束（停止/限位/异常）-> 解除
            break;
        case Phase::Decoupling:
            decouple_.tick();
            if (decouple_.hasError()) { error_ = decouple_.diag(); phase_ = Phase::Done; return; }
            if (decouple_.isDone()) phase_ = Phase::Done;
            break;
        case Phase::Done:
            break;
        }
    }

    void requestStop() override {
        stopping_ = true;
        if (phase_ == Phase::Coupling) couple_.cancel();
        else if (phase_ == Phase::Jogging) jog_.forceStopNow();
        else if (phase_ == Phase::Decoupling) decouple_.cancel();
    }
    void cancel(std::string_view reason) override {
        stopping_ = true;
        cancelReason_ = std::string(reason);
        if (phase_ == Phase::Coupling) couple_.cancel();
        else if (phase_ == Phase::Jogging) jog_.forceStopNow();
        else if (phase_ == Phase::Decoupling) decouple_.cancel();
    }

    bool isDone() const override { return phase_ == Phase::Done; }
    bool hasError() const override { return !error_.empty(); }
    std::string diag() const override { return error_; }
    std::string currentStepName() const override {
        switch (phase_) {
            case Phase::Coupling:   return std::string("gantry-jog[couple] ") + GantryLifecyclePolicy::stepName(couple_.currentStep());
            case Phase::Jogging:    return std::string("gantry-jog[jog] ") + JogPolicy::stepName(jog_.currentStep());
            case Phase::Decoupling: return std::string("gantry-jog[decouple] ") + GantryLifecyclePolicy::stepName(decouple_.currentStep());
            case Phase::Done:       return "gantry-jog[done]";
        }
        return "gantry-jog[?]";
    }

private:
    enum class Phase { Coupling, Jogging, Decoupling, Done };
    Phase phase_ = Phase::Coupling;
    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcGroupIndex g_;
    GantryLifecyclePolicy couple_;
    JogPolicy jog_;
    GantryLifecyclePolicy decouple_;
    std::string error_;
    bool stateLogged_ = false;
};

/// 龙门自动定位会话：与 GantryAutoJogSession 对称的「建立联动+使能 → 定位 → 解除+掉电」组合闭环。
/// 修复龙门位置移动（StartAbsMove / StartRelMove）缺少点动同款初始配置的缺陷：
///   原实现只调用 beginAbs/beginRel（LifecycleManaged 跳过使能 + 依赖 guard 已联动），
///   未联动/未使能时位置移动直接失败（"gantry not coupled (state!=3)" / 未使能）。
/// 本会话在 Moving 前先由 GantryLifecyclePolicy 完成「使能轴控 → 使能电机 → Couple → Ready」，
/// 运动结束后再 Decouple + 掉电，与点动闭环语义一致。要求：目标为龙门逻辑轴 X。
template <typename MovePolicy>
class GantryAutoMoveSession : public SessionBase {
public:
    GantryAutoMoveSession(SystemManagerVnext& m, plc_vnext::contracts::PlcGroupIndex g,
                          GantryLifecyclePolicy couple, MovePolicy move,
                          GantryLifecyclePolicy decouple, std::string opId,
                          ControlSource owner, AxisTarget target,
                          std::vector<ControlResource> res, OperationKind kind)
        : SessionBase(std::move(opId), owner, target, std::move(res), kind),
          m_(&m), g_(g), couple_(std::move(couple)),
          move_(std::move(move)), decouple_(std::move(decouple)) {}

    void tick() override {
        if (phase_ == Phase::Done) return;
        switch (phase_) {
        case Phase::Coupling:
            // 已联动则跳过建立（幂等：couple 策略要求 state!=1 会报错，故直接定位）。
            if (m_->gantryStatus(g_).rawState == 3) { phase_ = Phase::Moving; break; }
            couple_.tick();
            if (couple_.hasError()) { error_ = couple_.diag(); phase_ = Phase::Done; return; }
            if (couple_.isDone()) { phase_ = Phase::Done; return; }   // 建立中被打断（已取消）：不定位、不解除
            if (couple_.isReady()) phase_ = Phase::Moving;            // 联动成功 -> 定位
            break;
        case Phase::Moving:
            move_.tick();
            if (move_.hasError()) error_ = move_.diag();              // 运动异常：记录，仍进解除收口
            // 无论正常结束还是运动异常，都必须进入解除收口（防止 Error 时 isDone()==false
            // 导致会话卡死在 Moving、龙门保持联动+使能 + 租约永不释放）。
            if (move_.isDone() || move_.hasError()) phase_ = Phase::Decoupling;
            break;
        case Phase::Decoupling:
            decouple_.tick();
            if (decouple_.hasError()) { error_ = decouple_.diag(); phase_ = Phase::Done; return; }
            if (decouple_.isDone()) phase_ = Phase::Done;
            break;
        case Phase::Done:
            break;
        }
    }

    void requestStop() override {
        stopping_ = true;
        if (phase_ == Phase::Coupling) couple_.cancel();
        else if (phase_ == Phase::Moving) issueMoveStop();   // 写 Stop*（自复位），PLC 停稳后 move_ 自然 Done
        else if (phase_ == Phase::Decoupling) decouple_.cancel();
    }
    void cancel(std::string_view reason) override {
        stopping_ = true;
        cancelReason_ = std::string(reason);
        if (phase_ == Phase::Coupling) couple_.cancel();
        else if (phase_ == Phase::Moving) issueMoveStop();
        else if (phase_ == Phase::Decoupling) decouple_.cancel();
    }

    bool isDone() const override { return phase_ == Phase::Done; }
    bool hasError() const override { return !error_.empty(); }
    std::string diag() const override { return error_; }
    std::string currentStepName() const override {
        switch (phase_) {
            case Phase::Coupling:   return std::string("gantry-move[couple] ") + GantryLifecyclePolicy::stepName(couple_.currentStep());
            case Phase::Moving:     return std::string("gantry-move[move] ") + MovePolicy::stepName(move_.currentStep());
            case Phase::Decoupling: return std::string("gantry-move[decouple] ") + GantryLifecyclePolicy::stepName(decouple_.currentStep());
            case Phase::Done:       return "gantry-move[done]";
        }
        return "gantry-move[?]";
    }

private:
    /// 定位停止与 PositioningSession::issueStop 一致：写一次 Stop*（自复位脉冲）。
    void issueMoveStop() {
        if (moveStopIssued_) return;
        moveStopIssued_ = true;
        m_->stop(g_, domain_vnext::model::AxisFunction::X);
    }

    enum class Phase { Coupling, Moving, Decoupling, Done };
    Phase phase_ = Phase::Coupling;
    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcGroupIndex g_;
    GantryLifecyclePolicy couple_;
    MovePolicy move_;
    GantryLifecyclePolicy decouple_;
    std::string error_;
    bool moveStopIssued_ = false;
};


}  // namespace application_vnext::control::session_adapter
