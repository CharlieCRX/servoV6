#ifndef AUTO_REL_MOVE_ORCHESTRATOR_H
#define AUTO_REL_MOVE_ORCHESTRATOR_H

#include "../axis/EnableUseCase.h"
#include "../axis/MoveRelativeUseCase.h"

class AutoRelMoveOrchestrator {
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

    AutoRelMoveOrchestrator(EnableUseCase& en, MoveRelativeUseCase& mv)
        : enableUc(en), moveUc(mv) {}

    void start(double distance) {
        m_distance = distance;
        m_step = Step::EnsuringEnabled;

        m_moveIssued = false;       // ⭐ 重置幂等标志
        m_motionObserved = false;   // ⭐ 重置运动观测
        m_startPos = 0.0;           // ⭐ 重置起点
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

            // 1. Disabled → 主动 Enable
            if (axis.state() == AxisState::Disabled) {
                enableUc.execute(axis, true);
                break;
            }

            // 2. Idle → 进入下一阶段（但不发 Move）
            if (axis.state() == AxisState::Idle) {
                m_step = Step::IssuingMove;
                break;
            }

            // 3. 其他状态：保持
            break;

        case Step::IssuingMove:

            // ❗幂等：只允许发一次 Move
            if (m_moveIssued) {
                break;
            }

            // ⭐ 发起相对运动
            m_rejectionReason = moveUc.execute(axis, m_distance);
            if (m_rejectionReason == RejectionReason::None) {
                // ⭐ 记录起点（用于后续阶段）
                m_startPos = axis.currentAbsolutePosition();

                m_moveIssued = true;
                m_step = Step::WaitingMotionStart;
            }
            else {
                // ⭐ Move 被拒绝 → 立即掉电 + Error
                enableUc.execute(axis, false);
                m_step = Step::Error;
            }

            break;
        
        case Step::WaitingMotionStart:
          // ⭐ 只在尚未观测到运动时检测
          if (!m_motionObserved) {
              // ⭐ 判定条件：MovingRelative 或 位置变化
              if (axis.state() == AxisState::MovingRelative ||
                  std::abs(pos - m_startPos) > m_epsilon) {
                  m_motionObserved = true;
                  m_step = Step::WaitingMotionFinish;
              }
          }
          break;
        case Step::WaitingMotionFinish:

          // ❗防假完成
          if (!m_motionObserved) {
              break;
          }
        
          // ❗唯一完成判定
          if (axis.isMoveCompleted()) {
              enableUc.execute(axis, false);
              m_step = Step::Done;
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
    MoveRelativeUseCase& moveUc;

    Step m_step = Step::Initial;
    double m_distance = 0.0;

    // IssuingMove
    bool m_moveIssued = false;
    double m_startPos = 0.0;

    // WaitingMotionStart
    bool m_motionObserved = false;
    const double m_epsilon = 0.01;

    RejectionReason m_rejectionReason = RejectionReason::None;
};

#endif // AUTO_REL_MOVE_ORCHESTRATOR_H