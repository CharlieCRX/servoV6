#pragma once

#include "gantry/GantryFeedback.h"
#include "gantry/GantryRejection.h"
#include <optional>

/**
 * @brief 龙门电机命令
 *
 * 对应 PLC 寄存器「使能轴X电机」（同时使能 X1/X2 电机）。
 * enable = true → 使能, false → 掉电
 */
struct MotorCommand {
    bool enable;
};

/**
 * @brief 龙门电机控制器
 *
 * 职责：控制 PLC 寄存器「使能轴X电机」。
 *      在任何联动状态下均可访问（不经过 SystemContext 的龙门锁定逻辑）。
 *
 * PLC 硬件约束：
 *  - 「使能轴X电机」寄存器同时控制 X1/X2 电机（PLC 内部逻辑）
 *  - 「轴X状态显示」寄存器反馈电机是否已使能（0/1）
 *
 * 设计决策：
 *  - m_synchronized 独立于 GantryCouplingState::NotSynchronized
 *    电机控制器只需知道是否收到过反馈，不需要五态状态机
 *  - requestEnable 在任何联动状态下均可调用
 *    调用方自行保证语义正确性（场景 2 使能解耦是合理的）
 *  - 幂等内建：m_enabled == active → 返回 None，不生成新命令
 */
class GantryMotorController {
public:
    // ========== 意图生成 ==========

    /**
     * @brief 请求使能/掉电龙门电机
     * @return None           成功（包括幂等：已在目标状态）
     *         NotSynchronized 尚未收到第一帧 PLC 反馈，拒绝操作
     */
    GantryRejection requestEnable(bool active) {
        if (!m_synchronized) return GantryRejection::NotSynchronized;
        if (m_enabled == active) return GantryRejection::None;  // 幂等
        m_enabled = active;
        m_pending_command = MotorCommand{active};
        return GantryRejection::None;
    }

    // ========== 状态查询 ==========

    bool isEnabled() const { return m_enabled; }
    bool isSynchronized() const { return m_synchronized; }
    bool isNotSynchronized() const { return !m_synchronized; }

    // ========== 命令弹出 ==========

    bool hasPendingCommand() const { return m_pending_command.has_value(); }

    MotorCommand popPendingCommand() {
        auto cmd = *m_pending_command;
        m_pending_command.reset();
        return cmd;
    }

    // ========== 反馈接收（由 PLC 轮询线程调用） ==========

    /**
     * @brief 接收 PLC 寄存器「轴X状态显示」的反馈
     *        首次反馈自动标记 m_synchronized = true（退出未同步态）
     */
    void applyFeedback(const GantryFeedback& feedback) {
        m_synchronized = true;
        m_enabled = feedback.enable;
    }

private:
    bool m_enabled = false;
    bool m_synchronized = false;
    std::optional<MotorCommand> m_pending_command;
};
