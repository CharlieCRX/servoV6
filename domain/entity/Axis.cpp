#include "Axis.h"
Axis::Axis() : m_state(AxisState::Disabled)
{
}

AxisState Axis::state() const
{
    return m_state;
}

void Axis::enable()
{
    if (m_state == AxisState::Disabled) {
        m_state = AxisState::Idle;
    }
}
