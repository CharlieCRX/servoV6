#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "domain/gantry/GantryRejection.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include <variant>
#include <string>
#include <chrono>

/**
 * @brief 龙门运动编排器基类
 *
 * 为龙门 X 轴的运动操作（点动/绝对定位/相对定位）提供标准编排模板：
 *   1. startMotion()  → 使能龙门电机 + 联动
 *   2. tick()          → 推进使能→联动→执行→等待完成→解耦→掉电
 *   3. cancelMotion()  → 中断当前运动
 *
 * 状态机：
 *   Idle → EnsuringEnabled → WaitingEnabled → PostEnableDelay → Coupling → WaitingCoupled
 *   → PostCouplingDelay → IssuingCommand（子类实现） → Monitoring（子类实现）
 *   → PostMotionDelay → Decoupling → WaitingDecoupled → PostDecoupleDelay → Disabling → WaitingDisabled → Done
 *
 * 与单轴 JogOrchestrator 的差异：
 *   - 单轴：使能 → 点动 → 掉电（500ms 延迟）
 *   - 龙门：使能+联动 → 点动 → 解耦+掉电（500ms 延迟）
 *
 * 与 GantryOrchestrator 的差异：
 *   - GantryOrchestrator：纯联动/解耦编排，不含运动阶段
 *   - GantryMotionOrchestrator：联动 + 运动 + 解耦全流程编排
 */
class GantryMotionOrchestrator {
public:
    enum class Step {
        Idle,
        // --- 前置阶段：使能 + 延迟 + 联动 + 延迟 ---
        EnsuringEnabled,      // 下发龙门电机使能命令
        WaitingEnabled,       // 等待电机使能完成
        PostEnableDelay,      // 使能完成后延迟 400ms 确保运行稳定
        Coupling,             // 下发联动指令
        WaitingCoupled,       // 等待 PLC 反馈联动完成
        PostCouplingDelay,    // 联动确认后延迟 400ms 确保物理状态稳定

        // --- 运动阶段（子类重写钩子方法实现） ---
        IssuingCommand,       // 子类：下发具体运动指令（点动/绝对定位/相对定位）
        Monitoring,           // 子类：监视运动执行状态

        // --- 后置阶段：延迟 + 解耦 + 延迟 + 掉电 ---
        PostMotionDelay,      // 运动结束后等待 400ms 确保物理状态稳定 → 然后解耦
        Decoupling,           // 下发解耦指令
        WaitingDecoupled,     // 等待 PLC 反馈解耦完成
        PostDecoupleDelay,    // 解耦完成后延迟 400ms → 然后掉电
        Disabling,            // 下发龙门电机掉电命令
        WaitingDisabled,      // 等待掉电完成

        // --- 终态 ---
        Done,
        Error,
        ErrorCleaning         // 运动中出错 → 解耦+掉电清理
    };

    GantryMotionOrchestrator(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Idle)
    {
    }

    virtual ~GantryMotionOrchestrator() = default;

    // ========== 入口 ==========

    /**
     * @brief 启动龙门运动
     * @param axisId  目标轴（通常为 AxisId::X）
     */
    void startMotion(AxisId axisId) {
        m_axisId = axisId;
        m_step = Step::EnsuringEnabled;
        m_lastError = std::monostate{};

        m_enableSent    = false;
        m_coupleSent    = false;
        m_commandIssued = false;
        m_decoupleSent  = false;
        m_disableSent   = false;
        m_cleanupAfterError = false;

        m_postEnableDoneTime = std::chrono::steady_clock::time_point{};
        m_postCouplingDoneTime = std::chrono::steady_clock::time_point{};
        m_motionDoneTime = std::chrono::steady_clock::time_point{};
        m_postDecoupleDoneTime = std::chrono::steady_clock::time_point{};

        LOG_INFO(LogLayer::APP, "GantryMotion",
            logPrefix() + " START " + motionType());
    }

    /// @brief 取消当前运动（提前进入解耦掉电流程）
    void cancelMotion() {
        if (m_step != Step::Idle && m_step != Step::Done && m_step != Step::Error) {
            LOG_INFO(LogLayer::APP, "GantryMotion",
                logPrefix() + " Cancel requested -- entering Decoupling");
            m_step = Step::Decoupling;
        }
    }

    // ========== 逐帧驱动 ==========

