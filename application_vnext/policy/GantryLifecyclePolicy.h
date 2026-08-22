// ============================================================================
// GantryLifecyclePolicy.h —— 龙门生命周期策略（建立/复位/解除/掉电）
// ============================================================================
// 职责：管理龙门逻辑轴（X，A 组解析为 slot13）的电源生命周期，把"建立/复位/
// 解除 + 使能/掉电"独立于运动内核。每组同时只能有一个活动生命周期操作，避免
// Couple / Reset / Decouple 并发互相覆盖 GantryCommand.RequestSeq。
//
// 启动（beginCouple）——按 PLC 确认的启动顺序：
//   预检查 -> 使能轴控[X] -> 使能电机[X] -> 等 motionState==2
//     -> CommandErrorCode!=0 时先 Reset 并等清错 -> Couple -> 等最终成功 -> Ready
//   完成判定：State=3 && InternalStep=80 && CommandResult=2 && CommandErrorCode=0
//     && !Fault && X1InGear && X2InGear && LogicalControlAllowed && !MemberControlAllowed
//
// 解除（beginDecouple）：
//   确保逻辑轴已停 -> Decouple -> 等最终成功 -> EnableMotor[X]=OFF -> Done
//   完成判定：State=1 && InternalStep=10 && CommandResult=2 && CommandErrorCode=0
//     && !X1InGear && !X2InGear && !LogicalControlAllowed && MemberControlAllowed
//   未完成解除前不 EnableMotor[X]=OFF；EnableAxis[X] 保持当前约定不擅自关闭。
//
// 事务：GantryCommand 命令与 RequestSeq 自增由领域 GantryCouplingStateMachine
//   处理；提交阶段（含 CommitUncertain）经 AppVnextResult 返回，本策略在
//   CommitUncertain 时不重发新序号，进入等待靠 poll() 观察 AckSeq 闭环。
// 非阻塞：调用方每反馈周期先 m.poll() 再 tick()。
// ============================================================================
#pragma once

#include <chrono>
#include <string>

#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionCommon.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/logger/Logger.h"

namespace application_vnext::policy {

namespace gantry_lifecycle_detail {
constexpr double kResetTimeoutSeconds   = 5.0;
constexpr double kCoupleTimeoutSeconds  = 5.0;
constexpr double kDecoupleTimeoutSeconds= 5.0;
constexpr double kMotorReadyTimeoutSeconds = kEnableTimeoutSeconds;
}  // namespace gantry_lifecycle_detail

/// 龙门生命周期策略。构造持 SystemManagerVnext& + 组号；beginXxx 启动，
/// 调用方每反馈周期先 poll() 再 tick()；isReady()/isDone()/hasError()/diag() 查询。
class GantryLifecyclePolicy {
public:
    enum class Step {
        Idle,
        // 建立
        ValidatePreconditions,
        EnsureAxisControl, WaitAxisControlReady,
        EnsureMotor, WaitMotorReady,
        CheckGantryError, SubmitReset, WaitResetFinal,
        SubmitCouple, WaitCoupleFinal, Ready,
        // 解除
        EnsureLogicalAxisStopped, SubmitDecouple, WaitDecoupleFinal,
        DisableMotor, Done,
        Cancelled,   // 生命周期被取消（急停/断线/停止）：不再产生任何新 PLC 写
        Error
    };

    GantryLifecyclePolicy(SystemManagerVnext& m, plc_vnext::contracts::PlcGroupIndex g)
        : m_(&m), g_(g) {}

    /// 建立联动 + 使能逻辑轴。仅 Idle 可启动。
    void beginCouple();
    /// 解除联动 + 掉电逻辑轴。仅 Idle 可启动。
    void beginDecouple();
    /// 复位到 Idle（允许重新 begin）。若有进行中操作会静默忽略（由 caller 决定）。
    void reset() { step_ = Step::Idle; diag_.clear(); cancelled_ = false; }

    /// 受控取消（Phase 7 安全收口）：置 cancelled_。tick() 在真正提交（Couple/Decouple
    /// 已进入 AckSeq 等待）前终止，不再发任何使能/建立/解除写；若已进入 Wait*Final（事务
    /// 已提交），则转只读观察，等 AckSeq 自然收口到 Ready/Done 后以 Cancelled 结束。
    void cancel() { cancelled_ = true; }

