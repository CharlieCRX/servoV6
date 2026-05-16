#pragma once
#include "application/UseCaseError.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 点动用例
 *
 * 完整调用链：
 *   UI (ViewModel) → JogAxisUseCase.execute(manager, groupName, axisId, dir) → UseCaseError
 *   UI (ViewModel) → JogAxisUseCase.stop(manager, groupName, axisId, dir)    → void
 *
 * 涵盖三层错误：
 *   1. SystemManager 层 — 分组不存在 / 名称非法
 *   2. SystemContext 层 — 龙门联动锁定 / 逻辑轴不可用 / 轴未注册
 *   3. Axis 领域层 — 状态非法 / 限位拦截
 */
class JogAxisUseCase {
public:
    JogAxisUseCase() = default;

    /**
     * @brief 对指定分组中的指定轴执行点动
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @param dir       点动方向
     * @return UseCaseError — monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId,
                         Direction dir) {
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
            return ctxReason;  // PhysicalAxisLockedByGantry / LogicalAxisUnavailableWhenDecoupled / AxisNotRegistered
        }

        // ===== 阶段 2：轴领域层状态判定 =====
        if (!axis->jog(dir)) {
            return axis->lastRejection();  // RejectionReason::InvalidState / AtPositiveLimit / AtNegativeLimit ...
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

    /**
     * @brief 停止指定分组中指定轴的点动
     *
     * stop 是安全操作：
     * - 不返回错误码（无需向 UI 反馈失败）
     * - 分组不存在 / 龙门锁定 → 静默忽略
     * - 仅在有效轴且 stopJog 产生命令时，才下发至驱动
     *
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @param dir       停止的方向
     */
    void stop(SystemManager& manager,
              const std::string& groupName,
              AxisId axisId,
              Direction dir) {
        // ===== 阶段 0：分组查找（SystemManager 层） =====
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return;  // 分组不存在 → 静默返回
        }

        // ===== 阶段 1：轴获取（SystemContext 层） =====
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return;  // 龙门锁定 / 轴未注册 → 静默返回
        }

        // ===== 阶段 2：轴领域层停止点动 =====
        if (axis->stopJog(dir)) {
            // 3. 将产生的指令（JogCommand {active: false}）通过统一命令总线下发
            if (auto* drv = group->driver()) {
                auto commResult = drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
                if (!commResult.ok()) {
                    // stop() 是 void 返回，仅记录日志不返回错误
                    LOG_WARN(LogLayer::APP, "JogUC",
                        "send stop failed for axis, diagnostic=" + commResult.diagnostic);
                }
            }
        }
    }
};
