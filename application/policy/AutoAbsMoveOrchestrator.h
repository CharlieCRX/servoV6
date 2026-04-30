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

    void start(AxisId id, double target) {
        m_targetId = id;
        m_target = target;
        m_step = Step::EnsuringEnabled;
        m_motionObserved = false;

        m_traceId = TraceScope::current().traceId;
        LOG_INFO(LogLayer::APP, "AbsOrch", "START MoveAbsolute target=" + std::to_string(target));
    }

    void update(Axis& axis)
    {
        TraceScope scope("G1", "Y", m_traceId);
        // ⭐ 全局错误拦截（最高优先级）
        if (axis.state() == AxisState::Error) {
            LOG_ERROR(LogLayer::APP, "AbsOrch", "Axis entered Error state globally!");
            m_step = Step::Error;
            return;
        }
        double pos = axis.currentAbsolutePosition();

        switch (m_step)
        {
        case Step::EnsuringEnabled:
        
            if (axis.state() == AxisState::Disabled) {
                enableUc.execute(m_targetId, true);   // ⭐ AxisId 寻址
                break;
            }
        
            if (axis.state() == AxisState::Idle) {
                m_step = Step::IssuingMove;
                LOG_DEBUG(LogLayer::APP, "AbsOrch", "Step: EnsuringEnabled -> IssuingMove");
                break;
            }
        
            break;

        case Step::IssuingMove:
            m_rejectionReason = moveUc.execute(m_targetId, m_target);  // ⭐ AxisId 寻址
            if (m_rejectionReason == RejectionReason::None) {
                startPos = axis.currentAbsolutePosition();
                LOG_DEBUG(LogLayer::APP, "AbsOrch", "Step: IssuingMove -> WaitingMotionStart");
                m_step = Step::WaitingMotionStart;
            } else {
                LOG_ERROR(LogLayer::APP, "AbsOrch", "Move command rejected by UseCase/Domain");
                enableUc.execute(m_targetId, false);  // ⭐ AxisId 寻址
                m_step = Step::Error;
            }
            break;

        case Step::WaitingMotionStart:
            if (axis.state() == AxisState::MovingAbsolute ||
                std::abs(pos - startPos) > epsilon ||
                axis.isMoveCompleted()) {

                m_motionObserved = true;
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
                    enableUc.execute(m_targetId, false);
                    LOG_SUMMARY(LogLayer::APP, "AbsOrch", "MoveAbsolute(" + std::to_string(m_target) + ") -> SUCCESS");
                    m_step = Step::Done;
                } else {
                    // 意图消失了但物理没到位 -> 说明是被半路截杀了（如急停）
                    enableUc.execute(m_targetId, false);
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
    RejectionReason errorReason() const { return m_rejectionReason; }

private:
    EnableUseCase& enableUc;
    MoveAbsoluteUseCase& moveUc;

    Step m_step = Step::Initial;
    AxisId m_targetId = AxisId::Y;   // ⭐ 新增：目标轴标识
    double m_target = 0.0;

    double startPos = 0.0;
    bool m_motionObserved = false;

    const double epsilon = 0.01;

    RejectionReason m_rejectionReason = RejectionReason::None;

    std::string m_traceId = "N/A";
};