    void tick() {
        // Layer 0：分组解析
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            LOG_ERROR(LogLayer::APP, "GantryMotion",
                logPrefix() + " tick: can't get context, reason="
                    + std::to_string(static_cast<int>(mgrReason)));
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        // 急停安全锁检查
        if (group->emergencyStopController().isSystemLocked()) {
            if (m_step != Step::Idle && m_step != Step::Done && m_step != Step::Error) {
                LOG_INFO(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Safety locked -- aborting gracefully");
                m_step = Step::Done;
                m_lastError = std::monostate{};
            }
            return;
        }

        GantryPowerController& power = group->gantryPowerController();
        GantryCouplingController& coupling = group->gantryCouplingController();
        ISystemDriver* drv = group->driver();

        Step oldStep = m_step;

        switch (m_step) {
        case Step::Idle:
            break;

        // ============================================================
        // EnsuringEnabled：下发龙门电机使能命令
        // ============================================================
        case Step::EnsuringEnabled: {
            auto result = power.requestEnable(true);
            LOG_DEBUG(LogLayer::APP, "GantryMotion",
                logPrefix() + " EnsuringEnabled: requestEnable(true) result="
                    + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (power.hasPendingCommand() && drv) {
                    auto commResult = drv->send(power.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryMotion",
                        logPrefix() + " EnsuringEnabled: send power command, ok="
                            + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryMotion",
                            logPrefix() + " EnsuringEnabled -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " EnsuringEnabled -> WaitingEnabled");
                m_step = Step::WaitingEnabled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryMotion",
                    logPrefix() + " EnsuringEnabled -> Error: rejected "
                        + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        // ============================================================
        // WaitingEnabled：等待电机使能完成
        // ============================================================
        case Step::WaitingEnabled:
            LOG_TRACE(LogLayer::APP, "GantryMotion",
                logPrefix() + " WaitingEnabled: power.isEnabled="
                    + std::to_string(power.isEnabled()));
            if (power.isEnabled()) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " WaitingEnabled -> PostEnableDelay");
                m_postEnableDoneTime = std::chrono::steady_clock::now();
                m_step = Step::PostEnableDelay;
            }
            break;

        // ============================================================
        // PostEnableDelay：使能完成后延迟 400ms 确保运行稳定 → 然后联动
        // ============================================================
        case Step::PostEnableDelay: {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_postEnableDoneTime).count();
            if (elapsed >= kPostEnableDelaySeconds) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " PostEnableDelay -> Coupling (stabilized after "
                        + std::to_string(elapsed * 1000) + "ms)");
                m_step = Step::Coupling;
            }
            break;
        }

        // ============================================================
        // Coupling：下发联动指令
        // ============================================================
        case Step::Coupling: {
            auto result = coupling.requestCouple(true);
            LOG_DEBUG(LogLayer::APP, "GantryMotion",
                logPrefix() + " Coupling: requestCouple(true) result="
                    + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryMotion",
                        logPrefix() + " Coupling: send coupling command, ok="
                            + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryMotion",
                            logPrefix() + " Coupling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Coupling -> WaitingCoupled");
                m_step = Step::WaitingCoupled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Coupling -> Error: rejected "
                        + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        // ============================================================
        // WaitingCoupled：等待 PLC 反馈联动完成
        // ============================================================
        case Step::WaitingCoupled:
            LOG_TRACE(LogLayer::APP, "GantryMotion",
                logPrefix() + " WaitingCoupled: isCoupled="
                    + std::to_string(coupling.isCoupled())
                    + " hasError=" + std::to_string(coupling.hasError()));
            if (coupling.isCoupled()) {
                LOG_INFO(LogLayer::APP, "GantryMotion",
                    logPrefix() + " WaitingCoupled -> PostCouplingDelay (coupling confirmed by PLC)");
                m_postCouplingDoneTime = std::chrono::steady_clock::now();
                m_step = Step::PostCouplingDelay;
            } else if (coupling.hasError()) {
                m_lastError = coupling.getLastError();
                m_cleanupAfterError = true;
                LOG_WARN(LogLayer::APP, "GantryMotion",
                    logPrefix() + " WaitingCoupled -> Decoupling (cleanup after error: "
                        + rejectionToString(coupling.getLastError()) + ")");
                m_step = Step::Decoupling;
            }
            break;

        // ============================================================
        // PostCouplingDelay：联动确认后延迟 400ms 确保物理状态稳定 → 然后发送运动命令
        // ============================================================
        case Step::PostCouplingDelay: {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_postCouplingDoneTime).count();
            if (elapsed >= kPostCouplingDelaySeconds) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " PostCouplingDelay -> IssuingCommand (stabilized after "
                        + std::to_string(elapsed * 1000) + "ms)");
                m_step = Step::IssuingCommand;
            }
            break;
        }

