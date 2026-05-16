#pragma once
#include "application/UseCaseError.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"

/**
 * @brief 使能/掉电用例（统一命令总线重构版）
 *
 * 完整调用链：
 *   UI (ViewModel) → EnableUseCase.execute(manager, groupName, axisId, active) → UseCaseError
 *
 * 涵盖四层拦截（从上往下）：
 *   1. SystemManager 层  — 分组不存在 / 名称非法
 *   2. SystemContext 层   — 安全锁定 / 龙门联动锁定 / 轴未注册
 *      （SystemSafetyLocked 急停拦截已内置在 tryGetAxis 中，UseCase 无需显式检查）
 *   3. Axis 领域层       — 状态非法 / 运动中不可掉电
 *   4. 驱动下发层        — 包装 AxisCommandWithId 通过统一命令总线发送
 *
 * TDD 测试兼容性：
 *   - 测试接口保持 execute(manager, groupName, axisId, active) 不变
 *   - 测试预期错误码完全兼容：ContextRejection / RejectionReason / SafetyRejection
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

        // ===== 阶段 1：轴获取（SystemContext 层） =====
        //
        // 注意：tryGetAxis 内部已内置多层级拦截：
        //   Layer 0 — 安全锁定：急停中 / 未同步 / 过渡中  → SystemSafetyLocked
        //   Layer 1 — 龙门同步：NotSynchronized           → GantryNotSynchronized
        //   Layer 2 — 龙门语义：Coupled / Decoupled       → PhysicalAxisLockedByGantry / LogicalAxisUnavailableWhenDecoupled
        //   Layer 3 — 容器查找：轴未注册                   → AxisNotRegistered
        //
        // UseCase 无需显式调用 emergencyStopController().isSystemLocked()
        // 安全域拦截对 UseCase 透明，由 SystemContext 统一裁决。
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return ctxReason;
        }

        // ===== 阶段 2：轴领域层状态判定 =====
        if (!axis->enable(active)) {
            return axis->lastRejection();  // RejectionReason::InvalidState / AlreadyMoving
        }

        // ===== 阶段 3：若产生了待发送命令，通过统一命令总线包装下发 =====
        //
        // 老架构：drv->send(axisId, axis->getPendingCommand())  → 双参数
        // 新架构：drv->send(AxisCommandWithId{axisId, cmd})     → 统一 SystemCommand variant
        //
        // FakeAxisDriver 内部通过 std::visit 分发到 handle(AxisCommandWithId)，
        // 将命令写入 FakePLC 物理寄存器并记录 history。
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
