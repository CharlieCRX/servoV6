#pragma once

#include "application/policy/GantryMotionOrchestrator.h"
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>

/**
 * @brief 龙门绝对定位策略
 *
 * 继承 GantryMotionOrchestrator，实现龙门 X 轴的绝对定位运动编排。
 *
 * 流程（自动编排）：
 *   用户触发 → 使能+联动 → 触发 ABS_MOVE_TRIGGER → 监视运动
 *   → 运动完成 → 500ms延迟 → 解耦 → 掉电 → Done
 *
 * 使用示例：
 *   // 1. 先独立设置目标（通过 tryReadAxis）
 *   AxisViewModelCore::setAbsTarget(150.0);
 *   // 2. 触发运动
 *   GantryAbsMovePolicy policy(manager, "Machine_A");
 *   policy.startAbsMove(AxisId::X);
 *   while (policy.isBusy()) { policy.tick(); }
 */
class GantryAbsMovePolicy : public GantryMotionOrchestrator {
public:
    GantryAbsMovePolicy(SystemManager& manager, const std::string& groupName)
        : GantryMotionOrchestrator(manager, groupName)
    {
    }

    // ========== 入口 ==========

    /// @brief 启动龙门绝对定位运动
    void startAbsMove(AxisId axisId) {
        startMotion(axisId);
    }

protected:
    // ========== 钩子实现 ==========

    bool executeCommand(Axis& axis, SystemContext& group) override {
        LOG_DEBUG(LogLayer::APP, "GantryAbs",
            logPrefix() + " IssuingCommand: triggering ABS_MOVE_TRIGGER, target="
                + std::to_string(axis.absMoveTarget()));

        if (!axis.triggerAbsMove()) {
            LOG_ERROR(LogLayer::APP, "GantryAbs",
                logPrefix() + " triggerAbsMove rejected: "
                    + std::to_string(static_cast<int>(axis.lastRejection())));
            return false;
        }

        if (!sendPendingCommand(axis, group)) {
            return false;
        }

        m_startPos = axis.currentAbsolutePosition();

        LOG_DEBUG(LogLayer::APP, "GantryAbs",
            logPrefix() + " ABS_MOVE_TRIGGER sent, startPos="
                + std::to_string(m_startPos));
        return true;
    }

    bool checkMotionCompleted(Axis& axis) override {
        // 如果目标距离接近 0，运动不可观测，直接视为完成
        if (std::abs(axis.absMoveTarget() - axis.currentAbsolutePosition()) <= m_epsilon) {
            LOG_DEBUG(LogLayer::APP, "GantryAbs",
                logPrefix() + " Abs move completed: at target (pos≈"
                    + std::to_string(axis.currentAbsolutePosition())
                    + ", target≈" + std::to_string(axis.absMoveTarget()) + ")");
            return true;
        }

        // 判断运动是否完成：轴状态回到 Idle
        if (axis.state() == AxisState::Idle) {
            LOG_DEBUG(LogLayer::APP, "GantryAbs",
                logPrefix() + " Abs move completed: axis Idle (pos="
                    + std::to_string(axis.currentAbsolutePosition()) + ")");
            return true;
        }

        return false;
    }

    std::string motionType() const override {
        return "GantryAbsMove";
    }

private:
    double m_startPos = 0.0;
    const double m_epsilon = 0.01;
};