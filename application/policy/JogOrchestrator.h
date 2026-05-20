#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include <variant>
#include <string>

/**
 * @brief 点动运动编排器
 *
 * 职责：编排"使能 -> 下发点动 -> 运行 -> 停止 -> 等待空闲 -> 掉电"的完整流程。
 *
 * 分层职责：
 *   - EnableUseCase 负责：分组路由 + 轴状态幂等检查 + 下发使能/掉电命令
 *   - Axis 领域层负责：点动/停止的语义校验 + 产生 JogCommand
 *   - JogOrchestrator 负责：流程编排 + 错误转发
 *
 * 使用示例：
 *   JogOrchestrator orch(manager, "Machine_A");
 *   orch.startJog(AxisId::Y, Direction::Forward);
 *   while (orch.currentStep() != Step::Done && orch.currentStep() != Step::Error) {
 *       orch.tick();
 *   }
 */
class JogOrchestrator {
public:
    enum class Step {
        Idle,
        EnsuringEnabled,   // 下发使能
        IssuingJog,        // 下发点动指令
        Jogging,           // 点动运行中
        IssuingStop,       // 下发停止指令
        WaitingForIdle,    // 等待轴停稳
        EnsuringDisabled,  // 下发掉电
        Done,
        Error
    };

    /**
     * @param manager   系统管理器（用于 EnableUseCase 的分组路由）
     * @param groupName 目标分组名称
     */
    JogOrchestrator(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Idle)
    {
    }

    // ========== 入口 ==========

    void startJog(AxisId id, Direction dir) {
        m_targetId = id;
        m_dir = dir;
        m_step = Step::EnsuringEnabled;
        m_lastError = std::monostate{};

        m_jogIssued = false;
        m_stopIssued = false;
        m_disableIssued = false;

        m_traceId = TraceScope::current().traceId;

        LOG_INFO(LogLayer::APP, "JogOrch",
                 "[" + m_groupName + "][" + axisName(m_targetId) + "] START Jog "
                     + dirName(dir));
    }

