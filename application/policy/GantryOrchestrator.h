#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryGroup.h"
#include "application/UseCaseError.h"
#include <variant>
#include <string>

/**
 * @brief 龙门联动/解耦编排器
 * 
 * 职责：编排"使能 X 轴 → 等待使能完成 → 下发联动/解耦指令 → 等待 PLC 确认"的完整流程。
 * 
 * PLC 硬件约束：
 *   - 使能轴X电机 寄存器会同时使能 X1/X2（PLC 内部逻辑）
 *   - 轴X联动使能 寄存器必须在 使能轴X电机 = ON 时才可操作
 * 
 * 分层职责：
 *   - EnableUseCase 负责：分组路由 + 轴状态幂等检查 + 下发使能命令
 *   - GantryGroup 负责：联动幂等/冲突检查 + 产生 GantryCommand
 *   - GantryOrchestrator 负责：流程编排 + 错误转发
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
        EnsuringEnabled,      // 下发使能 X 轴
        WaitingEnabled,       // 等待 X 轴进入 Idle（使能成功）
        Coupling,             // 下发联动指令
        WaitingCoupled,       // 等待 PLC 反馈联动完成
        Decoupling,           // 下发解耦指令
        WaitingDecoupled,     // 等待 PLC 反馈解耦完成
        Done,
        Error
    };

    /**
     * @param manager   系统管理器（用于 EnableUseCase 的分组路由）
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
        // 分层获取组、轴、龙门对象
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        Axis* x = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(AxisId::X, x, ctxReason)) {
            m_step = Step::Error;
            m_lastError = ctxReason;
            return;
        }

        GantryGroup& gantry = group->gantry();

        switch (m_step) {
        case Step::Initial:
            break;

        // ============================================================
        // 联动流程
        // ============================================================

        case Step::EnsuringEnabled: {
            // EnableUseCase 内部已做分组路由 + 领域幂等检查
            // 不在此处检查 X 是否使能——Axis::enable 自己判断
            auto err = EnableUseCase{}.execute(m_manager, m_groupName, AxisId::X, true);
            if (std::holds_alternative<std::monostate>(err)) {
                m_step = Step::WaitingEnabled;
            } else {
                m_step = Step::Error;
                m_lastError = err;
            }
            break;
        }

        case Step::WaitingEnabled:
            if (x->state() == AxisState::Idle) {
                m_step = Step::Coupling;
            } else if (x->state() == AxisState::Error) {
                m_step = Step::Error;
                m_lastError = x->lastRejection();
            }
            // 其他状态继续等待
            break;

        case Step::Coupling: {
            auto result = gantry.requestCouple(true);
            if (result == GantryRejection::None) {
                // 下发 GantryCommand 到驱动
                if (gantry.hasPendingCommand()) {
                    if (auto* drv = group->driver()) {
                        auto cmd = gantry.popPendingCommand();
                        drv->sendGantry(cmd);
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
            if (gantry.isCoupled()) {
                m_step = Step::Done;
            } else if (gantry.hasError()) {
                m_step = Step::Error;
                m_lastError = gantry.getLastError();
            }
            break;

        // ============================================================
        // 解耦流程
        // ============================================================

        case Step::Decoupling: {
            auto result = gantry.requestCouple(false);
            if (result == GantryRejection::None) {
                if (gantry.hasPendingCommand()) {
                    if (auto* drv = group->driver()) {
                        auto cmd = gantry.popPendingCommand();
                        drv->sendGantry(cmd);
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
            if (!gantry.isCoupled() && !gantry.isCouplingRequested() && !gantry.isDecouplingRequested()) {
                m_step = Step::Done;
            } else if (gantry.hasError()) {
                m_step = Step::Error;
                m_lastError = gantry.getLastError();
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
     * @return UseCaseError variant，包含 ContextRejection / RejectionReason / GantryRejection
     */
    UseCaseError lastError() const { return m_lastError; }

private:
    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step;
    UseCaseError m_lastError = std::monostate{};
};
