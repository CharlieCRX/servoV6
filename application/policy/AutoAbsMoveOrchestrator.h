#include "../axis/EnableUseCase.h"
#include "../axis/MoveAbsoluteUseCase.h"

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
            if (moveUc.execute(axis, m_target) == RejectionReason::None) {
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
            // ⭐ 修正点：必须没有 pendingCommand
            if (axis.state() == AxisState::Idle) {
            
                if (!m_motionObserved) {
                    break;
                }
            
                if (std::abs(pos - m_target) > tolerance) {
                    m_step = Step::Error; // ❗未到位
                    break;
                }
            
                enableUc.execute(axis, false);
                m_step = Step::Done;
            }
            break;

        default:
            break;
        }
    }

    Step currentStep() const { return m_step; }

private:
    EnableUseCase& enableUc;
    MoveAbsoluteUseCase& moveUc;

    Step m_step = Step::Initial;
    double m_target = 0.0;

    double startPos = 0.0;
    bool m_motionObserved = false;

    const double epsilon = 0.01;
    const double tolerance = 0.01;
};