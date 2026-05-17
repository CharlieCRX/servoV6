#include "QtAxisViewModel.h"
#include "ViewModelError.h"

#include <QString>
#include <cmath>

QtAxisViewModel::QtAxisViewModel(AxisViewModelCore* core, QObject *parent)
    : QObject(parent), m_core(core) 
{
    m_lastState     = m_core->state();
    m_lastAbsPos    = m_core->absPos();
    m_lastRelPos    = m_core->relPos();
    m_lastHasError  = m_core->hasError();
    if (m_lastHasError) {
        auto e = m_core->lastError();
        m_lastErrorCode     = QString::fromStdString(e.code);
        m_lastErrorMessage  = QString::fromStdString(e.userMessage);
    }
}

// =========================================================================
// 状态投影 getters
// =========================================================================

int QtAxisViewModel::state() const    { return static_cast<int>(m_core->state()); }
double QtAxisViewModel::absPos() const { return m_core->absPos(); }
double QtAxisViewModel::relPos() const { return m_core->relPos(); }
double QtAxisViewModel::posLimit() const { return m_core->posLimit(); }
double QtAxisViewModel::negLimit() const { return m_core->negLimit(); }
double QtAxisViewModel::jogVelocity() const { return m_core->jogVelocity(); }
double QtAxisViewModel::moveVelocity() const { return m_core->moveVelocity(); }

// =========================================================================
// 错误接口 getters
// =========================================================================

bool QtAxisViewModel::hasError() const { return m_core->hasError(); }

QString QtAxisViewModel::errorCode() const
{
    if (m_core->hasError()) {
        return QString::fromStdString(m_core->lastError().code);
    }
    return {};
}

QString QtAxisViewModel::errorMessage() const
{
    if (m_core->hasError()) {
        return QString::fromStdString(m_core->lastError().userMessage);
    }
    return {};
}

// =========================================================================
// 控制输入
// =========================================================================

void QtAxisViewModel::jogPositivePressed()  { m_core->jog(Direction::Forward); }
void QtAxisViewModel::jogPositiveReleased() { m_core->jogStop(Direction::Forward); }
void QtAxisViewModel::jogNegativePressed()  { m_core->jog(Direction::Backward); }
void QtAxisViewModel::jogNegativeReleased() { m_core->jogStop(Direction::Backward); }
void QtAxisViewModel::moveAbsolute(double pos)      { m_core->moveAbsolute(pos); }
void QtAxisViewModel::moveRelative(double distance) { m_core->moveRelative(distance); }
void QtAxisViewModel::setJogVelocity(double v)      { m_core->setJogVelocity(v); }
void QtAxisViewModel::setMoveVelocity(double v)     { m_core->setMoveVelocity(v); }
void QtAxisViewModel::stop()                        { m_core->stop(); }

// =========================================================================
// 零位操作
// =========================================================================

void QtAxisViewModel::zeroAbsolutePosition() { m_core->zeroAbsolutePosition(); }
void QtAxisViewModel::setRelativeZero()      { m_core->setRelativeZero(); }
void QtAxisViewModel::clearRelativeZero()    { m_core->clearRelativeZero(); }

// =========================================================================
// 错误清理
// =========================================================================

void QtAxisViewModel::clearError()
{
    m_core->clearError();
    // 立即推送到 QML
    if (m_lastHasError) {
        m_lastHasError  = false;
        m_lastErrorCode.clear();
        m_lastErrorMessage.clear();
        emit errorChanged();
    }
}

// =========================================================================
// 帧驱动（节流信号发射）
// =========================================================================

void QtAxisViewModel::tick() {
    m_core->tick();

    // --- stateChanged ---
    AxisState currentState = m_core->state();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        emit stateChanged();
    }

    // --- absPosChanged ---
    double currentAbsPos = m_core->absPos();
    if (std::abs(currentAbsPos - m_lastAbsPos) > EPSILON) {
        m_lastAbsPos = currentAbsPos;
        emit absPosChanged();
    }

    // --- relPosChanged ---
    double currentRelPos = m_core->relPos();
    if (std::abs(currentRelPos - m_lastRelPos) > EPSILON) {
        m_lastRelPos = currentRelPos;
        emit relPosChanged();
    }

    // --- errorChanged ---
    bool currentHasError = m_core->hasError();
    if (currentHasError != m_lastHasError) {
        m_lastHasError = currentHasError;
        if (currentHasError) {
            auto e = m_core->lastError();
            m_lastErrorCode    = QString::fromStdString(e.code);
            m_lastErrorMessage = QString::fromStdString(e.userMessage);
        } else {
            m_lastErrorCode.clear();
            m_lastErrorMessage.clear();
        }
        emit errorChanged();
    }
}
