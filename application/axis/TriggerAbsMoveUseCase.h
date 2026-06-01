#pragma once
#include "application/UseCaseError.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"

/**
 * @brief 触发绝对位置移动用例（仅触发 ABS_MOVE_TRIGGER M 寄存器）
 *
 * ★ 此 UseCase 不写目标位置 —— 目标位置已在独立的 setAbsTarget() 路径中
 *   写入 PLC（ViewModel 直调 Domain → consumePendingCommands 消费）。
 *
 * ★ 触发前 PLC 内部必须已有目标值（来自 setAbsTarget 或外部 HMI/PLC 逻辑），
 *   否则 PLC 侧行为未定义。
 *
 * 调用链：
 *   AbsMovePolicy (Step::TriggeringMove)
 *     → TriggerAbsMoveUseCase::execute(manager, groupName, axisId)
 *       → SystemManager::tryGetGroup  (分组查找)
 *       → SystemContext::tryGetAxis   (轴获取 + 安全/龙门拦截)
 *       → Axis::triggerAbsMove()     (Domain 层：状态校验 + 限位预判)
 *       → Driver::send(TriggerAbsMoveCommand)  (仅触发 M 寄存器)
 *     → UseCaseError
 *
 * 四层拦截：
 *   1. SystemManager 层  -- 分组不存在 / 名称非法
 *   2. SystemContext 层   -- 安全锁定 / 龙门联动锁定 / 轴未注册
 *   3. Axis 领域层       -- 状态非法 / 硬限位 / 目标超限
 *   4. 驱动下发层        -- 通讯失败
 */
class TriggerAbsMoveUseCase {
public:
    TriggerAbsMoveUseCase() = default;

    /**
     * @brief 触发指定轴的绝对位置移动
     * @param manager   系统管理器（分组注册表）
     * @param groupName 目标分组名称
     * @param axisId    目标轴 ID
     * @return UseCaseError -- monostate 表示成功，否则为具体错误码
     */
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId) {
        // ===== 阶段 0：分组查找（SystemManager 层） =====
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return mgrReason;
        }

        // ===== 阶段 1：轴获取（SystemContext 层） =====
        //
        // tryGetAxis 内部已内置多层级拦截：
        //   Layer 0 -- 安全锁定：急停中 / 未同步 / 过渡中  -> SystemSafetyLocked
        //   Layer 1 -- 龙门同步：NotSynchronized           -> GantryNotSynchronized
        //   Layer 2 -- 龙门语义：Coupled / Decoupled       -> PhysicalAxisLockedByGantry /
        //                                                    LogicalAxisUnavailableWhenDecoupled
        //   Layer 3 -- 容器查找：轴未注册                   -> AxisNotRegistered
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return ctxReason;
        }

        // ===== 阶段 2：轴领域层状态判定 =====
        //
        // triggerAbsMove() 执行三层校验：
        //   1. 状态准入：只有 Idle 才允许触发
        //   2. 硬限位拦截：正/负限位激活时拒绝
        //   3. ★ 目标限位预判：基于 PLC feedback 回读的 absMoveTarget 值
        //      （而非 setAbsTarget 的调用参数），确保外部 HMI/PLC 写入的目标
        //      也能正确校验
        if (!axis->triggerAbsMove()) {
            return axis->lastRejection();
        }

        // ===== 阶段 3：通过统一命令总线包装下发 =====
        //
        // 生成的 pending command = TriggerAbsMoveCommand{}
        // 对应 PLC 操作：sendEdgeTrigger(ABS_MOVE_TRIGGER)
        // Infrastructure 层直接操作唯一寄存器，零条件判断
        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                auto commResult = drv->send(
                    AxisCommandWithId{axisId, axis->getPendingCommand()});
                if (!commResult.ok()) {
                    return commResult;
                }
            }
        }

        return std::monostate{};  // 成功：TriggerAbsMoveCommand 已送达 PLC
    }
};
