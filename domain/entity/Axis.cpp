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
    if (m_state == AxisState::Jogging || m_state == AxisState::Moving) {
        pending_direction_.reset();
    }
}

bool Axis::jog(Direction dir)
{
    if (m_state != AxisState::Idle) {
        return false;
    }

    pending_direction_ = dir;
    return true;
}

bool Axis::hasPendingCommand() const
{
    return pending_direction_.has_value();
}

std::optional<Direction> Axis::pendingDirection() const
{
    return pending_direction_;
}
