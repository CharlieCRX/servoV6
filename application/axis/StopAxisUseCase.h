#pragma once
#include "application/UseCaseError.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 停止轴用例
 *
 * 完整调用链：
 *   UI (ViewModel) → StopAxisUseCase.execute(manager, groupName, axisId) → UseCaseError
 *
 * 停止是安全指令，在领域层被设计为不可拒绝。
 * 涵盖两层错误：
 *   1. SystemManager 层 — 分组不存在 / 名称非法
 *   2. SystemContext 层 — 龙门联动锁定 / 轴未注册
 */
class StopAxisUseCase {
public:
    StopAxisUseCase() = default;

    /**
     * @brief 对指定分组中的指定轴执行停止
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @return UseCaseError — monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId) {
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

        // ===== 阶段 2：执行停止（领域层不可拒绝） =====
        if (axis->stop()) {
            // 将产生的停止指令发送至硬件抽象层
            if (auto* drv = group->driver()) {
                drv->send(axisId, axis->getPendingCommand());
            }
        }

        return std::monostate{};  // 成功
    }
};
