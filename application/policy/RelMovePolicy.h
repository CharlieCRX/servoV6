#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/TriggerRelMoveUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include <variant>
#include <string>
#include <cmath>
#include <chrono>

/**
 * @brief 相对定位触发策略（Policy 层编排）
 *
 * 职责：编排"使能 → 触发(REL_MOVE_TRIGGER) → 等待运动开始
 *       → 等待运动结束 → 关闭使能"的完整流程。
 *
 * ★ 不包含距离写入 — 距离已在 setRelTarget() 的独立路径中写入 PLC。
 *
 * 分层职责：
 *   - EnableUseCase         负责：使能/关闭使能
 *   - TriggerRelMoveUseCase 负责：仅触发 REL_MOVE_TRIGGER M 寄存器
 *   - AxisViewModelCore::setRelTarget 负责：独立写 REL_TARGET D 寄存器
 *     （★ 不在本 Policy 内调用，由 ViewModel 直调 Domain + consumePendingCommands 消费）
 *   - Axis 领域层           负责：状态校验 + 限位校验
 *   - RelMovePolicy         负责：触发流程编排 + 错误转发
 *
 * 与 AbsMovePolicy 的对称差异：
 *   - startRel(id) 不传 distance
 *   - 触发使用 TriggerRelMoveUseCase
 *   - 运动状态判定使用 AxisState::MovingRelative
 *   - 日志标识使用 "RelPolicy"
 *
 * 状态机流转：
 *   Initial → EnsuringEnabled → TriggeringMove → WaitingMotionStart
 *          → WaitingMotionFinish → Disabling → Done
 *          Error ←────────── Error ←──────── Error ←── Error
 *
 * 使用示例：
 *   // 1. 先独立设置距离（直接调用，不走 Policy）
 *   AxisViewModelCore::setRelTarget(50.0);
 *
 *   // 2. 然后触发运动（走 Policy 编排）
 *   RelMovePolicy policy(manager, "Machine_A");
 *   policy.startRel(AxisId::Y);  // ★ 不传 distance
 *   while (policy.currentStep() != Step::Done && policy.currentStep() != Step::Error) {
 *       policy.tick();
 *   }
 */
