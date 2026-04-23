#include "axis/EnableUseCase.h"
#include "axis/MoveAbsoluteUseCase.h"
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
    }

    void update(Axis& axis)
    {
        // ⭐ 全局错误拦截（最高优先级）
        if (axis.state() == AxisState::Error) {
            m_step = Step::Error;
            return;
        }
        double pos = axis.currentAbsolutePosition();

        switch (m_step)
        {
        case Step::EnsuringEnabled:
        
            if (axis.state() == AxisState::Disabled) {
                enableUc.execute(axis, true);   // ⭐ 主动发 Enable
                break;
            }
        
            if (axis.state() == AxisState::Idle) {
                m_step = Step::IssuingMove;
                break;
            }
        
            break;

        case Step::IssuingMove:
            m_rejectionReason = moveUc.execute(axis, m_target);
            if (m_rejectionReason == RejectionReason::None) {
                startPos = axis.currentAbsolutePosition();
                m_step = Step::WaitingMotionStart;
            } else {
                enableUc.execute(axis, false);
                m_step = Step::Error;
            }
            break;

        case Step::WaitingMotionStart:

            // ⭐ 修正点：用“位置变化 OR Moving”
            if (axis.state() == AxisState::MovingAbsolute ||
                std::abs(pos - startPos) > epsilon) {

                m_motionObserved = true;
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
                    m_step = Step::Done;
                } else {
                    // 意图消失了但物理没到位 -> 说明是被半路截杀了（如急停）
                    enableUc.execute(axis, false);
                    m_rejectionReason = RejectionReason::InvalidState; // 记录为非法中止
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
};