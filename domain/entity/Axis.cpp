#include "Axis.h"
#include <cmath>
Axis::Axis() : m_state(AxisState::Unknown)
{
}

AxisState Axis::state() const
{
    return m_state;
}

void Axis::applyFeedback(const AxisFeedback &feedback)
{
    m_state = feedback.state;
    m_current_abs_pos = feedback.absPos;

    // A. 运动类状态：清理运动意图
    if (m_state == AxisState::Jogging ||
        m_state == AxisState::MovingAbsolute ||
        m_state == AxisState::MovingRelative)
    {
        // 只有当前存的是运动命令时才清理，防止误杀 Stop 指令
        if (std::holds_alternative<JogCommand>(m_pending_intent) ||
            std::holds_alternative<MoveCommand>(m_pending_intent)) {
            m_pending_intent = std::monostate{};
        }
    }

    // B. 静止类状态：清理停止意图 (含 Idle, Disabled, Error)
    if (m_state == AxisState::Idle ||
        m_state == AxisState::Disabled ||
        m_state == AxisState::Error)
    {
        // 如果当前挂起的是 Stop 命令，则视为已完成
        if (std::holds_alternative<StopCommand>(m_pending_intent)) {
            m_pending_intent = std::monostate{};
        }
    }


    // C. 绝对位置清零：根据反馈的绝对位置与零点的接近程度自动清理 ZeroAbsoluteCommand 意图
    if (std::holds_alternative<ZeroAbsoluteCommand>(m_pending_intent)) {
        // 判定公式：|CurrentPos - 0.0| < EPSILON
        if (std::abs(m_current_abs_pos) < POSITION_EPSILON) {
            m_pending_intent = std::monostate{}; 
        }
    }
}

bool Axis::jog(Direction dir)
{
    if (m_state != AxisState::Idle) {
        return false;
    }

    m_pending_intent = JogCommand{ dir };
    return true;
}

bool Axis::moveAbsolute(double target)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Absolute, target };
    return true;
}

bool Axis::moveRelative(double distance)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Relative, distance };
    return true;
}
bool Axis::stop()
{
    m_pending_intent = StopCommand{};

    return true;
}

bool Axis::zeroAbsolutePosition()
{
    // 只有在静止态（Idle/Disabled）才允许修改基准
    if (m_state == AxisState::Idle ||
        m_state == AxisState::Disabled)
    {
        m_pending_intent = ZeroAbsoluteCommand{};
        return true;
    }
    return false;
}

double Axis::currentAbsolutePosition() const
{
  return m_current_abs_pos;
}


bool Axis::hasPendingCommand() const
{
    // 语义：只要不是 monostate，就代表有运动类命令挂起
    return !std::holds_alternative<std::monostate>(m_pending_intent);
}


const AxisCommand &Axis::getPendingCommand() const
{
  return m_pending_intent;
}

bool Axis::hasPendingStop() const
{
    return std::holds_alternative<StopCommand>(m_pending_intent);
}
