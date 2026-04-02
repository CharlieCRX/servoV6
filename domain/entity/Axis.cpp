#include "Axis.h"
Axis::Axis() : m_state(AxisState::Unknown)
{
}

AxisState Axis::state() const
{
    return m_state;
}

void Axis::applyFeedback(const AxisFeedback &feedback)
{
    // 1. 更新现实状态
    m_state = feedback.state;

    // 2. 同步：如果现实已经进入执行态，消费掉挂起的意图
    if (m_state == AxisState::Jogging || 
        m_state == AxisState::MovingAbsolute || 
        m_state == AxisState::MovingRelative) 
    {
        m_pending_direction.reset();
        m_pending_move_type = MoveType::None;
        m_pending_target.reset();
    }

    // 3. 消费停止意图 (进入静止态时清理)
    // 只要现实告诉我们轴已经停了（无论关闭使能还是报错），停止意图就达成了
    if (m_state == AxisState::Disabled || 
        m_state == AxisState::Error) 
    {
        m_pending_stop = false;
    }
}

bool Axis::jog(Direction dir)
{
    if (m_state != AxisState::Idle) {
        return false;
    }

    m_pending_direction = dir;
    return true;
}

bool Axis::moveAbsolute(double target)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_move_type = MoveType::Absolute;
    m_pending_target = target;
    return true;
}

bool Axis::moveRelative(double distance)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_move_type = MoveType::Relative;
    m_pending_target = distance;
    return true;
}
bool Axis::stop()
{
    // Stop 是特权指令，不需要 Idle 状态，甚至在 Error 状态下也应允许尝试停止
    m_pending_stop = true;

    // 一旦 Stop，清除所有其他正在挂起的移动意图（因为没意义了）
    m_pending_direction.reset();
    m_pending_move_type = MoveType::None;
    m_pending_target.reset();

    return true;
}


/**
 * 插槽名称,            对应变量,                   意图类型
 * Jog  插槽,   m_pending_direction,           点动 (Forward/Backward)
 * Move 插槽,   m_pending_move_type / target,  定位 (Absolute/Relative)
 */
bool Axis::hasPendingCommand() const
{
    return m_pending_direction.has_value() || (m_pending_move_type != MoveType::None);
}

bool Axis::hasPendingStop() const
{
    return m_pending_stop;
}

std::optional<Direction> Axis::pendingDirection() const
{
    return m_pending_direction;
}

MoveType Axis::pendingMoveType() const
{
    return m_pending_move_type;
}

std::optional<double> Axis::pendingTarget() const
{
    return m_pending_target;
}
