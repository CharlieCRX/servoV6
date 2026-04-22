#include "AxisViewModelCore.h"

AxisViewModelCore::AxisViewModelCore(Axis& axis, 
                                     JogOrchestrator& jogOrch, 
                                     AutoAbsMoveOrchestrator& absOrch, 
                                     StopAxisUseCase& stopUc)
    : m_axis(axis), 
      m_jogOrch(jogOrch), 
      m_absOrch(absOrch), 
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

void AxisViewModelCore::jogPositivePressed() {
    m_jogOrch.startJog(Direction::Forward);
}

void AxisViewModelCore::jogPositiveReleased() {
    m_jogOrch.stopJog(Direction::Forward);
}

void AxisViewModelCore::tick() {
    // 系统唯一推进入口：驱动所有的策略器
    m_jogOrch.update(m_axis);
    m_absOrch.update(m_axis);
}