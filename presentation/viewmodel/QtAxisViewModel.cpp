#include "QtAxisViewModel.h"
#include <QString>
#include <QVariantMap>

// =============================================================================
// 构造
// =============================================================================

QtAxisViewModel::QtAxisViewModel(AxisViewModelCore* core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
    if (m_core) {
        m_lastState = m_core->state();
        m_lastAbsPos = m_core->absPos();
        m_lastRelPos = m_core->relPos();
        m_lastJogVelocity  = m_core->jogVelocity();
        m_lastMoveVelocity = m_core->moveVelocity();
    }
}

// =============================================================================
// Getters（透传 Core）
// =============================================================================

int QtAxisViewModel::state() const       { return m_core ? static_cast<int>(m_core->state()) : 0; }
double QtAxisViewModel::absPos() const   { return m_core ? m_core->absPos() : 0.0; }
double QtAxisViewModel::relPos() const   { return m_core ? m_core->relPos() : 0.0; }
double QtAxisViewModel::posLimit() const { return m_core ? m_core->posLimit() : 0.0; }
double QtAxisViewModel::negLimit() const { return m_core ? m_core->negLimit() : 0.0; }
double QtAxisViewModel::jogVelocity() const  { return m_core ? m_core->jogVelocity()  : 0.0; }
double QtAxisViewModel::moveVelocity() const { return m_core ? m_core->moveVelocity() : 0.0; }

bool QtAxisViewModel::hasError() const { return m_core ? m_core->hasError() : false; }

QString QtAxisViewModel::errorCode() const {
    if (!m_core || !m_core->hasError()) return {};
    return QString::fromStdString(m_core->lastError().code);
}

QString QtAxisViewModel::errorMessage() const {
    if (!m_core || !m_core->hasError()) return {};
    return QString::fromStdString(m_core->lastError().userMessage);
}

// ⭐ 新增 Getters

bool QtAxisViewModel::isEnabled() const {
    return m_core ? m_core->isEnabled() : false;
}

QString QtAxisViewModel::stateText() const {
    if (!m_core) return "Unavailable";
    switch (m_core->state()) {
    case AxisState::Unknown:        return "Unknown";
    case AxisState::Disabled:       return "Disabled";
    case AxisState::Idle:           return "Standstill";
    case AxisState::Jogging:        return "Jogging";
    case AxisState::MovingAbsolute: return "MovingAbsolute";
    case AxisState::MovingRelative: return "MovingRelative";
    case AxisState::Error:          return "Error";
    default:                        return "Unknown";
    }
}

QString QtAxisViewModel::errorCategory() const {
    if (!m_core || !m_core->hasError()) return "None";
    auto err = m_core->lastError();
    switch (err.category) {
    case ErrorCategory::Inline:  return "Inline";
    case ErrorCategory::Modal:   return "Modal";
    case ErrorCategory::Silent:  return "Silent";
    default:                     return "Unknown";
    }
}

int QtAxisViewModel::errorCount() const {
    return m_core ? static_cast<int>(m_core->errorCount()) : 0;
}

// =============================================================================
// P0: 使能/去使能
// =============================================================================

void QtAxisViewModel::enable() {
    if (m_core) m_core->enable(true);
}

void QtAxisViewModel::disable() {
    if (m_core) m_core->disable();
}

// =============================================================================
// 控制输入
// =============================================================================

void QtAxisViewModel::jogPositivePressed() {
    if (m_core) m_core->jog(Direction::Forward);
}

void QtAxisViewModel::jogPositiveReleased() {
    if (m_core) m_core->jogStop(Direction::Forward);
}

void QtAxisViewModel::jogNegativePressed() {
    if (m_core) m_core->jog(Direction::Backward);
}

void QtAxisViewModel::jogNegativeReleased() {
    if (m_core) m_core->jogStop(Direction::Backward);
}

void QtAxisViewModel::moveAbsolute(double targetPos) {
    if (m_core) m_core->moveAbsolute(targetPos);
}

void QtAxisViewModel::moveRelative(double distance) {
    if (m_core) m_core->moveRelative(distance);
}

void QtAxisViewModel::setJogVelocity(double v) {
    if (m_core) m_core->setJogVelocity(v);
}

void QtAxisViewModel::setMoveVelocity(double v) {
    if (m_core) m_core->setMoveVelocity(v);
}

void QtAxisViewModel::stop() {
    if (m_core) m_core->stop();
}

// =============================================================================
// 零位操作
// =============================================================================

