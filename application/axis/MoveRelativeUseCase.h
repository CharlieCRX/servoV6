#pragma once
#include "application/UseCaseError.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 相对定位用例
 *
 * 完整调用链：
 *   UI (ViewModel) → MoveRelativeUseCase.execute(manager, groupName, axisId, distance) → UseCaseError
 *
 * 涵盖三层错误：
 *   1. SystemManager 层 — 分组不存在 / 名称非法
 *   2. SystemContext 层 — 龙门联动锁定 / 轴未注册
 *   3. Axis 领域层 — 状态非法 / 目标超限 / 已处于限位点
 */
class MoveRelativeUseCase {
public:
    MoveRelativeUseCase() = default;

    /**
     * @brief 对指定分组中的指定轴执行相对位移
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @param distance  移动增量（可正可负）
     * @return UseCaseError — monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId,
                         double distance) {
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
        if (!axis->moveRelative(distance)) {
            LOG_WARN(LogLayer::APP, "MoveRelUC",
                     "MoveRelative rejected. Reason code: "
                         + std::to_string(static_cast<int>(axis->lastRejection())));
            return axis->lastRejection();  // RejectionReason::InvalidState / TargetOutOf… / At…Limit
        }

        // ===== 阶段 3：若产生了待发送命令，通过统一命令总线包装下发 =====
        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
                if (!commResult.ok()) {
                    return commResult;
                }
            }
        }

        return std::monostate{};  // 成功
    }
};
