#pragma once
#include "domain/entity/SystemContext.h"
#include "domain/entity/AxisId.h"

class EnableUseCase {
public:
    // 构造函数现在非常干净，不再持有 Repository 或 Driver 引用
    // 因为这些资源现在都由传入的 SystemContext 实例持有
    EnableUseCase() = default;

    /**
     * @brief 执行使能操作
     * @param group 目标系统分组（平行宇宙实例）
     * @param id 轴 ID
     * @param active 使能/断电
     */
    RejectionReason execute(SystemContext& group, AxisId id, bool active) {
        Axis* axis = nullptr;
        RejectionReason reason = RejectionReason::None;

        // 1. 尝试从分组中获取轴实例
        // 这里会自动触发 SystemContext 内部的龙门状态校验（联动/解耦拦截）
        if (!group.tryGetAxis(id, axis, reason)) {
            return reason; // 返回 InvalidState 等拦截原因
        }

        // 2. 轴内部状态判定（例如：运动中禁止断电）
        if (!axis->enable(active)) {
            return axis->lastRejection();
        }

        // 3. 通过该分组绑定的驱动发送物理指令
        if (axis->hasPendingCommand()) {
            if (auto* drv = group.driver()) {
                drv->send(id, axis->getPendingCommand());
            }
        }

        return RejectionReason::None;
    }
};