        // ============================================================
        // IssuingCommand：子类下发具体运动指令（仅调用一次）
        // ============================================================
        case Step::IssuingCommand:
            if (!m_commandIssued) {
                // 获取 X 轴（此时联动已确认，tryGetAxis(X) 应通过）
                {
                    Axis* axis = nullptr;
                    ContextRejection ctxReason = ContextRejection::None;
                    if (!group->tryGetAxis(m_axisId, axis, ctxReason)) {
                        LOG_ERROR(LogLayer::APP, "GantryMotion",
                            logPrefix() + " IssuingCommand: tryGetAxis failed, reason="
                                + std::string(contextRejectionToString(ctxReason)));
                        m_step = Step::ErrorCleaning;
                        m_lastError = ctxReason;
                        m_cleanupAfterError = true;
                        return;
                    }

                    // 调用子类钩子下发运动指令
                    if (!executeCommand(*axis, *group)) {
                        LOG_ERROR(LogLayer::APP, "GantryMotion",
                            logPrefix() + " IssuingCommand: executeCommand failed");
                        m_step = Step::ErrorCleaning;
                        m_lastError = captureAxisError(*axis);
                        m_cleanupAfterError = true;
                        return;
                    }
                }
                m_commandIssued = true;
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " IssuingCommand -> Monitoring");
                m_step = Step::Monitoring;
            }
            break;

        // ============================================================
        // Monitoring：子类监视运动执行状态
        // ============================================================
        case Step::Monitoring: {
            Axis* axis = nullptr;
            ContextRejection ctxReason = ContextRejection::None;
            if (!group->tryGetAxis(m_axisId, axis, ctxReason)) {
                LOG_ERROR(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Monitoring: tryGetAxis failed, reason="
                        + std::string(contextRejectionToString(ctxReason)));
                m_step = Step::ErrorCleaning;
                m_lastError = ctxReason;
                m_cleanupAfterError = true;
                return;
            }

            // 异常跌落检测：轴意外回到 Error
            if (axis->state() == AxisState::Error) {
                LOG_ERROR(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Monitoring: Axis Error state detected");
                m_step = Step::ErrorCleaning;
                m_lastError = axis->lastRejection();
                m_cleanupAfterError = true;
                return;
            }

            // 调用子类钩子判断运动是否完成
            // 注意：钩子（如 GantryJogPolicy::checkMotionCompleted）可能产生
            // 新的待执行命令（如点动停止 JogCommand{active=false}），必须
            // 在钩子调用后立即消费，否则在两个 tick 之间 applyFeedback 会
            // 在 Jogging 状态下无条件清除 JogCommand，导致停止命令丢失。
            if (checkMotionCompleted(*axis)) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Monitoring -> PostMotionDelay (motion completed)");
                m_motionDoneTime = std::chrono::steady_clock::now();
                m_step = Step::PostMotionDelay;
            }

            // ★ 消费子类钩子产生的 Axis 待执行命令（如点动停止命令）
            if (axis->hasPendingCommand() && drv) {
                drv->send(AxisCommandWithId{m_axisId, axis->getPendingCommand()});
            }
            break;
        }

        // ============================================================
        // PostMotionDelay：运动结束后等待 400ms 确保物理状态稳定 → 然后解耦
        // ============================================================
        case Step::PostMotionDelay: {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_motionDoneTime).count();
            if (elapsed >= kPostMotionDelaySeconds) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " PostMotionDelay -> Decoupling (stabilized after "
                        + std::to_string(elapsed * 1000) + "ms)");
                m_step = Step::Decoupling;
            }
            break;
        }

        // ============================================================
        // Decoupling：下发解耦指令
        // ============================================================
        case Step::Decoupling: {
            auto result = coupling.requestCouple(false);
            LOG_DEBUG(LogLayer::APP, "GantryMotion",
                logPrefix() + " Decoupling: requestCouple(false) result="
                    + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryMotion",
                        logPrefix() + " Decoupling: send decoupling command, ok="
                            + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryMotion",
                            logPrefix() + " Decoupling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Decoupling -> WaitingDecoupled");
                m_step = Step::WaitingDecoupled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Decoupling -> Error: rejected "
                        + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        // ============================================================
        // WaitingDecoupled：等待 PLC 反馈解耦完成
        // ============================================================
        case Step::WaitingDecoupled:
            LOG_TRACE(LogLayer::APP, "GantryMotion",
                logPrefix() + " WaitingDecoupled: isDecouplingRequested="
                    + std::to_string(coupling.isDecouplingRequested()));
            if (!coupling.isDecouplingRequested()) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " WaitingDecoupled -> PostDecoupleDelay (decoupled by PLC)");
                m_postDecoupleDoneTime = std::chrono::steady_clock::now();
                m_step = Step::PostDecoupleDelay;
            }
            break;

        // ============================================================
        // PostDecoupleDelay：解耦完成后延迟 400ms → 然后掉电
        // ============================================================
        case Step::PostDecoupleDelay: {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - m_postDecoupleDoneTime).count();
            if (elapsed >= kPostDecoupleDelaySeconds) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " PostDecoupleDelay -> Disabling (stabilized after "
                        + std::to_string(elapsed * 1000) + "ms)");
                m_step = Step::Disabling;
            }
            break;
        }

        // ============================================================
        // Disabling：下发龙门电机掉电命令
        // ============================================================
        case Step::Disabling: {
            auto result = power.requestEnable(false);
            LOG_DEBUG(LogLayer::APP, "GantryMotion",
                logPrefix() + " Disabling: requestEnable(false) result="
                    + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (power.hasPendingCommand() && drv) {
                    auto commResult = drv->send(power.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryMotion",
                        logPrefix() + " Disabling: send power disable command, ok="
                            + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryMotion",
                            logPrefix() + " Disabling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Disabling -> WaitingDisabled");
                m_step = Step::WaitingDisabled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Disabling -> Error: rejected "
                        + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        // ============================================================
        // WaitingDisabled：等待掉电完成
        // ============================================================
        case Step::WaitingDisabled:
            LOG_TRACE(LogLayer::APP, "GantryMotion",
                logPrefix() + " WaitingDisabled: power.isEnabled="
                    + std::to_string(power.isEnabled())
                    + " cleanupAfterError=" + std::to_string(m_cleanupAfterError));
            if (!power.isEnabled()) {
                if (m_cleanupAfterError) {
                    m_cleanupAfterError = false;
                    LOG_ERROR(LogLayer::APP, "GantryMotion",
                        logPrefix() + " WaitingDisabled -> Error (cleanup completed)");
                    m_step = Step::Error;
                } else {
                    LOG_INFO(LogLayer::APP, "GantryMotion",
                        logPrefix() + " WaitingDisabled -> Done (power off confirmed)");
                    m_step = Step::Done;
                }
            }
            break;

        // ============================================================
        // ErrorCleaning：运动中出错 → 解耦 + 掉电清理
        // ============================================================
        case Step::ErrorCleaning:
            // 先解耦，再掉电（复用 existing transitions）
            if (!m_decoupleSent) {
                auto result = coupling.requestCouple(false);
                if (result == GantryRejection::None) {
                    if (coupling.hasPendingCommand() && drv) {
                        drv->send(coupling.popPendingCommand());
                    }
                    m_decoupleSent = true;
                }
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " ErrorCleaning: requesting decouple");
            }
            // 等待解耦完成后进入 Disabling
            if (m_decoupleSent && !coupling.isDecouplingRequested()) {
                LOG_DEBUG(LogLayer::APP, "GantryMotion",
                    logPrefix() + " ErrorCleaning -> Disabling (decoupled)");
                m_step = Step::Disabling;
            }
            break;

        case Step::Done:
        case Step::Error:
        default:
            break;
        }

        // 记录状态转换日志
        if (oldStep != m_step) {
            LOG_DEBUG(LogLayer::APP, "GantryMotion",
                logPrefix() + " step transition: " + stepToString(oldStep)
                    + " -> " + stepToString(m_step));
            if (m_step == Step::Done) {
                LOG_INFO(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Flow completed successfully");
            } else if (m_step == Step::Error) {
                LOG_ERROR(LogLayer::APP, "GantryMotion",
                    logPrefix() + " Flow ended with error");
            }
        }
    }

    // ========== 状态查询 ==========

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }
    UseCaseError lastError() const { return m_lastError; }

    /// @brief 是否处于活跃流程中（非终态且非 Idle）
    bool isBusy() const {
        return m_step != Step::Idle
            && m_step != Step::Done
            && m_step != Step::Error;
    }

    static std::string stepToString(Step s) {
        switch (s) {
            case Step::Idle:              return "Idle";
            case Step::EnsuringEnabled:   return "EnsuringEnabled";
            case Step::WaitingEnabled:    return "WaitingEnabled";
            case Step::PostEnableDelay:    return "PostEnableDelay";
            case Step::Coupling:           return "Coupling";
            case Step::WaitingCoupled:     return "WaitingCoupled";
            case Step::PostCouplingDelay:  return "PostCouplingDelay";
            case Step::IssuingCommand:     return "IssuingCommand";
            case Step::Monitoring:         return "Monitoring";
            case Step::PostMotionDelay:    return "PostMotionDelay";
            case Step::Decoupling:         return "Decoupling";
            case Step::WaitingDecoupled:   return "WaitingDecoupled";
            case Step::PostDecoupleDelay:  return "PostDecoupleDelay";
            case Step::Disabling:          return "Disabling";
            case Step::WaitingDisabled:   return "WaitingDisabled";
            case Step::Done:              return "Done";
            case Step::Error:             return "Error";
            case Step::ErrorCleaning:     return "ErrorCleaning";
            default: return "Unknown(" + std::to_string(static_cast<int>(s)) + ")";
        }
    }