    /// @brief 停止点动（带 AxisId + Direction 双重防误杀）
    void stopJog(AxisId id, Direction dir) {
        if (id != m_targetId || dir != m_dir) {
            LOG_WARN(LogLayer::APP, "JogOrch",
                     "[" + m_groupName + "][" + axisName(m_targetId) + "] "
                     "StopJog ignored -- mismatched AxisId/Direction (got "
                         + axisName(id) + "/" + dirName(dir) + ")");
            return;
        }

        if (m_step == Step::EnsuringEnabled ||
            m_step == Step::IssuingJog ||
            m_step == Step::Jogging) {
            LOG_INFO(LogLayer::APP, "JogOrch",
                     "[" + m_groupName + "][" + axisName(m_targetId) + "] Stop requested by UI");
            m_step = Step::IssuingStop;
        }
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

        // ★ Safety Lock Pre-check（Layer 0 前置于 tryGetAxis）
        //     安全锁定时，无情中止当前编排流程，回到 Done，
        //     避免进入 Error 状态导致急停解除后无法启动新 Jog。
        if (group->emergencyStopController().isSystemLocked()) {
            if (m_step != Step::Idle && m_step != Step::Done && m_step != Step::Error) {
                LOG_INFO(LogLayer::APP, "JogOrch",
                         "[" + m_groupName + "][" + axisName(m_targetId) + "] Safety locked -- aborting gracefully");
                m_step = Step::Done;
                m_lastError = std::monostate{};
            }
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
            LOG_ERROR(LogLayer::APP, "JogOrch",
                      "[" + m_groupName + "][" + axisName(m_targetId) + "] Axis Error state -- aborting");
            m_step = Step::Error;
            m_lastError = axis->lastRejection();
            return;
        }

        switch (m_step) {
        case Step::Idle:
            break;

        // ============================================================
        // EnsuringEnabled：使能 -> 等待 Idle
        // ============================================================

        case Step::EnsuringEnabled:
            if (axis->state() == AxisState::Disabled) {
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] EnsuringEnabled -- sending Enable command");
                // EnableUseCase 内部已做分组路由 + 领域幂等检查
                auto err = EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, true);
                if (!std::holds_alternative<std::monostate>(err)) {
                    m_step = Step::Error;
                    m_lastError = err;
                    return;
                }
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] Sent Enable");
                break;
            }
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] EnsuringEnabled -> IssuingJog");
                m_step = Step::IssuingJog;
                break;
            }
            // 其他状态（Unknown/Moving...）：保持等待
            break;

        // ============================================================
        // IssuingJog：下发点动指令 -> Jogging
        // ============================================================

        case Step::IssuingJog:
            if (m_jogIssued) break;  // 幂等保护

            LOG_DEBUG(LogLayer::APP, "JogOrch",
                      "[" + m_groupName + "][" + axisName(m_targetId) + "] IssuingJog -- sending Jog command " + dirName(m_dir));

            // 调用领域层语义检查
            if (!axis->jog(m_dir)) {
                LOG_ERROR(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] Jog rejected: "
                              + std::to_string(static_cast<int>(axis->lastRejection())));
                m_lastError = axis->lastRejection();
                // 失败熔断 -> 掉电
                EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
                m_step = Step::Error;
                break;
            }

            // 领域层通过 -> 下发 JogCommand 到驱动
            if (axis->hasPendingCommand()) {
                if (auto* drv = group->driver()) {
                    auto commResult = drv->send(AxisCommandWithId{m_targetId, axis->getPendingCommand()});
                    if (!commResult.ok()) {
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
            }

            m_jogIssued = true;
            LOG_DEBUG(LogLayer::APP, "JogOrch",
                      "[" + m_groupName + "][" + axisName(m_targetId) + "] IssuingJog -> Jogging");
            m_step = Step::Jogging;
            break;

        // ============================================================
        // Jogging：运行中监视
        // ============================================================

        case Step::Jogging:
            // 异常跌落检测：轴意外回到 Idle 且无待处理指令
            if (axis->state() == AxisState::Idle && !axis->hasPendingCommand()) {
                LOG_ERROR(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] "
                          "Axis unexpectedly Idle during Jog -- forcing stop sequence");
                m_step = Step::IssuingStop;
            }
            break;

        // ============================================================
        // IssuingStop：下发停止 -> WaitingForIdle
        // ============================================================

        case Step::IssuingStop:
            if (!m_stopIssued) {
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] IssuingStop -- sending Stop command");
                if (axis->stopJog(m_dir)) {
                    if (axis->hasPendingCommand()) {
                        if (auto* drv = group->driver()) {
                            auto commResult = drv->send(AxisCommandWithId{m_targetId, axis->getPendingCommand()});
                            if (!commResult.ok()) {
                                m_step = Step::Error;
                                m_lastError = commResult;
                                return;
                            }
                        }
                    }
                }
                m_stopIssued = true;
            }
            LOG_DEBUG(LogLayer::APP, "JogOrch",
                      "[" + m_groupName + "][" + axisName(m_targetId) + "] IssuingStop -> WaitingForIdle");
            m_step = Step::WaitingForIdle;
            break;

        // ============================================================
        // WaitingForIdle：等待轴停稳
        // ============================================================

        case Step::WaitingForIdle:
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] WaitingForIdle -> EnsuringDisabled");
                m_step = Step::EnsuringDisabled;
            }
            break;

        // ============================================================
        // EnsuringDisabled：掉电 -> Done
        // ============================================================

        case Step::EnsuringDisabled:
            if (!m_disableIssued) {
                LOG_DEBUG(LogLayer::APP, "JogOrch",
                          "[" + m_groupName + "][" + axisName(m_targetId) + "] EnsuringDisabled -- sending Disable command");
                auto err = EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
                if (!std::holds_alternative<std::monostate>(err)) {
                    m_step = Step::Error;
                    m_lastError = err;
                    return;
                }
                m_disableIssued = true;
            }
            if (axis->state() == AxisState::Disabled) {
                LOG_SUMMARY(LogLayer::APP, "JogOrch",
                            "[" + m_groupName + "][" + axisName(m_targetId) + "] Jog -> SUCCESS (Safely Stopped)");
                m_step = Step::Done;
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
    /// @note 仅当错误来自 Axis 领域层时有效；SystemManager/SystemContext 层错误返回 None
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

    static std::string dirName(Direction dir) {
        return dir == Direction::Forward ? "Forward(+)" : "Backward(-)";
    }

    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step;
    AxisId m_targetId = AxisId::Y;
    Direction m_dir = Direction::Forward;
    UseCaseError m_lastError = std::monostate{};

    bool m_jogIssued = false;
    bool m_stopIssued = false;
    bool m_disableIssued = false;

    std::string m_traceId = "N/A";
};
