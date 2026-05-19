#pragma once

#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "domain/gantry/GantryRejection.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include <variant>
#include <string>

/**
 * @brief 龙门联动/解耦编排器
 *
 * 职责：编排「使能龙门电机 -> 等待使能完成 -> 下发联动/解耦指令 -> 等待 PLC 确认」的完整流程。
 *
 * 分层职责：
 *   - GantryPowerController    负责：电机使能/掉电状态机 + 生成 GantryPowerCommand
 *   - GantryCouplingController 负责：联动/解耦状态机 + 生成 GantryCouplingCommand
 *   - GantryOrchestrator       负责：流程编排 + 错误转发
 *
 * PLC 硬件约束：
 *   -「使能轴X电机」寄存器会同时使能 X1/X2（PLC 内部逻辑）
 *   -「轴X联动使能」寄存器必须在电机使能后才可操作
 *
 * 使用示例：
 *   GantryOrchestrator orch(manager, "Machine_A");
 *   orch.startCoupling();   // 或 orch.startDecoupling();
 *   while (orch.currentStep() != Step::Done && orch.currentStep() != Step::Error) {
 *       orch.tick();
 *   }
 */
class GantryOrchestrator {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,      // 下发龙门电机使能命令
        WaitingEnabled,       // 等待电机使能完成
        Coupling,             // 下发联动指令
        WaitingCoupled,       // 等待 PLC 反馈联动完成
        Decoupling,           // 下发解耦指令
        WaitingDecoupled,     // 等待 PLC 反馈解耦完成
        Disabling,            // 下发龙门电机掉电命令
        WaitingDisabled,      // 等待掉电完成
        Done,
        Error
    };

    /**
     * @param manager   系统管理器（用于获取 SystemContext）
     * @param groupName 目标分组名称
     */
    GantryOrchestrator(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Initial)
    {
    }

    // ========== 入口 ==========

    void startCoupling() {
        m_step = Step::EnsuringEnabled;
    }

    void startDecoupling() {
        m_step = Step::Decoupling;
    }

    /**
     * @brief 一键解除联动并关闭使能（startCoupling 的逆操作）
     *
     * 流程：Decoupling → WaitingDecoupled → Disabling → WaitingDisabled → Done
     *
     * 对应 UI「联动使能按钮」在已联动状态下的反向操作。
     */
    void stopCouplingAndDisable() {
        m_step = Step::Decoupling;
        m_disableAfterDecouple = true;
    }

    /**
     * @brief 使能并解除联动（保持电机使能，仅断联动）
     *
     * 流程：EnsuringEnabled → WaitingEnabled → Decoupling → WaitingDecoupled → Done
     *
     * 对应 UI「解除联动按钮」在已联动状态下的操作（密码验证后）。
     */
    void enableAndDecouple() {
        m_step = Step::EnsuringEnabled;
        m_decoupleAfterEnable = true;
    }

    // ========== 逐帧驱动 ==========

    void tick() {
        // 获取 SystemContext
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            LOG_ERROR(LogLayer::APP, "GantryOrch",
                m_groupName + " tick: can't get context, reason=" + std::to_string(static_cast<int>(mgrReason)));
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        GantryPowerController& power = group->gantryPowerController();
        GantryCouplingController& coupling = group->gantryCouplingController();
        ISystemDriver* drv = group->driver();

        Step oldStep = m_step;

        switch (m_step) {
        case Step::Initial:
            LOG_TRACE(LogLayer::APP, "GantryOrch",
                m_groupName + " Initial: waiting for start command");
            break;

        // ============================================================
        // 联动流程
        // ============================================================

        case Step::EnsuringEnabled: {
            // 调用 GantryPowerController::requestEnable(true)
            // 内部已做幂等/冲突检查，不在此处重复
            auto result = power.requestEnable(true);
            LOG_DEBUG(LogLayer::APP, "GantryOrch",
                m_groupName + " EnsuringEnabled: requestEnable(true) result=" + rejectionToString(result));
            if (result == GantryRejection::None) {
                // 下发 GantryPowerCommand 到驱动
                if (power.hasPendingCommand() && drv) {
                    auto commResult = drv->send(power.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " EnsuringEnabled: send power command, ok=" + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryOrch",
                            m_groupName + " EnsuringEnabled -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryOrch",
                    m_groupName + " EnsuringEnabled -> WaitingEnabled");
                m_step = Step::WaitingEnabled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryOrch",
                    m_groupName + " EnsuringEnabled -> Error: rejected " + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingEnabled:
            LOG_TRACE(LogLayer::APP, "GantryOrch",
                m_groupName + " WaitingEnabled: power.isEnabled=" + std::to_string(power.isEnabled())
                + " decoupleAfterEnable=" + std::to_string(m_decoupleAfterEnable));
            if (power.isEnabled()) {
                if (m_decoupleAfterEnable) {
                    m_decoupleAfterEnable = false;
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " WaitingEnabled -> Decoupling (enableAndDecouple flow)");
                    m_step = Step::Decoupling;
                } else {
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " WaitingEnabled -> Coupling");
                    m_step = Step::Coupling;
                }
            }
            break;

        case Step::Coupling: {
            auto result = coupling.requestCouple(true);
            LOG_DEBUG(LogLayer::APP, "GantryOrch",
                m_groupName + " Coupling: requestCouple(true) result=" + rejectionToString(result));
            if (result == GantryRejection::None) {
                // 下发 GantryCouplingCommand 到驱动
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " Coupling: send coupling command, ok=" + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryOrch",
                            m_groupName + " Coupling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryOrch",
                    m_groupName + " Coupling -> WaitingCoupled");
                m_step = Step::WaitingCoupled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryOrch",
                    m_groupName + " Coupling -> Error: rejected " + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingCoupled:
            LOG_TRACE(LogLayer::APP, "GantryOrch",
                m_groupName + " WaitingCoupled: isCoupled=" + std::to_string(coupling.isCoupled())
                + " hasError=" + std::to_string(coupling.hasError()));
            if (coupling.isCoupled()) {
                LOG_INFO(LogLayer::APP, "GantryOrch",
                    m_groupName + " WaitingCoupled -> Done (coupling confirmed by PLC)");
                m_step = Step::Done;
            } else if (coupling.hasError()) {
                LOG_WARN(LogLayer::APP, "GantryOrch",
                    m_groupName + " WaitingCoupled -> Error: "
                    + rejectionToString(coupling.getLastError()));
                m_step = Step::Error;
                m_lastError = coupling.getLastError();
            }
            break;

        // ============================================================
        // 解耦流程
        // ============================================================

        case Step::Decoupling: {
            auto result = coupling.requestCouple(false);
            LOG_DEBUG(LogLayer::APP, "GantryOrch",
                m_groupName + " Decoupling: requestCouple(false) result=" + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " Decoupling: send decoupling command, ok=" + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryOrch",
                            m_groupName + " Decoupling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryOrch",
                    m_groupName + " Decoupling -> WaitingDecoupled");
                m_step = Step::WaitingDecoupled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryOrch",
                    m_groupName + " Decoupling -> Error: rejected " + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingDecoupled:
            LOG_TRACE(LogLayer::APP, "GantryOrch",
                m_groupName + " WaitingDecoupled: isDecouplingRequested="
                + std::to_string(coupling.isDecouplingRequested()));
            // 解耦操作 PLC 不返回错误码（errorCode 始终为 None），
            // 因此仅依赖 isDecouplingRequested 变为 false 判断解耦完成，
            // 不需要检查 hasError()。
            if (!coupling.isDecouplingRequested()) {
                if (m_disableAfterDecouple) {
                    m_disableAfterDecouple = false;
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " WaitingDecoupled -> Disabling (stopCouplingAndDisable flow)");
                    m_step = Step::Disabling;
                } else {
                    LOG_INFO(LogLayer::APP, "GantryOrch",
                        m_groupName + " WaitingDecoupled -> Done (decoupling confirmed)");
                    m_step = Step::Done;
                }
            }
            break;

        // ============================================================
        // 掉电流程（stopCouplingAndDisable 后半段）
        // ============================================================

        case Step::Disabling: {
            auto result = power.requestEnable(false);
            LOG_DEBUG(LogLayer::APP, "GantryOrch",
                m_groupName + " Disabling: requestEnable(false) result=" + rejectionToString(result));
            if (result == GantryRejection::None) {
                if (power.hasPendingCommand() && drv) {
                    auto commResult = drv->send(power.popPendingCommand());
                    LOG_DEBUG(LogLayer::APP, "GantryOrch",
                        m_groupName + " Disabling: send power disable command, ok=" + std::to_string(commResult.ok()));
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "GantryOrch",
                            m_groupName + " Disabling -> Error: comm failed");
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                LOG_DEBUG(LogLayer::APP, "GantryOrch",
                    m_groupName + " Disabling -> WaitingDisabled");
                m_step = Step::WaitingDisabled;
            } else {
                LOG_WARN(LogLayer::APP, "GantryOrch",
                    m_groupName + " Disabling -> Error: rejected " + rejectionToString(result));
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingDisabled:
            LOG_TRACE(LogLayer::APP, "GantryOrch",
                m_groupName + " WaitingDisabled: power.isEnabled=" + std::to_string(power.isEnabled()));
            if (!power.isEnabled()) {
                LOG_INFO(LogLayer::APP, "GantryOrch",
                    m_groupName + " WaitingDisabled -> Done (power off confirmed)");
                m_step = Step::Done;
            }
            break;

        case Step::Done:
            // 终态：tick 不再执行有效逻辑
            break;

        case Step::Error:
            // 终态：tick 不再执行有效逻辑
            break;
        }

        // 记录状态转换日志（当 step 变化且不是终态重复时）
        if (oldStep != m_step) {
            LOG_DEBUG(LogLayer::APP, "GantryOrch",
                m_groupName + " step transition: " + stepToString(oldStep)
                + " -> " + stepToString(m_step));
            if (m_step == Step::Done) {
                LOG_INFO(LogLayer::APP, "GantryOrch",
                    m_groupName + " Flow completed successfully");
            } else if (m_step == Step::Error) {
                LOG_ERROR(LogLayer::APP, "GantryOrch",
                    m_groupName + " Flow ended with error");
            }
        }
    }

    // ========== 状态查询 ==========

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }

    /**
     * @brief 获取最后一次错误
     * @return UseCaseError variant，包含 ContextRejection / GantryRejection
     */
    UseCaseError lastError() const { return m_lastError; }

    /// @brief Step 枚举转字符串（日志辅助）
    static std::string stepToString(Step s) {
        switch (s) {
            case Step::Initial:          return "Initial";
            case Step::EnsuringEnabled:  return "EnsuringEnabled";
            case Step::WaitingEnabled:   return "WaitingEnabled";
            case Step::Coupling:         return "Coupling";
            case Step::WaitingCoupled:   return "WaitingCoupled";
            case Step::Decoupling:       return "Decoupling";
            case Step::WaitingDecoupled: return "WaitingDecoupled";
            case Step::Disabling:        return "Disabling";
            case Step::WaitingDisabled:  return "WaitingDisabled";
            case Step::Done:             return "Done";
            case Step::Error:            return "Error";
            default: return "Unknown(" + std::to_string(static_cast<int>(s)) + ")";
        }
    }

private:
    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step;
    UseCaseError m_lastError = std::monostate{};
    bool m_disableAfterDecouple = false;
    bool m_decoupleAfterEnable = false;
};
