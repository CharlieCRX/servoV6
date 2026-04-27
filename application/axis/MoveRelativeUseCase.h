#pragma once
#include "axis/IAxisDriver.h"
#include "infrastructure/logger/Logger.h" // 🌟 引入日志系统

/**
 * @brief 相对位置移动执行案例（纯净动作层）
 */
class MoveRelativeUseCase {
public:
    explicit MoveRelativeUseCase(IAxisDriver& driver) : driver_(driver) {}

    /**
     * @brief 执行相对位移
     * @param axis 轴实体
     * @param distance 移动增量（可正可负）
     * @return RejectionReason 透传领域层判定结果
     */
    RejectionReason execute(Axis& axis, double distance) {
        // 1. 调用领域规则，尝试产生相对定位意图
        // Axis 内部会负责：当前状态检查、终点坐标计算及软限位预检
        if (!axis.moveRelative(distance)) {
            // 🌟 记录业务层面的拒绝
            LOG_WARN(LogLayer::APP, "MoveRelUC", "MoveRelative rejected. Reason code: " + std::to_string(static_cast<int>(axis.lastRejection())));
            return axis.lastRejection();
        }

        // 2. 规则允许，下发 MoveCommand 到驱动层
        driver_.send(axis.getPendingCommand());
        return RejectionReason::None;
    }

private:
    IAxisDriver& driver_;
};