#pragma once
#include "application/UseCaseError.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/safety/SafetyRejection.h"

/**
 * @brief 设备急停 UseCase
 *
 * 完整调用链：
 *   UI (ViewModel) → EmergencyStopUseCase.execute(manager, groupName) → UseCaseError
 *
 * 涵盖两层错误：
 *   1. SystemManager 层 — 分组不存在 / 名称非法
 *   2. Safety 领域层 — NotSynchronized / AlreadyInState / InvalidStateTransition
 *
 * 成功时：
 *   1. EmergencyStopController::requestEmergencyStop()
 *        → 产生 EmergencyStopCommand{ true }，本地状态 Running → EmergencyStopping
 *   2. SystemContext::driver()->send(EmergencyStopCommand{ true })
 *        → 下发到物理层（PLC 命令寄存器）
 *   3. 下一帧反馈循环：
 *        → PLC "设备急停中" = true → applyFeedback(true) → EmergencyStopped
 *
 * 设计原则：
 *   - 急停命令通过统一命令总线 (ISystemDriver::send) 下发，不走特殊通道
 *   - UseCase 无状态，多次调用幂等安全
 *   - 命令与状态分离：命令写入 PLC 的"设备急停"寄存器，状态由"设备急停中"反馈提供
 */
class EmergencyStopUseCase {
public:
    EmergencyStopUseCase() = default;

    /**
     * @brief 对指定分组触发设备急停
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @return UseCaseError — monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName) {
        // ===== 阶段 0：分组查找（SystemManager 层） =====
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return mgrReason;  // GroupNotFound / GroupNameInvalid
        }

        // ===== 阶段 1：安全域状态机裁决 =====
        auto& controller = group->emergencyStopController();
        SafetyRejection rejection = controller.requestEmergencyStop();
        if (rejection != SafetyRejection::None) {
            return rejection;  // NotSynchronized / AlreadyInState / InvalidStateTransition
        }

        // ===== 阶段 2：若产生了待发送命令，下发至物理驱动 =====
        if (controller.hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                drv->send(controller.popPendingCommand());
            }
        }

        return std::monostate{};  // 成功
    }
};
