#pragma once
#include "application/axis/IAxisDriver.h"

/**
 * @brief 绝对定位执行案例（纯净动作层）
 * 严格遵守单一语义原则：只负责发起定位，不混入使能策略
 */
class MoveAbsoluteUseCase {
public:
    explicit MoveAbsoluteUseCase(IAxisDriver& driver) : driver_(driver) {}

    /**
     * @brief 执行绝对定位
     * @param axis 轴实体
     * @param target 目标绝对位置
     * @return RejectionReason 100% 透传自领域层的原始原因
     */
    RejectionReason execute(Axis& axis, double target) {
        // 1. 调用领域层规则，判定该移动请求是否合法
        if (!axis.moveAbsolute(target)) {
            // 2. 拒绝：直接返回领域层的拦截原因，不执行任何自愈逻辑
            return axis.lastRejection();
        }

        // 3. 允许：将 Axis 产生的 MoveCommand 下发给驱动
        driver_.send(axis.getPendingCommand());
        return RejectionReason::None;
    }

private:
    IAxisDriver& driver_;
};