protected:
    // ========== 钩子方法（子类重写） ==========

    /**
     * @brief 在 IssuingCommand 阶段执行（仅调用一次）
     * @param axis 已通过 tryGetAxis 校验的 Axis 引用
     * @param group 当前 SystemContext（用于获取 driver 下发命令）
     * @return true 成功，false 失败
     */
    virtual bool executeCommand(Axis& axis, SystemContext& group) = 0;

    /**
     * @brief 在 Monitoring 阶段每 tick 调用
     * @param axis 已通过 tryGetAxis 校验的 Axis 引用
     * @return true 运动完成（→ 进入解耦阶段），false 仍在运动中
     */
    virtual bool checkMotionCompleted(Axis& axis) = 0;

    /// @brief 获取运动类型名称（用于日志）
    virtual std::string motionType() const = 0;

    // ========== 子类辅助方法 ==========

    /// @brief 下发 Axis 待执行命令到驱动
    bool sendPendingCommand(Axis& axis, SystemContext& group) {
        if (axis.hasPendingCommand()) {
            if (auto* drv = group.driver()) {
                auto commResult = drv->send(
                    AxisCommandWithId{m_axisId, axis.getPendingCommand()});
                if (!commResult.ok()) {
                    LOG_ERROR(LogLayer::APP, "GantryMotion",
                        logPrefix() + " sendPendingCommand: comm failed");
                    return false;
                }
            }
        }
        return true;
    }

    /// @brief 捕获 Axis 当前错误
    UseCaseError captureAxisError(Axis& axis) {
        return axis.lastRejection();
    }

    /// @brief 获取日志前缀
    std::string logPrefix() const {
        return "[" + m_groupName + "][" + axisIdToString(m_axisId) + "] ";
    }

    SystemManager& manager() { return m_manager; }
    const std::string& groupName() const { return m_groupName; }
    AxisId axisId() const { return m_axisId; }

