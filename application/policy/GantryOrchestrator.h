#pragma once

#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "application/UseCaseError.h"
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

    // ========== 逐帧驱动 ==========

    void tick() {
        // 获取 SystemContext
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        GantryPowerController& power = group->gantryPowerController();
        GantryCouplingController& coupling = group->gantryCouplingController();
        ISystemDriver* drv = group->driver();

        switch (m_step) {
        case Step::Initial:
            break;

        // ============================================================
        // 联动流程
        // ============================================================

        case Step::EnsuringEnabled: {
            // 调用 GantryPowerController::requestEnable(true)
            // 内部已做幂等/冲突检查，不在此处重复
            auto result = power.requestEnable(true);
            if (result == GantryRejection::None) {
                // 下发 GantryPowerCommand 到驱动
                if (power.hasPendingCommand() && drv) {
                    auto commResult = drv->send(power.popPendingCommand());
                    if (!commResult.ok()) {
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                m_step = Step::WaitingEnabled;
            } else {
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingEnabled:
            if (power.isEnabled()) {
                m_step = Step::Coupling;
            }
            // 其他状态继续等待（Enabling 尚未完成）
            // 注意：即使 PLC 反馈 Disabled（使能失败），也继续等待，
            //       GantryPowerController::applyFeedback 会如实反映物理状态
            break;

        case Step::Coupling: {
            auto result = coupling.requestCouple(true);
            if (result == GantryRejection::None) {
                // 下发 GantryCouplingCommand 到驱动
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    if (!commResult.ok()) {
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                m_step = Step::WaitingCoupled;
            } else {
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingCoupled:
            if (coupling.isCoupled()) {
                m_step = Step::Done;
            } else if (coupling.hasError()) {
                m_step = Step::Error;
                m_lastError = coupling.getLastError();
            }
            break;

        // ============================================================
        // 解耦流程
        // ============================================================

        case Step::Decoupling: {
            auto result = coupling.requestCouple(false);
            if (result == GantryRejection::None) {
                if (coupling.hasPendingCommand() && drv) {
                    auto commResult = drv->send(coupling.popPendingCommand());
                    if (!commResult.ok()) {
                        m_step = Step::Error;
                        m_lastError = commResult;
                        return;
                    }
                }
                m_step = Step::WaitingDecoupled;
            } else {
                m_step = Step::Error;
                m_lastError = result;
            }
            break;
        }

        case Step::WaitingDecoupled:
            // 解耦操作 PLC 不返回错误码（errorCode 始终为 None），
            // 因此仅依赖 isDecouplingRequested 变为 false 判断解耦完成，
            // 不需要检查 hasError()。
            if (!coupling.isDecouplingRequested()) {
                m_step = Step::Done;
            }
            break;

        case Step::Done:
        case Step::Error:
            break;
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

private:
    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step;
    UseCaseError m_lastError = std::monostate{};
};
