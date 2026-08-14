// ============================================================================
// AbsMovePolicy.h —— vnext 绝对定位触发策略（对照旧 application/policy/AbsMovePolicy.h）
// ============================================================================
// 编排：使能 → PostEnableDelay → TriggeringMove(★仅触发) → WaitingMotionStart
//       → WaitingMotionFinish(自然回 idle) → PostStopDelay → 掉电 → Done
// ★ 不包含目标写入 —— 目标已在独立 setAbsTarget() 路径写入 PLC。
//   （与旧 AutoAbsMoveOrchestrator 的"下发完整指令"不同，本策略只触发 ABS_MOVE_TRIGGER）
// 写通道：SystemManagerVnext 原子用例（复用 domain 意图校验 + CommandOutbox）。
// 反馈源：Axis::feedback().motionState（由 SystemManagerVnext::poll() 注入）：
//   2=电机使能空闲（使能完成/运动完毕）；5=绝对定位执行中；0=未使能。
// 非阻塞：调用方每反馈周期调 tick()（生产：主 poll 循环；调试：阻塞便利 run）。
// 纯 C++：只依赖 application_vnext + domain_vnext + plc_vnext contracts。
// ============================================================================
#pragma once

#include <chrono>
#include <cmath>
#include <limits>
#include <string>

#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AxisMotionCommon.h"
#include "application_vnext/policy/GantryMotionGuard.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace application_vnext::policy {

class AbsMovePolicy {
public:
    enum class Step {
        Initial, EnsuringEnabled, PostEnableDelay, TriggeringMove,
        WaitingMotionStart, WaitingMotionFinish, PostStopDelay,
        Disabling, Done, Error
    };

    /// @param m  SystemManagerVnext（写原子用例 + 读反馈）
    /// @param slot 目标物理槽位（须已在拓扑中绑定到某 AxisFunction）
    AbsMovePolicy(SystemManagerVnext& m, plc_vnext::contracts::PlcAxisSlot slot)
        : m_(&m), slot_(slot) {}

    /// 入口：不接收 target —— 目标已在独立 setAbsTarget() 写入 PLC。
    void start() {
        // LifecycleManaged：龙门生命周期已负责使能，本策略跳过 EnsuringEnabled，
        // 直接进入 PostEnableDelay（m_idleReachedTime 即时钟起点，等硬件稳定）。
        m_step = (power_ == PowerOwnership::LifecycleManaged) ? Step::PostEnableDelay
                                                              : Step::EnsuringEnabled;
        m_diag.clear();
        m_enableSent = false;
        m_moveTriggered = false;
        m_motionObserved = false;
        m_stopNormalizeSent = false;
        m_disableSent = false;
        m_posReachedSet = false;
        m_startPos = 0.f;
        m_fnValid = false;
        m_idleReachedTime = std::chrono::steady_clock::now();
        if (const auto* axis = m_->system().findBySlot(slot_)) {
            m_fn = axis->key().function;
            m_fnValid = true;
        }
        if (!m_fnValid) {
            m_step = Step::Error;
            m_diag = "slot not registered (绑定到某 AxisFunction 后才可走 SystemManagerVnext)";
        }
    }

