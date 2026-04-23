#include "QtAxisViewModel.h"
#include <cmath>

QtAxisViewModel::QtAxisViewModel(AxisViewModelCore* core, QObject *parent)
    : QObject(parent), m_core(core) 
{
    m_lastState = m_core->state();
    m_lastAbsPos = m_core->absPos();
}

int QtAxisViewModel::state() const { return static_cast<int>(m_core->state()); }
double QtAxisViewModel::absPos() const { return m_core->absPos(); }

void QtAxisViewModel::jogPositivePressed() { m_core->jogPositivePressed(); }
void QtAxisViewModel::jogPositiveReleased() { m_core->jogPositiveReleased(); }
void QtAxisViewModel::jogNegativePressed() { m_core->jogNegativePressed(); }
void QtAxisViewModel::jogNegativeReleased() { m_core->jogNegativeReleased(); }
void QtAxisViewModel::moveAbsolute(double pos) { m_core->moveAbsolute(pos); }
void QtAxisViewModel::moveRelative(double distance) { m_core->moveRelative(distance); }
void QtAxisViewModel::setJogVelocity(double v) { m_core->setJogVelocity(v); }
void QtAxisViewModel::setMoveVelocity(double v) { m_core->setMoveVelocity(v); }
void QtAxisViewModel::stop() { m_core->stop(); }

void QtAxisViewModel::tick() {
    m_core->tick();

    AxisState currentState = m_core->state();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        emit stateChanged();
    }

    double currentPos = m_core->absPos();
    if (std::abs(currentPos - m_lastAbsPos) > EPSILON) {
        m_lastAbsPos = currentPos;
        emit absPosChanged();
    }
}