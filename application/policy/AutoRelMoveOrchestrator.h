#ifndef AUTO_REL_MOVE_ORCHESTRATOR_H
#define AUTO_REL_MOVE_ORCHESTRATOR_H

#include "application/axis/EnableUseCase.h"
#include "application/axis/MoveRelativeUseCase.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include <cmath>

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

    // ⭐ 升级 1：接收目标轴标识 AxisId
    void start(AxisId id, double distance) {
        m_targetId = id;            // 记录目标轴
        m_distance = distance;
        m_step = Step::EnsuringEnabled;

        m_moveIssued = false;       // 重置幂等标志
        m_motionObserved = false;   // 重置运动观测
        m_startPos = 0.0;           // 重置起点

        m_traceId = TraceScope::current().traceId;
        LOG_INFO(LogLayer::APP, "RelOrch", "START MoveRelative distance=" + std::to_string(distance));
    }

    void update(Axis& axis)
    {
        TraceScope scope("G1", "Y", m_traceId);
        // ⭐ 全局错误拦截（最高优先级）
        if (axis.state() == AxisState::Error) {
            LOG_ERROR(LogLayer::APP, "RelOrch", "Axis entered Error state globally!");
            m_step = Step::Error;
            return;
        }

        double pos = axis.currentAbsolutePosition();
        
        switch (m_step)
        {
        case Step::EnsuringEnabled:

            // 1. Disabled → 主动 Enable
            if (axis.state() == AxisState::Disabled) {
                enableUc.execute(m_targetId, true); // ⭐ 升级 2：按 ID 路由
                break;
            }

            // 2. Idle → 进入下一阶段（但不发 Move）
            if (axis.state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "RelOrch", "Step: EnsuringEnabled -> IssuingMove");
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

            // ⭐ 发起相对运动 (按 ID 路由)
            m_rejectionReason = moveUc.execute(m_targetId, m_distance);
            if (m_rejectionReason == RejectionReason::None) {
                // ⭐ 记录起点（用于后续阶段）
                m_startPos = axis.currentAbsolutePosition();
                LOG_DEBUG(LogLayer::APP, "RelOrch", "Step: IssuingMove -> WaitingMotionStart");

                m_moveIssued = true;
                m_step = Step::WaitingMotionStart;
            }
            else {
                // ⭐ Move 被拒绝 → 立即掉电 + Error
                LOG_ERROR(LogLayer::APP, "RelOrch", "Move command rejected by UseCase/Domain");
                enableUc.execute(m_targetId, false); // ⭐ 升级 2：按 ID 路由
                m_step = Step::Error;
            }

            break;
        
        case Step::WaitingMotionStart:
          // ⭐ 只在尚未观测到运动时检测
          if (!m_motionObserved) {
              // ⭐ 判定条件：MovingRelative 或 位置变化
              if (axis.state() == AxisState::MovingRelative ||
                  std::abs(pos - m_startPos) > m_epsilon ||
                  axis.isMoveCompleted()) {
                  m_motionObserved = true;
                  LOG_DEBUG(LogLayer::APP, "RelOrch", "Step: WaitingMotionStart -> WaitingMotionFinish");
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
                // 物理级终极验证：意图虽然消失了，但我真的到了吗？
                double currentPos = axis.currentAbsolutePosition();
                if (std::abs(currentPos - (m_startPos + m_distance)) < m_epsilon) {
                    // 只有物理到位，才是真正的 Done
                    enableUc.execute(m_targetId, false); // ⭐ 升级 2：按 ID 路由
                    LOG_SUMMARY(LogLayer::APP, "RelOrch", "MoveRelative(" + std::to_string(m_distance) + ") -> SUCCESS");
                    m_step = Step::Done;
                } else {
                    // 物理没到位，说明可能被外力打断了，视为失败
                    enableUc.execute(m_targetId, false); // ⭐ 升级 2：按 ID 路由
                    m_rejectionReason = RejectionReason::InvalidState; 
                    LOG_SUMMARY(LogLayer::APP, "RelOrch", "MoveRelative(" + std::to_string(m_distance) + ") -> ABORTED (Target not reached)");
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
    MoveRelativeUseCase& moveUc;

    Step m_step = Step::Initial;
    AxisId m_targetId = AxisId::Y;   // ⭐ 记录目标轴
    double m_distance = 0.0;

    // IssuingMove
    bool m_moveIssued = false;
    double m_startPos = 0.0;

    // WaitingMotionStart
    bool m_motionObserved = false;
    const double m_epsilon = 0.01;

    RejectionReason m_rejectionReason = RejectionReason::None;

    std::string m_traceId = "N/A";
};

#endif // AUTO_REL_MOVE_ORCHESTRATOR_H