    void tick() {
        if (m_step == Step::Done || m_step == Step::Error) return;

        // 急停/安全锁：运行中优雅终止（兜底掉电）。
        if (m_->isSystemLocked()) {
            if (m_step != Step::Initial) {
                disableMotor();
                m_diag = "safety locked, aborted";
                m_step = Step::Done;
            }
            return;
        }

        const domain_vnext::system::Axis* axis = m_->system().findBySlot(slot_);
        if (!axis) { m_step = Step::Error; m_diag = "slot not registered"; disableMotor(); return; }
        m_fn = axis->key().function;
        m_fnValid = true;

        const auto& fb = axis->feedback();
        const int16_t ms = fb.motionState;
        const float pos = fb.absPosition;
        const bool alarm = fb.alarmWord != 0;

        // 运行中报警 → Error（兜底掉电）。
        if (m_step != Step::Initial && alarm) {
            m_step = Step::Error;
            m_diag = "axis alarm";
            disableMotor();
            return;
        }

        // LifecycleManaged：运行中持续校验逻辑轴许可，失联即停（不直接掉电，
        // 掉电由生命周期策略在安全解除后负责）。
        if (power_ == PowerOwnership::LifecycleManaged && guard_) {
            const auto gr = guard_->evaluate();
            if (!gr.allowed) {
                if (m_fnValid) m_->stop(m_fn);
                m_step = Step::Error;
                m_diag = std::string("gantry permit lost: ") + gr.reason;
                return;
            }
        }

        using clock = std::chrono::steady_clock;
        const auto now = clock::now();
        auto elapsedSince = [&](clock::time_point t) {
            return std::chrono::duration<double>(now - t).count();
        };

        switch (m_step) {
        case Step::EnsuringEnabled:
            if (ms == kMotionMotorIdle) {           // 已就绪（电机使能空闲）
                m_enableSent = false;
                m_idleReachedTime = now;
                m_step = Step::PostEnableDelay;
                break;
            }
            if (!m_enableSent) {
                // ms==0：轴控+电机都未使能 → 都下发；ms==1：仅电机未使能 → 只补电机。
                bool ok = true;
                if (ms == kMotionNotEnabled) ok = appResultOk(m_->enableAxis(m_fn, true));
                if (ok) ok = appResultOk(m_->enableMotor(m_fn, true));
                if (!ok) { m_step = Step::Error; m_diag = "enable failed"; return; }
                m_enableSent = true;
                m_enableSentTime = now;
            } else if (elapsedSince(m_enableSentTime) > kEnableTimeoutSeconds) {
                m_step = Step::Error; m_diag = "enable timeout (motionState!=2)"; return;
            }
            break;

        case Step::PostEnableDelay:
            if (ms != kMotionMotorIdle) { m_step = Step::Error; m_diag = "axis left idle during enable delay"; return; }
            if (elapsedSince(m_idleReachedTime) >= kPostEnableDelaySeconds) m_step = Step::TriggeringMove;
            break;

        case Step::TriggeringMove:
            if (!m_moveTriggered) {
                if (!appResultOk(m_->triggerAbsMove(m_fn))) { m_step = Step::Error; m_diag = "trigger rejected"; return; }
                m_moveTriggered = true;
                m_triggerTime = now;
                m_startPos = pos;
                m_step = Step::WaitingMotionStart;
            }
            break;

        case Step::WaitingMotionStart:
            if (ms == kMotionAbsMove ||                       // 进入绝对定位
                std::fabs(pos - m_startPos) > kPositionEpsilon) {  // 或位移已发生
                m_motionObserved = true;
                m_finishEnterTime = now;
                m_step = Step::WaitingMotionFinish;
            } else if (elapsedSince(m_triggerTime) >= kMotionStartTimeoutSeconds) {
                // 长时间未观察到运动且位置未变：判定"运动未启动"，不冒充成功。
                m_step = Step::Error;
                m_diag = "motion never started (ms!=5, pos unchanged)";
                return;
            }
            break;

        case Step::WaitingMotionFinish:
            if (!m_motionObserved) break;
            if (ms == kMotionMotorIdle) {             // PLC 已空闲 → 无条件判成功
                onMoveCompleted();
                break;
            }
            // 兜底：state!=2 但位置已到位(±tol) 并持续 3s → 也判成功（写 OFF 收尾）。
            {
                const bool targetSet = !std::isnan(m_verifyTarget);
                const bool posAtTarget = targetSet &&
                    std::fabs(pos - m_verifyTarget) <= m_verifyTolerance;
                if (posAtTarget) {
                    if (!m_posReachedSet) { m_posReachedSet = true; m_posReachedTime = now; }
                    if (elapsedSince(m_posReachedTime) >= kPositionReachedStableSeconds) {
                        onMoveCompleted();
                    }
                } else {
                    m_posReachedSet = false;   // 位置离开到位窗口 → 重置计时
                }
            }
            break;

        case Step::PostStopDelay:
            if (elapsedSince(m_stopIdleReachedTime) >= kPostStopDelaySeconds) m_step = Step::Disabling;
            break;

        case Step::Disabling:
            disableMotor();            // 掉电 = 使能电机 OFF（见方案决策 3）
            m_step = Step::Done;
            break;

        case Step::Initial:
        default:
            break;
        }
    }

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }
    const std::string& diag() const { return m_diag; }

    static const char* stepName(Step s) {
        switch (s) {
            case Step::Initial:             return "Initial";
            case Step::EnsuringEnabled:     return "EnsuringEnabled";
            case Step::PostEnableDelay:     return "PostEnableDelay";
            case Step::TriggeringMove:      return "TriggeringMove";
            case Step::WaitingMotionStart:  return "WaitingMotionStart";
            case Step::WaitingMotionFinish: return "WaitingMotionFinish";
            case Step::PostStopDelay:       return "PostStopDelay";
            case Step::Disabling:           return "Disabling";
            case Step::Done:                return "Done";
            case Step::Error:               return "Error";
        }
        return "?";
    }

    /// 运动自然结束后是否补发 Stop* 复位领域 busy（默认开；写自复位线圈，无副作用）。
    bool sendStopAfterIdle() const { return m_sendStopAfterIdle; }
    void setSendStopAfterIdle(bool v) { m_sendStopAfterIdle = v; }

    /// 设置到位校验目标（仅用于判定"是否到达目标"，本策略**不写**目标）。
    /// 默认 NaN=不校验（仅凭 ms==2 判完成）。runAbs 会自动设置。
    void setVerifyTarget(float t) { m_verifyTarget = t; }
    /// 设置到位容差（默认 ±0.5，见 kTargetTolerance）。可按轴定位精度调整。
    void setVerifyTolerance(float t) { m_verifyTolerance = t; }

    /// 电源所有权。仅 start() 前可设（运行中不可切换）。
    void setPowerOwnership(PowerOwnership p) {
        if (m_step == Step::Initial) power_ = p;
    }
    /// 注入龙门运动许可守卫（仅 LifecycleManaged 生效）。
    void setGantryGuard(const GantryMotionGuard* g) { guard_ = g; }
    /// 标记为不可用（如逻辑轴未绑定），返回已处于 Error 的策略。
    void setUnavailable(const char* reason) { m_step = Step::Error; m_diag = reason; }

