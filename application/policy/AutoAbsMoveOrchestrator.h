#pragma once
#include "axis/EnableUseCase.h"
#include "axis/MoveAbsoluteUseCase.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>

class AutoAbsMoveOrchestrator {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,
        IssuingMove,
        WaitingMotionStart,
        WaitingMotionFinish,
        Done,
        Error
    };

    AutoAbsMoveOrchestrator(EnableUseCase& en, MoveAbsoluteUseCase& mv)
        : enableUc(en), moveUc(mv) {}

    void start(double target) {
        m_target = target;
        m_step = Step::EnsuringEnabled;
        m_motionObserved = false;

        m_traceId = TraceScope::current().traceId;
        LOG_INFO(LogLayer::APP, "AbsOrch", "START MoveAbsolute target=" + std::to_string(target));
    }

    void update(Axis& axis)
    {
        // ⭐ 全局错误拦截（最高优先级）
        if (axis.state() == AxisState::Error) {
            m_step = Step::Error;
            return;
        }
        double pos = axis.currentAbsolutePosition();

        TraceScope scope("G1", "Y", m_traceId);
        switch (m_step)
        {
        case Step::EnsuringEnabled:
        
            if (axis.state() == AxisState::Disabled) {
                enableUc.execute(axis, true);   // ⭐ 主动发 Enable
                break;
            }
        
            if (axis.state() == AxisState::Idle) {
                m_step = Step::IssuingMove;
                LOG_DEBUG(LogLayer::APP, "AbsOrch", "Step: EnsuringEnabled -> IssuingMove");
                break;
            }
        
            break;

        case Step::IssuingMove:
            m_rejectionReason = moveUc.execute(axis, m_target);
            if (m_rejectionReason == RejectionReason::None) {
                startPos = axis.currentAbsolutePosition();
                // 🌟 增加状态流转日志
                LOG_DEBUG(LogLayer::APP, "AbsOrch", "Step: IssuingMove -> WaitingMotionStart");
                m_step = Step::WaitingMotionStart;
            } else {
                // 🌟 增加动作失败时的错误拦截日志
                LOG_ERROR(LogLayer::APP, "AbsOrch", "Move command rejected by UseCase/Domain");
                enableUc.execute(axis, false);
                m_step = Step::Error;
            }
            break;

        case Step::WaitingMotionStart:
            if (axis.state() == AxisState::MovingAbsolute ||
                std::abs(pos - startPos) > epsilon ||
                axis.isMoveCompleted()) {

                m_motionObserved = true;
                // 🌟 增加状态流转日志
                LOG_DEBUG(LogLayer::APP, "AbsOrch", "Step: WaitingMotionStart -> WaitingMotionFinish");
                m_step = Step::WaitingMotionFinish;
            }
            break;

        case Step::WaitingMotionFinish:

            if (!m_motionObserved) {
                break;
            }
        
            if (axis.isMoveCompleted()) {
                // 物理级终极验证：意图虽然消失了，但我真的到了吗？
                double currentPos = axis.currentAbsolutePosition();
                if (std::abs(currentPos - m_target) < epsilon) {
                    // 只有物理到位，才是真正的 Done
                    enableUc.execute(axis, false);
                    LOG_SUMMARY(LogLayer::APP, "AbsOrch", "MoveAbsolute(" + std::to_string(m_target) + ") -> SUCCESS");
                    m_step = Step::Done;
                } else {
                    // 意图消失了但物理没到位 -> 说明是被半路截杀了（如急停）
                    enableUc.execute(axis, false);
                    m_rejectionReason = RejectionReason::InvalidState; // 记录为非法中止
                    LOG_SUMMARY(LogLayer::APP, "AbsOrch", "MoveAbsolute(" + std::to_string(m_target) + ") -> ABORTED (Target not reached)");
                    m_step = Step::Error; 
                }
            }
            break;

        default:
            break;
        }
    }

    Step currentStep() const { return m_step; }
    RejectionReason errorReason() const { return m_rejectionReason; } // 新增接口

private:
    EnableUseCase& enableUc;
    MoveAbsoluteUseCase& moveUc;

    Step m_step = Step::Initial;
    double m_target = 0.0;

    double startPos = 0.0;
    bool m_motionObserved = false;

    const double epsilon = 0.01;


    RejectionReason m_rejectionReason = RejectionReason::None;

    std::string m_traceId = "N/A";
};