void QtAxisViewModel::zeroAbsolutePosition() {
    if (m_core) m_core->zeroAbsolutePosition();
}

void QtAxisViewModel::setRelativeZero() {
    if (m_core) m_core->setRelativeZero();
}

void QtAxisViewModel::clearRelativeZero() {
    if (m_core) m_core->clearRelativeZero();
}

// =============================================================================
// ⭐ 错误管理（新增确认能力）
// =============================================================================

void QtAxisViewModel::clearError() {
    if (m_core) m_core->clearError();
}

QVariantList QtAxisViewModel::getAllErrors() const {
    QVariantList list;
    if (!m_core) return list;

    auto errors = m_core->allErrors();
    for (const auto& e : errors) {
        QVariantMap map;
        map["code"]     = QString::fromStdString(e.code);
        map["message"]  = QString::fromStdString(e.userMessage);
        map["category"] = [&]() -> QString {
            switch (e.category) {
            case ErrorCategory::Inline:  return "Inline";
            case ErrorCategory::Modal:   return "Modal";
            case ErrorCategory::Silent:  return "Silent";
            default:                     return "Unknown";
            }
        }();
        map["debug"] = QString::fromStdString(e.debugMessage);
        list.append(map);
    }
    return list;
}

void QtAxisViewModel::acknowledgeError(int index) {
    if (m_core) m_core->acknowledgeError(static_cast<size_t>(index));
}

void QtAxisViewModel::acknowledgeAllErrors() {
    if (m_core) m_core->clearAllErrors();
}

// =============================================================================
// Tick：驱动 Core 状态机 + 节流信号发送（包含新增 property）
// =============================================================================

void QtAxisViewModel::tick() {
    if (!m_core) return;

    // 1. 驱动底层状态机
    m_core->tick();

    // 2. 缓存对比 → 按需 emit
    bool emitState  = false;
    bool emitPos    = false;
    bool emitLimit  = false;
    bool emitVel    = false;
    bool emitError  = false;
    bool emitErCnt  = false;

    // State + isEnabled + stateText
    if (m_lastState != m_core->state()) {
        m_lastState = m_core->state();
        emitState = true;
    }

    // Position
    double newAbsPos = m_core->absPos();
    double newRelPos = m_core->relPos();
    if (std::abs(m_lastAbsPos - newAbsPos) > EPSILON ||
        std::abs(m_lastRelPos - newRelPos) > EPSILON) {
        m_lastAbsPos = newAbsPos;
        m_lastRelPos = newRelPos;
        emitPos = true;
    }

    // Limits（不做值变化检测，跟随 stateChanged）
    // （若极限仅通过 Axis 更新，可不单独 emit；这里保持跟随 stateChanged）

    // Velocity
    double newJogVelocity = m_core->jogVelocity();
    double newMoveVelocity = m_core->moveVelocity();
    if (std::abs(m_lastJogVelocity - newJogVelocity) > EPSILON ||
        std::abs(m_lastMoveVelocity - newMoveVelocity) > EPSILON) {
        m_lastJogVelocity  = newJogVelocity;
        m_lastMoveVelocity = newMoveVelocity;
        emitVel = true;
    }

    // Error
    bool newHasError = m_core->hasError();
    int  newErrorCount = static_cast<int>(m_core->errorCount());
    if (newHasError != m_lastHasError || newErrorCount != m_lastErrorCount) {
        m_lastHasError  = newHasError;
        m_lastErrorCount = newErrorCount;
        emitError = true;

        if (newHasError) {
            m_lastErrorCode    = QString::fromStdString(m_core->lastError().code);
            m_lastErrorMessage = QString::fromStdString(m_core->lastError().userMessage);
        } else {
            m_lastErrorCode.clear();
            m_lastErrorMessage.clear();
        }
    }

    if (newErrorCount != m_lastErrorCount) {
        m_lastErrorCount = newErrorCount;
        emitErCnt = true;
    }

    // 3. Emit（合并同类信号，避免冗余 notify）
    if (emitState) {
        emit stateChanged();
        // isEnabled, stateText 绑定到 stateChanged（无需独立信号）
    }
    if (emitPos) {
        emit absPosChanged();
        emit relPosChanged();
    }
    if (emitState || emitPos) {
        emit limitsChanged();
    }
    if (emitVel) {
        emit velocityChanged();
    }
    if (emitError) {
        emit errorChanged();
    }
    if (emitErCnt) {
        emit errorCountChanged();
    }
}
