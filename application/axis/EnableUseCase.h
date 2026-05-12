#pragma once
#include "application/UseCaseError.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"

/**
 * @brief 使能/掉电用例
 * 
 * 完整调用链：
 *   UI (ViewModel) → EnableUseCase.execute(manager, groupName, axisId, active) → UceCaseError
 * 
 * 涵盖三层错误：
 *   1. SystemManager 层 — 分组不存在 / 名称非法
 *   2. SystemContext 层 — 龙门联动锁定 / 轴未注册
 *   3. Axis 领域层 — 状态非法 / 运动中不可掉电
 */
class EnableUseCase {
public:
    EnableUseCase() = default;

    /**
     * @brief 对指定分组中的指定轴执行使能/掉电
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @param active    true=使能上电, false=掉电
     * @return UseCaseError — monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId,
                         bool active) {
        // ===== 阶段 0：分组查找（SystemManager 层） =====
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return mgrReason;  // GroupNotFound / GroupNameInvalid
        }

        // ===== 阶段 1：轴获取与龙门校验（SystemContext 层） =====
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return ctxReason;  // PhysicalAxisLockedByGantry / AxisNotRegistered / …
        }

        // ===== 阶段 2：轴领域层状态判定 =====
        if (!axis->enable(active)) {
            return axis->lastRejection();  // RejectionReason::InvalidState / AlreadyMoving
        }

        // ===== 阶段 3：若产生了待发送命令，下发至物理驱动 =====
        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                drv->send(axisId, axis->getPendingCommand());
            }
        }

        return std::monostate{};  // 成功
    }
};
