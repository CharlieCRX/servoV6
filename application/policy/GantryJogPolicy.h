#pragma once

#include "application/policy/GantryMotionOrchestrator.h"
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 龙门点动策略
 *
 * 继承 GantryMotionOrchestrator，实现龙门 X 轴的点动运动编排。
 *
 * 流程（自动编排）：
 *   用户长按 → 使能+联动 → 下发点动 → 运行中
 *   用户松手 → 停止点动 → 等待Idle → 500ms延迟 → 解耦 → 掉电 → Done
 *
 * 使用示例：
 *   GantryJogPolicy policy(manager, "Machine_A");
 *   policy.startJog(AxisId::X, Direction::Forward);
 *   while (policy.isBusy()) { policy.tick(); }
 *   // 用户松手
 *   policy.stopJog();
 *   while (policy.isBusy()) { policy.tick(); }
 */
class GantryJogPolicy : public GantryMotionOrchestrator {
public:
    GantryJogPolicy(SystemManager& manager, const std::string& groupName)
        : GantryMotionOrchestrator(manager, groupName)
    {
    }

    // ========== 入口 ==========

    /// @brief 启动龙门点动
    void startJog(AxisId axisId, Direction dir) {
        m_dir = dir;
        m_userPressing = true;
        m_stopIssued = false;
        startMotion(axisId);
    }

    /// @brief 用户松手，停止点动
    void stopJog() {
        LOG_INFO(LogLayer::APP, "GantryJog",
            logPrefix() + " Stop requested by UI");
        m_userPressing = false;
    }

protected:
    // ========== 钩子实现 ==========

    bool executeCommand(Axis& axis, SystemContext& group) override {
        LOG_DEBUG(LogLayer::APP, "GantryJog",
            logPrefix() + " IssuingCommand: sending Jog command "
                + directionName(m_dir));

        if (!axis.jog(m_dir)) {
            LOG_ERROR(LogLayer::APP, "GantryJog",
                logPrefix() + " Jog rejected: "
                    + std::to_string(static_cast<int>(axis.lastRejection())));
            return false;
        }

        if (!sendPendingCommand(axis, group)) {
            return false;
        }

        LOG_DEBUG(LogLayer::APP, "GantryJog",
            logPrefix() + " Jog command sent successfully");
        return true;
    }

    bool checkMotionCompleted(Axis& axis) override {
        // 用户松手但轴还未停稳 → 通过 axis.stopJog() 产生停止命令
        // （父类 Monitoring 步骤会自动消费待执行命令并发送到驱动）
        if (!m_userPressing && !m_stopIssued) {
            LOG_DEBUG(LogLayer::APP, "GantryJog",
                logPrefix() + " User released, requesting Stop");
            axis.stopJog(m_dir);
            m_stopIssued = true;
            return false;  // 停止命令已发出，等待轴停稳
        }

        // 点动完成条件：用户已松手 + 轴已停稳进入 Idle
        if (!m_userPressing && axis.state() == AxisState::Idle) {
            LOG_DEBUG(LogLayer::APP, "GantryJog",
                logPrefix() + " Jog completed: user released, axis Idle");
            return true;
        }

        return false;
    }

    std::string motionType() const override {
        return "GantryJog(" + directionName(m_dir) + ")";
    }

private:
    static std::string directionName(Direction dir) {
        return dir == Direction::Forward ? "Forward(+)" : "Backward(-)";
    }

    Direction m_dir = Direction::Forward;
    bool m_userPressing = false;
    bool m_stopIssued = false;
};