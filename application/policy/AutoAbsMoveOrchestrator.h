#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include <variant>
#include <string>
#include <cmath>

/**
 * @brief 绝对定位运动编排器
 *
 * 职责：编排"使能 → 下发绝对定位 → 等待运动开始 → 等待运动完成 → 掉电"的完整流程。
 *
 * 分层职责：
 *   - EnableUseCase / MoveAbsoluteUseCase 负责：分组路由 + 轴状态幂等检查 + 下发命令
 *   - Axis 领域层负责：绝对定位的语义校验 + 产生 MoveCommand
 *   - AutoAbsMoveOrchestrator 负责：流程编排 + 错误转发
 *
 * 使用示例：
 *   AutoAbsMoveOrchestrator orch(manager, "Machine_A");
 *   orch.startAbs(AxisId::Y, 100.0);
 *   while (orch.currentStep() != Step::Done && orch.currentStep() != Step::Error) {
 *       orch.tick();
 *   }
 */
class AutoAbsMoveOrchestrator {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,   // 下发使能
        IssuingMove,       // 下发绝对定位指令
        WaitingMotionStart,// 等待运动开始
        WaitingMotionFinish,// 等待运动完成
        Done,
        Error
    };

    /**
     * @param manager   系统管理器（用于 EnableUseCase / MoveAbsoluteUseCase 的分组路由）
     * @param groupName 目标分组名称
     */
    AutoAbsMoveOrchestrator(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Initial)
    {
    }

    // ========== 入口 ==========

    void startAbs(AxisId id, double target) {
        m_targetId = id;
        m_target = target;
        m_step = Step::EnsuringEnabled;
        m_lastError = std::monostate{};

        m_moveIssued = false;
        m_motionObserved = false;
        m_startPos = 0.0;

        m_traceId = TraceScope::current().traceId;

        LOG_INFO(LogLayer::APP, "AbsOrch",
                 "[" + m_groupName + "][" + axisName(m_targetId) + "] START MoveAbsolute target="
                     + std::to_string(target));
    }

    // ========== 逐帧驱动 ==========

    void tick() {
        TraceScope scope(m_groupName, axisName(m_targetId), m_traceId);

        // 第 0 层：分组解析（SystemManager）
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        // 第 1 层：轴获取与龙门校验（SystemContext）
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(m_targetId, axis, ctxReason)) {
            m_step = Step::Error;
            m_lastError = ctxReason;
            return;
        }

        // 全局最高优先级：硬件/状态错误拦截
        if (axis->state() == AxisState::Error) {
            LOG_ERROR(LogLayer::APP, "AbsOrch",
                      "[" + m_groupName + "][" + axisName(m_targetId) + "] Axis Error state — aborting");
            m_step = Step::Error;
            m_lastError = axis->lastRejection();
            return;
        }

        double pos = axis->currentAbsolutePosition();

        switch (m_step) {
        case Step::Initial:
            break;

        // ============================================================
        // EnsuringEnabled：使能 → 等待 Idle
        // ============================================================

        case Step::EnsuringEnabled:
            if (axis->state() == AxisState::Disabled) {
                auto err = EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, true);
                if (!std::holds_alternative<std::monostate>(err)) {
                    m_step = Step::Error;
                    m_lastError = err;
                    return;
                }
                LOG_DEBUG(LogLayer::APP, "AbsOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] Sent Enable");
                break;
            }
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "AbsOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] EnsuringEnabled → IssuingMove");
                m_step = Step::IssuingMove;
                break;
            }
            // 其他状态（Unknown/Moving…）：保持等待
            break;

        // ============================================================
        // IssuingMove：下发绝对定位指令 → WaitingMotionStart
        // ============================================================

        case Step::IssuingMove:
            if (m_moveIssued) break;  // 幂等保护

            {
                auto err = MoveAbsoluteUseCase{}.execute(m_manager, m_groupName, m_targetId, m_target);
                if (!std::holds_alternative<std::monostate>(err)) {
                    LOG_ERROR(LogLayer::APP, "AbsOrch",
                              "[" + m_groupName + "][" + axisName(m_targetId) + "] MoveAbsolute rejected");
                    m_lastError = err;
                    // 失败熔断 → 掉电
                    EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
                    m_step = Step::Error;
                    break;
                }

                // ⭐ 记录起点（用于后续阶段）
                m_startPos = pos;
                m_moveIssued = true;
                LOG_DEBUG(LogLayer::APP, "AbsOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] IssuingMove → WaitingMotionStart");
                m_step = Step::WaitingMotionStart;
            }
            break;

        // ============================================================
        // WaitingMotionStart：等待运动开始
        // ============================================================

        case Step::WaitingMotionStart:
            if (!m_motionObserved) {
                if (axis->state() == AxisState::MovingAbsolute ||
                    std::abs(pos - m_startPos) > m_epsilon ||
                    axis->isMoveCompleted()) {
                    m_motionObserved = true;
                    LOG_DEBUG(LogLayer::APP, "AbsOrch",
                              "[" + m_groupName + "][" + axisName(m_targetId) + "] WaitingMotionStart → WaitingMotionFinish");
                    m_step = Step::WaitingMotionFinish;
                }
            }
            break;

        // ============================================================
        // WaitingMotionFinish：等待运动完成
        // ============================================================

        case Step::WaitingMotionFinish:
            if (!m_motionObserved) break;  // 防假完成

            if (axis->isMoveCompleted()) {
                double currentPos = axis->currentAbsolutePosition();
                if (std::abs(currentPos - m_target) < m_epsilon) {
                    // 物理到位 → Done
                    EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
                    LOG_SUMMARY(LogLayer::APP, "AbsOrch",
                                "[" + m_groupName + "][" + axisName(m_targetId) + "] MoveAbsolute("
                                    + std::to_string(m_target) + ") -> SUCCESS");
                    m_step = Step::Done;
                } else {
                    // 物理没到位 → Error
                    EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
                    m_lastError = RejectionReason::InvalidState;
                    LOG_SUMMARY(LogLayer::APP, "AbsOrch",
                                "[" + m_groupName + "][" + axisName(m_targetId) + "] MoveAbsolute("
                                    + std::to_string(m_target) + ") -> ABORTED (Target not reached)");
                    m_step = Step::Error;
                }
            }
            break;

        case Step::Done:
        case Step::Error:
        default:
            break;
        }
    }

    // ========== 状态查询 ==========

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }

    /**
     * @brief 获取最后一次错误
     * @return UseCaseError variant，包含 ContextRejection / RejectionReason
     */
    UseCaseError lastError() const { return m_lastError; }

    /// @brief 获取错误码（保持向后兼容的 RejectionReason 提取）
    RejectionReason errorReason() const {
        if (std::holds_alternative<RejectionReason>(m_lastError)) {
            return std::get<RejectionReason>(m_lastError);
        }
        return RejectionReason::None;
    }

private:
    static std::string axisName(AxisId id) {
        switch (id) {
            case AxisId::Y:  return "Y";
            case AxisId::Z:  return "Z";
            case AxisId::R:  return "R";
            case AxisId::X:  return "X";
            case AxisId::X1: return "X1";
            case AxisId::X2: return "X2";
        }
        return "?";
    }

    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step;
    AxisId m_targetId = AxisId::Y;
    double m_target = 0.0;
    UseCaseError m_lastError = std::monostate{};

    // IssuingMove
    bool m_moveIssued = false;
    double m_startPos = 0.0;

    // WaitingMotionStart
    bool m_motionObserved = false;
    const double m_epsilon = 0.01;

    std::string m_traceId = "N/A";
};
