#pragma once
#include "application/UseCaseError.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/safety/SafetyRejection.h"

/**
 * @brief 解除设备急停 UseCase
 *
 * 完整调用链：
 *   UI (ViewModel) -> ReleaseEmergencyStopUseCase.execute(manager, groupName) -> UseCaseError
 *
 * 前置条件：
 *   - EmergencyStopController 必须处于 EmergencyStopped 状态
 *   - 不接受 Running / EmergencyStopping / NotSynchronized 状态下的解除调用
 *
 * 成功时：
 *   1. EmergencyStopController::requestReleaseEmergencyStop()
 *        -> 产生 EmergencyStopCommand{ false }，本地状态 EmergencyStopped -> ReleasingEmergencyStop
 *   2. SystemContext::driver()->send(EmergencyStopCommand{ false })
 *        -> 下发到物理层（PLC 命令寄存器）
 *   3. 下一帧反馈循环：
 *        -> PLC "设备急停中" = false -> applyFeedback(false) -> Running
 *
 * 设计原则：
 *   - 命令通过统一命令总线 (ISystemDriver::send) 下发
 *   - UseCase 无状态，多次调用幂等安全
 *   - 物理急停按钮与软件急停共享同一解除路径（只要状态在 EmergencyStopped 即可）
 */
class ReleaseEmergencyStopUseCase {
public:
    ReleaseEmergencyStopUseCase() = default;

    /**
     * @brief 解除指定分组的设备急停
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @return UseCaseError -- monostate 表示成功，否则为具体错误码
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
        SafetyRejection rejection = controller.requestReleaseEmergencyStop();
        if (rejection != SafetyRejection::None) {
            return rejection;  // NotSynchronized / NotEmergencyStopped / AlreadyInState
        }

        // ===== 阶段 2：若产生了待发送命令，下发至物理驱动 =====
        if (controller.hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                auto commResult = drv->send(controller.popPendingCommand());
                if (!commResult.ok()) {
                    return commResult;
                }
            }
        }

        return std::monostate{};  // 成功
    }
};
