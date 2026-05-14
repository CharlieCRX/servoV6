#pragma once

#include "gantry/GantryFeedback.h"
#include "gantry/GantryRejection.h"
#include <optional>

/**
 * @brief 龙门电机控制器 — 五态全闭环状态机
 *
 * 职责：控制 PLC 寄存器「使能轴X电机」（同时使能 X1/X2 电机）。
 *      在任何联动状态下均可访问（不经过 SystemContext 的龙门锁定逻辑）。
 *
 * 状态机流转：
 *  NotSynchronized ──[applyFeedback]──→ Disabled / Enabled
 *  Disabled  ──[requestEnable(true)]──→ Enabling + MotorCommand
 *  Enabling  ──[applyFeedback]────────→ Enabled / Disabled
 *  Enabled   ──[requestEnable(false)]─→ Disabling + MotorCommand
 *  Disabling ──[applyFeedback]────────→ Disabled / Enabled
 *
 * m_enabled 废除，由 m_status 统一管理：
 *  - isEnabled() → m_status == Enabled
 *  - isSynchronized() → m_status != NotSynchronized
 */
struct MotorCommand {
    bool enable;  // true = 使能, false = 掉电
};

class GantryMotorController {
public:
    enum class Status {
        NotSynchronized,  // 上电后尚未收到任何 PLC 反馈
        Disabled,         // 已同步，电机掉电
        Enabling,         // 已下发使能命令，等待 PLC 确认
        Enabled,          // 电机已使能（PLC 反馈确认）
        Disabling         // 已下发掉电命令，等待 PLC 确认
    };

    // ========== 意图生成 ==========

    /**
     * @brief 请求使能/掉电龙门电机
     * @return None            成功
     *         NotSynchronized 尚未收到第一帧 PLC 反馈
     *         StateConflict   转换进行中（已在 Enabling / Disabling），拒绝操作
     */
    GantryRejection requestEnable(bool active) {
        if (m_status == Status::NotSynchronized) return GantryRejection::NotSynchronized;
        if (m_status == Status::Enabling || m_status == Status::Disabling)
            return GantryRejection::StateConflict;

        if (active && m_status == Status::Enabled)  return GantryRejection::None;  // 幂等
        if (!active && m_status == Status::Disabled) return GantryRejection::None;  // 幂等

        m_pending_command = MotorCommand{active};
        m_status = active ? Status::Enabling : Status::Disabling;
        return GantryRejection::None;
    }

    // ========== 状态查询 ==========

    Status status() const { return m_status; }

    bool isEnabled() const        { return m_status == Status::Enabled; }
    bool isSynchronized() const   { return m_status != Status::NotSynchronized; }
    bool isNotSynchronized() const { return m_status == Status::NotSynchronized; }

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
     *        首次反馈从 NotSynchronized 迁出；
     *        后续反馈如实反映物理真相，可能将 Enabling→Disabled（PLC 未确认）等。
     */
    void applyFeedback(const GantryFeedback& feedback) {
        m_status = feedback.enable ? Status::Enabled : Status::Disabled;
    }

private:
    Status m_status = Status::NotSynchronized;
    std::optional<MotorCommand> m_pending_command;
};
