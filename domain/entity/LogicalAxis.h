#pragma once

#include "../value/GantryPosition.h"
#include "../value/MotionDirection.h"
#include "Axis.h"           // AxisState
#include <string>

/**
 * @file LogicalAxis.h
 * @brief 逻辑龙门轴实体 (X)
 *
 * 职责：
 *   - 表示龙门逻辑整体 X
 *   - 持有聚合后的逻辑位置 GantryPosition
 *   - 持有聚合后的状态（Idle / Moving / Error / Limit）
 *   - 持有统一命令槽（约束 19：运动互斥）
 *
 * 关键设计决策：
 *   - 不持有 PhysicalAxis 引用：LogicalAxis 是"视图"，
 *     数据由 GantrySystem 在每次操作时通过聚合计算后注入
 *   - 命令槽互斥：同一时刻只能有一个活跃意图
 *
 * 约束映射：
 *   position()                → 约束 8, 10 (逻辑位置定义)
 *   aggregatedState()         → 约束 20 (状态聚合)
 *   canAcceptCommand()        → 约束 19 (运动互斥)
 */

class LogicalAxis {
public:
    // ═══════════════════════════════════
    // 聚合运动类型枚举
    // ═══════════════════════════════════
    enum class AggregatedMotion {
        Idle,
        Jogging,
        MovingAbsolute,
        MovingRelative
    };

    // ═══════════════════════════════════
    // 命令槽类型
    // ═══════════════════════════════════
    enum class CommandType {
        None,
        Jog,
        MoveAbsolute,
        MoveRelative,
        Stop
    };

    /**
     * @brief 命令槽数据结构
     */
    struct CommandSlot {
        CommandType type = CommandType::None;
        MotionDirection jogDirection = MotionDirection::Forward;
        double moveTarget = 0.0;        // MoveAbsolute 用
        double moveDelta = 0.0;         // MoveRelative 用
    };

    LogicalAxis() = default;

    // ═══════════════════════════════════
    // 位置
    // ═══════════════════════════════════

    /// 逻辑位置 (约束 8, 10: X.position = X1.pos)
    GantryPosition position() const { return m_position; }

    /// 由 GantrySystem 计算后注入
    void setPosition(GantryPosition pos) { m_position = pos; }

    // ═══════════════════════════════════
    // 聚合状态 (约束 20)
    // ═══════════════════════════════════

    /// 聚合后的轴状态（优先级: Alarm > Limit > Moving > Idle）
    AxisState aggregatedState() const { return m_aggregatedState; }

    /// 聚合后的运动类型
    AggregatedMotion motion() const { return m_motion; }

    /// 是否正在运动中
    bool isMoving() const { return m_motion != AggregatedMotion::Idle; }

    /// 是否有报警
    bool isError() const { return m_aggregatedState == AxisState::Error; }

    /// 是否有活跃限位
    bool hasActiveLimit() const { return m_hasActiveLimit; }

    // ═══════════════════════════════════
    // 状态注入（由 GantrySystem 每周期聚合后调用）
    // ═══════════════════════════════════

    /**
     * @brief 由 GantrySystem 在 aggregateState() 中调用，
     *        注入计算好的聚合状态
     */
    void applyAggregatedState(
        AxisState state,
        AggregatedMotion motion,
        GantryPosition pos,
        bool anyLimit
    ) {
        m_aggregatedState = state;
        m_motion = motion;
        m_position = pos;
        m_hasActiveLimit = anyLimit;
    }

    // ═══════════════════════════════════
    // 命令槽 (约束 19：运动互斥)
    // ═══════════════════════════════════

    /// 命令槽是否空闲（可以接受新命令）
    /// Stop 不占用命令槽（Stop 是瞬时命令，不应阻塞后续运动）
    bool canAcceptCommand() const {
        return m_commandSlot.type == CommandType::None ||
               m_commandSlot.type == CommandType::Stop;
    }

    /**
     * @brief 尝试接受 Jog 命令
     * @param dir 逻辑方向
     * @return 空字符串 = 成功；否则返回拒绝原因
     */
    std::string tryAcceptJog(MotionDirection dir) {
        if (!canAcceptCommand()) return "Slot: command slot is busy";
        m_commandSlot.type = CommandType::Jog;
        m_commandSlot.jogDirection = dir;
        return "";
    }

    /**
     * @brief 尝试接受 MoveAbsolute 命令
     * @param target 绝对目标位置 (mm)
     * @return 空字符串 = 成功；否则返回拒绝原因
     */
    std::string tryAcceptMoveAbsolute(double target) {
        if (!canAcceptCommand()) return "Slot: command slot is busy";
        m_commandSlot.type = CommandType::MoveAbsolute;
        m_commandSlot.moveTarget = target;
        return "";
    }

    /**
     * @brief 尝试接受 MoveRelative 命令
     * @param delta 相对位移量 (mm)
     * @return 空字符串 = 成功；否则返回拒绝原因
     */
    std::string tryAcceptMoveRelative(double delta) {
        if (!canAcceptCommand()) return "Slot: command slot is busy";
        m_commandSlot.type = CommandType::MoveRelative;
        m_commandSlot.moveDelta = delta;
        return "";
    }

    /**
     * @brief 尝试接受停止命令
     * @return 始终成功（停止命令不受命令槽限制）
     */
    std::string tryAcceptStop() {
        m_commandSlot.type = CommandType::Stop;
        return "";
    }

    /// 清除当前命令（命令完成/中断后调用）
    void clearCommand() {
        m_commandSlot = CommandSlot{};
    }

    /// 查看当前待处理命令
    const CommandSlot& pendingCommand() const { return m_commandSlot; }

private:
    GantryPosition m_position{0.0};
    AxisState m_aggregatedState = AxisState::Idle;
    AggregatedMotion m_motion = AggregatedMotion::Idle;
    bool m_hasActiveLimit = false;
    CommandSlot m_commandSlot;
};