    void tick();

    Step currentStep() const { return step_; }
    bool isReady() const { return step_ == Step::Ready; }
    bool isDone() const { return step_ == Step::Done || step_ == Step::Cancelled; }
    bool hasError() const { return step_ == Step::Error; }
    const std::string& diag() const { return diag_; }

    static const char* stepName(Step s);

private:
    enum class SubmitOutcome { Accepted, Uncertain, Failed };
    using clock = std::chrono::steady_clock;

    bool findLogicalAxis();
    int16_t motionState() const;
    const domain_vnext::model::GantryStatusModel& gs() const {
        return m_->gantryStatus(g_);
    }
    SubmitOutcome submitOutcome(const AppVnextResult& r) const;
    bool coupleFinalSatisfied(int32_t seq) const;
    bool decoupleFinalSatisfied(int32_t seq) const;
    bool resetFinalSatisfied(int32_t seq) const;
    /// 记录本次已提交请求的 RequestSeq（供 AckSeq 闭环）。
    void recordSubmittedSeq() { currentSeq_ = m_->lastGantryRequestSeq(g_); }
    void toError(const std::string& reason) {
        LOG_ERROR(LogLayer::APP, "GantryLifecycle",
                  "[gantry] group=" + std::to_string(g_.value())
                  + " step=" + stepName(step_) + " error=" + reason);
        step_ = Step::Error; diag_ = reason;
    }

    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcGroupIndex g_;
    Step step_ = Step::Idle;
    std::string diag_;
    const domain_vnext::system::Axis* logical_ = nullptr;
    clock::time_point stepStart_{};
    int32_t currentSeq_ = 0;
    bool cancelled_ = false;
    bool enabledSent_ = false;
    bool motorSent_ = false;
    bool stopSent_ = false;
    bool disableSent_ = false;
    bool resetFromFault_ = false;   // 本次建立是否由"故障复位"进入（复位后需完整使能+联动序列）
};

// ---------------------------------------------------------------------------
// 内联实现
// ---------------------------------------------------------------------------

inline void GantryLifecyclePolicy::beginCouple() {
    if (step_ != Step::Idle) return;   // 每组同一时间仅一个活动生命周期操作
    diag_.clear();
    cancelled_ = false;
    enabledSent_ = false; motorSent_ = false; stopSent_ = false; disableSent_ = false;
    currentSeq_ = 0;
    logical_ = nullptr;
    resetFromFault_ = false;
    LOG_INFO(LogLayer::APP, "GantryLifecycle", "[gantry] begin couple group=" + std::to_string(g_.value()));
    step_ = Step::ValidatePreconditions;
    stepStart_ = clock::now();
}

inline void GantryLifecyclePolicy::beginDecouple() {
    if (step_ != Step::Idle) return;
    diag_.clear();
    cancelled_ = false;
    enabledSent_ = false; motorSent_ = false; stopSent_ = false; disableSent_ = false;
    currentSeq_ = 0;
    // P0：解除入口立即解析逻辑轴，避免 motionState() 恒 0 卡在停止判断。
    if (!findLogicalAxis()) { toError("logical axis X not bound in group"); return; }
    LOG_INFO(LogLayer::APP, "GantryLifecycle", "[gantry] begin decouple group=" + std::to_string(g_.value()));
    step_ = Step::EnsureLogicalAxisStopped;
    stepStart_ = clock::now();
}

inline bool GantryLifecyclePolicy::findLogicalAxis() {
    logical_ = m_->system().find({g_, domain_vnext::model::AxisFunction::X});
    return logical_ != nullptr;
}

inline int16_t GantryLifecyclePolicy::motionState() const {
    return logical_ ? logical_->feedback().motionState : 0;
}

inline GantryLifecyclePolicy::SubmitOutcome GantryLifecyclePolicy::submitOutcome(
    const AppVnextResult& r) const {
    if (appResultOk(r)) return SubmitOutcome::Accepted;
    // CommitUncertain：Command 已写、RequestSeq 结果未知，绝不能据此重发新序号，
    // 由调用方继续 poll() 观察 AckSeq 闭环（本策略进入等待态）。
    const auto* cf = appErrorOf<GantryCommFailed>(r);
    if (cf) {
        LOG_ERROR(LogLayer::APP, "GantryLifecycle",
                  "[gantry] submit communication failed group=" + std::to_string(g_.value())
                  + " submitState=" + gantrySubmitStateName(cf->result.state)
                  + " commStatus=" + communicationStatusName(cf->result.result.status)
                  + " requestSeq=" + std::to_string(cf->result.requestSeq)
                  + " diag=" + cf->result.result.diagnostic);
        if (cf->result.committedUnknown()) return SubmitOutcome::Uncertain;
    }
    const auto* rr = appErrorOf<GantryRequestRejected>(r);
    if (rr) {
        LOG_ERROR(LogLayer::APP, "GantryLifecycle",
                  "[gantry] submit request rejected group=" + std::to_string(g_.value())
                  + " reason=" + gantryRequestResultName(rr->result));
    } else if (appErrorOf<AppNotBooted>(r)) {
        LOG_ERROR(LogLayer::APP, "GantryLifecycle",
                  "[gantry] submit failed group=" + std::to_string(g_.value())
                  + " reason=AppNotBooted");
    }
    return SubmitOutcome::Failed;
}

/// 统一闭环：AckSeq==本次 RequestSeq && CommandResult==2 && CommandErrorCode==0。
inline bool GantryLifecyclePolicy::coupleFinalSatisfied(int32_t seq) const {
    const auto& s = gs();
    return s.trusted && s.ackSeq == seq && s.rawState == 3 && s.internalStep == 80 &&
           s.commandResult == 2 && s.commandErrorCode == 0 && !s.fault &&
           s.x1InGear && s.x2InGear && s.logicalControlAllowed && !s.memberControlAllowed;
}

inline bool GantryLifecyclePolicy::decoupleFinalSatisfied(int32_t seq) const {
    const auto& s = gs();
    return s.trusted && s.ackSeq == seq && s.rawState == 1 && s.internalStep == 10 &&
           s.commandResult == 2 && s.commandErrorCode == 0 &&
           !s.x1InGear && !s.x2InGear && !s.logicalControlAllowed && s.memberControlAllowed;
}

inline bool GantryLifecyclePolicy::resetFinalSatisfied(int32_t seq) const {
    const auto& s = gs();
    return s.trusted && s.ackSeq == seq && s.commandResult == 2 && s.rawState == 1 &&
           s.internalStep == 10 && s.memberControlAllowed && s.commandErrorCode == 0;
}


inline void GantryLifecyclePolicy::tick() {
    if (step_ == Step::Idle || step_ == Step::Ready ||
        step_ == Step::Done || step_ == Step::Error || step_ == Step::Cancelled) return;
    const Step prev = step_;
    const auto now = clock::now();
    const double el = std::chrono::duration<double>(now - stepStart_).count();

    // Phase 7 安全收口：已取消时，
    //   - 若事务尚未提交（未进入 Wait*Final）：立即终止，不再发任何 Couple/使能/解除写；
    //   - 若事务已提交（WaitCoupleFinal / WaitDecoupleFinal / WaitResetFinal）：转只读观察，
    //     等 AckSeq 自然收口（含 Reset 的 AckSeq 闭环），期间保持租约、不发新龙门请求。
    if (cancelled_) {
        switch (step_) {
            case Step::WaitCoupleFinal:
            case Step::WaitDecoupleFinal:
            case Step::WaitResetFinal:
                break;   // 观察模式：下面各 case 只读反馈 + 超时，不产生新写
            default:
                step_ = Step::Cancelled;
                return;
        }
    }

    switch (step_) {
    case Step::ValidatePreconditions:
        if (!findLogicalAxis()) { toError("logical axis X not bound in group"); return; }
        if (m_->isSystemLocked()) { toError("system safety locked"); return; }
        if (!gs().trusted) { toError("gantry status not trusted"); return; }
        if (!m_->system().group(g_).isReady()) { toError("group not ready"); return; }
        // 故障（State=5 / fault 标志）：先复位（Command=3 + RequestSeq++）清错，
        // 复位完成后走完整使能+联动序列，而不是直接报错拒绝。
        if (gs().fault || gs().rawState == 5) {
            step_ = Step::SubmitReset; resetFromFault_ = true; stepStart_ = now; break;
        }
        if (gs().rawState != 1) {
            // 带实际状态值与名称，便于排查：0未配置 / 2建立中 / 4解除中。
            toError("gantry not decoupled (state=" + std::to_string(gs().rawState) + " "
                    + domain_vnext::model::gantryCouplingStateName(gs().coupling) + ")");
            return;
        }
        step_ = Step::EnsureAxisControl; stepStart_ = now; break;

    case Step::EnsureAxisControl:
        if (appResultOk(m_->enableAxis(g_, domain_vnext::model::AxisFunction::X, true))) {
            step_ = Step::WaitAxisControlReady; stepStart_ = now;
        } else { toError("enableAxis failed"); return; }
        break;

    case Step::WaitAxisControlReady:
        // 确认轴控 ON（motionState==1：轴控ON、电机OFF）后再开电机。
        if (motionState() == kMotionEnabledMotorOff || motionState() == kMotionMotorIdle) {
            step_ = Step::EnsureMotor; stepStart_ = now; break;
        }
        if (el >= gantry_lifecycle_detail::kMotorReadyTimeoutSeconds) {
            toError("axis-control ready timeout (motionState!=1/2)"); return;
        }
        break;

    case Step::EnsureMotor:
        if (appResultOk(m_->enableMotor(g_, domain_vnext::model::AxisFunction::X, true))) {
            step_ = Step::WaitMotorReady; stepStart_ = now;
        } else { toError("enableMotor failed"); return; }
        break;

    case Step::WaitMotorReady:
        if (motionState() == kMotionMotorIdle) {
            step_ = Step::CheckGantryError; stepStart_ = now; break;
        }
        if (el >= gantry_lifecycle_detail::kMotorReadyTimeoutSeconds) {
            toError("motor ready timeout (motionState!=2)"); return;
        }
        break;

    case Step::CheckGantryError:
        // 复位是恢复路径：仅当上次命令留有错误码时才先 Reset 清错再 Couple。
        if (gs().commandErrorCode != 0) { step_ = Step::SubmitReset; stepStart_ = now; }
        else { step_ = Step::SubmitCouple; stepStart_ = now; }
        break;

    case Step::SubmitReset:
        if (submitOutcome(m_->gantryReset(g_)) == SubmitOutcome::Failed) {
            toError("gantryReset submit failed"); return;
        }
        recordSubmittedSeq();   // 记录本次 Reset 的 RequestSeq（供 AckSeq 闭环）
        LOG_INFO(LogLayer::APP, "GantryLifecycle",
                 "[gantry] submit Reset group=" + std::to_string(g_.value())
                 + " reqSeq=" + std::to_string(currentSeq_));
        step_ = Step::WaitResetFinal; stepStart_ = now; break;

    case Step::WaitResetFinal:
        if (resetFinalSatisfied(currentSeq_)) {
            // 已取消时不进入后续（会产生写），直接 Cancelled。
            // 故障复位（resetFromFault_）：复位后需完整使能+联动序列（EnsureAxisControl 起）；
            // 否则（CheckGantryError 的清错复位，此时电机已使能）直接 SubmitCouple。
            const bool fromFault = resetFromFault_;
            resetFromFault_ = false;
            step_ = cancelled_ ? Step::Cancelled
                               : (fromFault ? Step::EnsureAxisControl : Step::SubmitCouple);
            if (step_ != Step::Cancelled) stepStart_ = now;
            break;
        }
        if (el >= gantry_lifecycle_detail::kResetTimeoutSeconds) {
            if (cancelled_) { step_ = Step::Cancelled; return; }
            toError("gantry reset timeout"); return;
        }
        break;

    case Step::SubmitCouple:
        if (submitOutcome(m_->gantryCouple(g_)) == SubmitOutcome::Failed) {
            toError("gantryCouple submit failed"); return;
        }
        recordSubmittedSeq();   // 记录本次 Couple 的 RequestSeq
        LOG_INFO(LogLayer::APP, "GantryLifecycle",
                 "[gantry] submit Couple group=" + std::to_string(g_.value())
                 + " reqSeq=" + std::to_string(currentSeq_));
        step_ = Step::WaitCoupleFinal; stepStart_ = now; break;

    case Step::WaitCoupleFinal:
        if (coupleFinalSatisfied(currentSeq_)) {
            step_ = cancelled_ ? Step::Cancelled : Step::Ready; break;
        }
        if (el >= gantry_lifecycle_detail::kCoupleTimeoutSeconds) {
            if (cancelled_) { step_ = Step::Cancelled; return; }
            toError("gantry couple timeout"); return;
        }
        break;

    case Step::EnsureLogicalAxisStopped:
        if (motionState() == kMotionMotorIdle) {
            step_ = Step::SubmitDecouple; stepStart_ = now; break;
        }
        if (!stopSent_) {
            if (!appResultOk(m_->stop(g_, domain_vnext::model::AxisFunction::X))) {
                toError("stop logical axis failed"); return;
            }
            stopSent_ = true;
        }
        if (el >= gantry_lifecycle_detail::kDecoupleTimeoutSeconds) {
            toError("logical axis stop timeout"); return;
        }
        break;

    case Step::SubmitDecouple:
        if (submitOutcome(m_->gantryDecouple(g_)) == SubmitOutcome::Failed) {
            toError("gantryDecouple submit failed"); return;
        }
        recordSubmittedSeq();   // 记录本次 Decouple 的 RequestSeq
        LOG_INFO(LogLayer::APP, "GantryLifecycle",
                 "[gantry] submit Decouple group=" + std::to_string(g_.value())
                 + " reqSeq=" + std::to_string(currentSeq_));
        step_ = Step::WaitDecoupleFinal; stepStart_ = now; break;

    case Step::WaitDecoupleFinal:
        if (decoupleFinalSatisfied(currentSeq_)) {
            // 已取消时不进入 DisableMotor（会产生 enableMotor=OFF 写），直接 Cancelled。
            step_ = cancelled_ ? Step::Cancelled : Step::DisableMotor;
            if (step_ != Step::Cancelled) stepStart_ = now;
            break;
        }
        if (el >= gantry_lifecycle_detail::kDecoupleTimeoutSeconds) {
            if (cancelled_) { step_ = Step::Cancelled; return; }
            toError("gantry decouple timeout"); return;
        }
        break;

    case Step::DisableMotor:
        if (!disableSent_) {
            if (!appResultOk(m_->enableMotor(g_, domain_vnext::model::AxisFunction::X, false))) {
                toError("disableMotor write failed"); return;
            }
            disableSent_ = true;
        }
        step_ = Step::Done; break;

    case Step::Idle: case Step::Ready: case Step::Done:
    case Step::Error: default: break;
    }
    if (step_ != prev) {
        LOG_DEBUG(LogLayer::APP, "GantryLifecycle",
                  "[gantry] step group=" + std::to_string(g_.value())
                  + " " + stepName(prev) + " -> " + stepName(step_));
    }

}

inline const char* GantryLifecyclePolicy::stepName(Step s) {
    switch (s) {
        case Step::Idle:                return "Idle";
        case Step::ValidatePreconditions:return "ValidatePreconditions";
        case Step::EnsureAxisControl:   return "EnsureAxisControl";
        case Step::WaitAxisControlReady:return "WaitAxisControlReady";
        case Step::EnsureMotor:         return "EnsureMotor";
        case Step::WaitMotorReady:      return "WaitMotorReady";
        case Step::CheckGantryError:    return "CheckGantryError";
        case Step::SubmitReset:         return "SubmitReset";
        case Step::WaitResetFinal:      return "WaitResetFinal";
        case Step::SubmitCouple:        return "SubmitCouple";
        case Step::WaitCoupleFinal:     return "WaitCoupleFinal";
        case Step::Ready:               return "Ready";
        case Step::EnsureLogicalAxisStopped:return "EnsureLogicalAxisStopped";
        case Step::SubmitDecouple:      return "SubmitDecouple";
        case Step::WaitDecoupleFinal:   return "WaitDecoupleFinal";
        case Step::DisableMotor:        return "DisableMotor";
        case Step::Done:                return "Done";
        case Step::Cancelled:           return "Cancelled";
        case Step::Error:               return "Error";
    }
    return "?";
}


}  // namespace application_vnext::policy