class RelMovePolicy {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,     // 使能
        TriggeringMove,      // 触发 REL_MOVE_TRIGGER（★ 无 SettingTarget）
        WaitingMotionStart,  // 等待运动开始
        WaitingMotionFinish, // 等待运动完成（只看轴状态：Moving→等待, Idle→完成，不判定位置）
        Disabling,           // 关闭使能
        Done,
        Error
    };

    RelMovePolicy(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Initial)
        , m_enableTimeoutSeconds(2.0)   // ★ 使能超时：2 秒后未收到 Idle feedback 则判定失败
    {}

    // ========== 入口 ==========

    /// @brief 启动相对移动触发流程
    /// @param id 目标轴 ID
    /// ★ 不接收 distance 参数 —— 距离已在独立 setRelTarget() 路径中写入 PLC
    void startRel(AxisId id) {
        m_targetId = id;
        m_step = Step::EnsuringEnabled;
        m_lastError = std::monostate{};

        m_moveTriggered = false;
        m_motionObserved = false;

        // ★ 使能防重复 & 超时相关标志复位
        m_enableSent       = false;
        m_enableSentTime   = std::chrono::steady_clock::time_point{};

        m_traceId = TraceScope::current().traceId;

        LOG_INFO(LogLayer::APP, "RelPolicy",
                 "[" + m_groupName + "][" + axisName(m_targetId)
                     + "] START triggerRelMove (distance already in PLC)");
    }

    // ========== 逐帧驱动 ==========

    void tick() {
        TraceScope scope(m_groupName, axisName(m_targetId), m_traceId);

        // Layer 0：分组解析
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        // 急停安全锁检查
        if (group->emergencyStopController().isSystemLocked()) {
            if (m_step != Step::Initial && m_step != Step::Done && m_step != Step::Error) {
                LOG_INFO(LogLayer::APP, "RelPolicy",
                         "[" + m_groupName + "][" + axisName(m_targetId)
                             + "] Safety locked -- aborting gracefully");
                m_step = Step::Done;
                m_lastError = std::monostate{};
            }
            return;
        }

        // Layer 1：轴获取
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(m_targetId, axis, ctxReason)) {
            m_step = Step::Error;
            m_lastError = ctxReason;
            return;
        }

        if (axis->state() == AxisState::Error) {
            LOG_ERROR(LogLayer::APP, "RelPolicy",
                      "[" + m_groupName + "][" + axisName(m_targetId)
                          + "] Axis Error state -- aborting");
            m_step = Step::Error;
            m_lastError = axis->lastRejection();
            return;
        }

        double pos = axis->currentAbsolutePosition();

        switch (m_step) {
        case Step::Initial:
            break;

        // ============================================================
        // Step 1：EnsuringEnabled —— 先使能
        // ============================================================
        case Step::EnsuringEnabled:
            if (axis->state() == AxisState::Disabled) {
                if (!m_enableSent) {
                    LOG_DEBUG(LogLayer::APP, "RelPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- sending Enable (first time)");
                    auto err = EnableUseCase{}.execute(
                        m_manager, m_groupName, m_targetId, true);
                    if (!std::holds_alternative<std::monostate>(err)) {
                        LOG_ERROR(LogLayer::APP, "RelPolicy",
                                  "[" + m_groupName + "][" + axisName(m_targetId)
                                      + "] EnsuringEnabled -- EnableUseCase FAILED");
                        m_step = Step::Error;
                        m_lastError = err;
                        break;
                    }
                    m_enableSent       = true;
                    m_enableSentTime   = std::chrono::steady_clock::now();
                    LOG_DEBUG(LogLayer::APP, "RelPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- Enable sent, waiting for Idle feedback...");
                } else {
                    auto elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - m_enableSentTime).count();
                    if (elapsed > m_enableTimeoutSeconds) {
                        LOG_ERROR(LogLayer::APP, "RelPolicy",
                                  "[" + m_groupName + "][" + axisName(m_targetId)
                                      + "] EnsuringEnabled -- TIMEOUT after "
                                      + std::to_string(m_enableTimeoutSeconds)
                                      + "s, still not Idle. Aborting.");
                        m_step = Step::Error;
                        m_lastError = ErrTimeout{"EnsuringEnabled", m_enableTimeoutSeconds};
                        break;
                    }
                    LOG_TRACE(LogLayer::APP, "RelPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- waiting for Idle feedback... (enable already sent, "
                                  + std::to_string(elapsed) + "s elapsed)");
                }
                break;
            }
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "RelPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] EnsuringEnabled -> TriggeringMove  ★ 直接触发，不写距离");
                m_enableSent = false;   // ★ 清除标志，完成使能阶段
                m_step = Step::TriggeringMove;
                break;
            }
            break;

        // ============================================================
        // Step 2：TriggeringMove —— 触发 REL_MOVE_TRIGGER（★ 无 SettingTarget）
        // ============================================================
        case Step::TriggeringMove:
            if (m_moveTriggered) break;
            {
                LOG_DEBUG(LogLayer::APP, "RelPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] TriggeringMove -- sending REL_MOVE_TRIGGER");
                auto err = TriggerRelMoveUseCase{}.execute(
                    m_manager, m_groupName, m_targetId);
                if (!std::holds_alternative<std::monostate>(err)) {
                    LOG_ERROR(LogLayer::APP, "RelPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] TriggerRelMove rejected");
                    EnableUseCase{}.execute(
                        m_manager, m_groupName, m_targetId, false);
                    m_step = Step::Error;
                    m_lastError = err;
                    break;
                }
                m_moveTriggered = true;
                m_startPos = pos;
                LOG_DEBUG(LogLayer::APP, "RelPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] TriggeringMove -> WaitingMotionStart");
                m_step = Step::WaitingMotionStart;
            }
            break;

        // ============================================================
        // Step 3：WaitingMotionStart —— 等待运动开始
        // ============================================================
        case Step::WaitingMotionStart:
            if (!m_motionObserved) {
                if (axis->state() == AxisState::MovingRelative ||
                    std::abs(pos - m_startPos) > m_epsilon) {
                    m_motionObserved = true;
                    LOG_DEBUG(LogLayer::APP, "RelPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] WaitingMotionStart -> WaitingMotionFinish");
                    m_step = Step::WaitingMotionFinish;
                }
            }
            break;

        // ============================================================
        // Step 4：WaitingMotionFinish —— 等待运动完成
        // ★ 使用轴状态判定而非 isMoveCompleted()：
        //   - MovingAbsolute / MovingRelative → 仍在运动中，继续等待
        //   - Idle → 运动完成，推进到 Disabling
        //   不使用 isMoveCompleted() 的原因：
        //   applyFeedback 的闭环逻辑在状态变为 Moving 时会立即消费
        //   TriggerRelMoveCommand，导致 isMoveCompleted() 过早返回 true。
        // ============================================================
        case Step::WaitingMotionFinish:
            if (!m_motionObserved) break;

            if (axis->state() == AxisState::MovingAbsolute ||
                axis->state() == AxisState::MovingRelative) {
                break;
            }
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "RelPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] WaitingMotionFinish -- move completed -> Disabling");
                m_step = Step::Disabling;
            }
            break;

        // ============================================================
        // Step 5：Disabling —— 关闭使能
        // ============================================================
        case Step::Disabling:
            {
                LOG_DEBUG(LogLayer::APP, "RelPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] Disabling -- sending Disable");
                EnableUseCase{}.execute(
                    m_manager, m_groupName, m_targetId, false);
                LOG_SUMMARY(LogLayer::APP, "RelPolicy",
                            "[" + m_groupName + "][" + axisName(m_targetId)
                                + "] triggerRelMove -> SUCCESS");
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
    UseCaseError lastError() const { return m_lastError; }

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
    Step m_step = Step::Initial;
    AxisId m_targetId = AxisId::Y;
    UseCaseError m_lastError = std::monostate{};

    bool m_moveTriggered = false;
    double m_startPos = 0.0;

    bool m_motionObserved = false;
    const double m_epsilon = 0.01;

    // ========== ★ 使能防重复发送 + 超时 ==========
    bool m_enableSent = false;
    std::chrono::steady_clock::time_point m_enableSentTime;
    const double m_enableTimeoutSeconds;

    std::string m_traceId = "N/A";
};
