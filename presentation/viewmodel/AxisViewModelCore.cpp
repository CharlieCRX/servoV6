#include "AxisViewModelCore.h"

AxisViewModelCore::AxisViewModelCore(Axis& axis, 
                                     JogOrchestrator& jogOrch, 
                                     AutoAbsMoveOrchestrator& absOrch, 
                                     AutoRelMoveOrchestrator& relOrch,
                                     StopAxisUseCase& stopUc)
    : m_axis(axis), 
      m_jogOrch(jogOrch), 
      m_absOrch(absOrch), 
      m_relOrch(relOrch),
      m_stopUc(stopUc) 
{
}

AxisState AxisViewModelCore::state() const {
    // 最小实现：直接透传底层状态，绝不自己缓存
    return m_axis.state();
}

double AxisViewModelCore::absPos() const {
    return m_axis.currentAbsolutePosition();
}

double AxisViewModelCore::relPos() const
{
    return m_axis.currentRelativePosition();
}

bool AxisViewModelCore::isEnabled() const
{
    return m_axis.state() != AxisState::Disabled;
}

double AxisViewModelCore::jogVelocity() const { return m_axis.getjogVelocity(); }
double AxisViewModelCore::moveVelocity() const { return m_axis.getMoveVelocity(); }

void AxisViewModelCore::jogPositivePressed() {
    m_jogOrch.startJog(Direction::Forward);
}

void AxisViewModelCore::jogPositiveReleased() {
    m_jogOrch.stopJog(Direction::Forward);
}

void AxisViewModelCore::jogNegativePressed()
{
    m_jogOrch.startJog(Direction::Backward);
}

void AxisViewModelCore::jogNegativeReleased() {
    m_jogOrch.stopJog(Direction::Backward);
}

void AxisViewModelCore::moveAbsolute(double targetPos)
{
    m_absOrch.start(targetPos);
}

void AxisViewModelCore::moveRelative(double distance)
{
    m_relOrch.start(distance);
}

void AxisViewModelCore::stop()
{
    m_stopUc.execute(m_axis);
}

void AxisViewModelCore::setJogVelocity(double v) { m_axis.setJogVelocity(v); }
void AxisViewModelCore::setMoveVelocity(double v) { m_axis.setMoveVelocity(v); }

void AxisViewModelCore::tick() {
    // 系统唯一推进入口：驱动所有的策略器
    m_jogOrch.update(m_axis);
    m_absOrch.update(m_axis);
    m_relOrch.update(m_axis);
}