private:
    static std::string rejectionToString(GantryRejection r) {
        switch (r) {
            case GantryRejection::None:                       return "None";
            case GantryRejection::NotSynchronized:            return "NotSynchronized";
            case GantryRejection::StateConflict:              return "StateConflict";
            case GantryRejection::PositionToleranceExceeded:  return "PositionToleranceExceeded";
            case GantryRejection::X1NotEnabled:               return "X1NotEnabled";
            case GantryRejection::X2NotEnabled:               return "X2NotEnabled";
            case GantryRejection::X1NotStationary:            return "X1NotStationary";
            case GantryRejection::X2NotStationary:            return "X2NotStationary";
            case GantryRejection::UnknownError:               return "UnknownError";
        }
        return "?";
    }

    SystemManager& m_manager;
    std::string m_groupName;
    AxisId m_axisId = AxisId::X;
    Step m_step = Step::Idle;
    UseCaseError m_lastError = std::monostate{};

    // --- 内部状态标志 ---
    bool m_commandIssued = false;
    bool m_cleanupAfterError = false;

    // --- 使能后延迟相关 ---
    std::chrono::steady_clock::time_point m_postEnableDoneTime;

    // --- 联动后延迟相关 ---
    std::chrono::steady_clock::time_point m_postCouplingDoneTime;

    // --- 运动后延迟相关 ---
    std::chrono::steady_clock::time_point m_motionDoneTime;

    // --- 解耦后延迟相关 ---
    std::chrono::steady_clock::time_point m_postDecoupleDoneTime;

    // --- 解耦相关 ---
    bool m_decoupleSent = false;

    // 未使用的标志（保持与父类框架一致）
    bool m_enableSent = false;
    bool m_coupleSent = false;
    bool m_disableSent = false;

    static constexpr double kPostEnableDelaySeconds = 0.5;
    static constexpr double kPostCouplingDelaySeconds = 0.5;
    static constexpr double kPostMotionDelaySeconds = 0.5;
    static constexpr double kPostDecoupleDelaySeconds = 0.5;
};