private:
    /// 运动完成收尾：复位领域 busy（Stop* 自复位，无副作用）+ 进入停稳确认窗 → 掉电。
    void onMoveCompleted() {
        if (m_sendStopAfterIdle && !m_stopNormalizeSent) {
            m_->stop(m_fn);
            m_stopNormalizeSent = true;
        }
        m_stopIdleReachedTime = std::chrono::steady_clock::now();
        m_step = Step::PostStopDelay;
    }
    /// 掉电 = 使能电机 OFF（每轮最多一次）。LifecycleManaged 下不掉电
    /// （掉电由生命周期策略在安全解除后负责），此处 no-op。
    void disableMotor() {
        if (m_disableSent) return;
        m_disableSent = true;
        if (m_fnValid && power_ != PowerOwnership::LifecycleManaged) m_->enableMotor(m_fn, false);
    }

    SystemManagerVnext* m_;
    plc_vnext::contracts::PlcAxisSlot slot_;
    Step m_step = Step::Initial;
    domain_vnext::model::AxisFunction m_fn = domain_vnext::model::AxisFunction::X;
    bool m_fnValid = false;
    std::string m_diag;

    bool m_enableSent = false;
    std::chrono::steady_clock::time_point m_enableSentTime;
    std::chrono::steady_clock::time_point m_idleReachedTime;

    bool m_moveTriggered = false;
    bool m_motionObserved = false;
    std::chrono::steady_clock::time_point m_triggerTime;
    float m_startPos = 0.f;
    /// 到位校验目标（仅判定用，不写）；NaN=不校验。见 setVerifyTarget()。
    float m_verifyTarget = std::numeric_limits<float>::quiet_NaN();
    /// 到位容差（默认 ±0.1，仅用于兜底）。见 setVerifyTolerance()。
    float m_verifyTolerance = kTargetTolerance;
    std::chrono::steady_clock::time_point m_finishEnterTime;

    // ---- 兜底到位（state!=2 但位置到位持续 3s）----
    bool m_posReachedSet = false;
    std::chrono::steady_clock::time_point m_posReachedTime;

    bool m_stopNormalizeSent = false;
    bool m_sendStopAfterIdle = true;
    std::chrono::steady_clock::time_point m_stopIdleReachedTime;

    PowerOwnership power_ = PowerOwnership::SelfManaged;
    const GantryMotionGuard* guard_ = nullptr;
    bool m_disableSent = false;
};
}  // namespace application_vnext::policy
