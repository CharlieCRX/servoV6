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
    m_state = feedback.state;
}
