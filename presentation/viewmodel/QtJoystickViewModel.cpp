#include "QtJoystickViewModel.h"
#include "JoystickViewModel.h"
#include "QtAxisViewModel.h"

// =============================================================================
// 构造
// =============================================================================

QtJoystickViewModel::QtJoystickViewModel(JoystickViewModel* core, QObject* parent)
    : QObject(parent)
    , m_core(core)
{
}

// =============================================================================
// Getters（透传 Core）
// =============================================================================

bool QtJoystickViewModel::connected() const
{
    return m_core ? m_core->isConnected() : false;
}

int QtJoystickViewModel::deviceCount() const
{
    return m_core ? m_core->deviceCount() : 0;
}

int QtJoystickViewModel::uiMode() const
{
    return m_core ? m_core->uiMode() : 0;
}

double QtJoystickViewModel::stepDistance() const
{
    return m_core ? m_core->stepDistance() : 1.0;
}

float QtJoystickViewModel::deadzone() const
{
    return m_core ? m_core->deadzone() : 0.15f;
}

bool QtJoystickViewModel::isEnabled() const
{
    return m_core ? m_core->isEnabled() : true;
}

int QtJoystickViewModel::currentDirection() const
{
    return m_lastDirection;
}

QString QtJoystickViewModel::directionText() const
{
    if (!m_core) return QStringLiteral("无");

    auto dir = m_core->currentDirection();
    switch (dir) {
        case JoystickDirection::Neutral:       return QStringLiteral("回中");
        case JoystickDirection::Forward:       return QStringLiteral("前进");
        case JoystickDirection::Backward:      return QStringLiteral("后退");
        case JoystickDirection::Left:          return QStringLiteral("左");
        case JoystickDirection::Right:         return QStringLiteral("右");
        case JoystickDirection::ForwardLeft:   return QStringLiteral("前左");
        case JoystickDirection::ForwardRight:  return QStringLiteral("前右");
        case JoystickDirection::BackwardLeft:  return QStringLiteral("后左");
        case JoystickDirection::BackwardRight: return QStringLiteral("后右");
        default:                                return QStringLiteral("未知");
    }
}

// =============================================================================
// Setters（透传 Core + 发射信号）
// =============================================================================

void QtJoystickViewModel::setUIMode(int mode)
{
    if (m_core) {
        m_core->setUIMode(mode);
    }
    if (m_lastUIMode != mode) {
        m_lastUIMode = mode;
        emit uiModeChanged();
    }
}

void QtJoystickViewModel::setStepDistance(double mm)
{
    if (m_core) {
        m_core->setStepDistance(mm);
    }
    if (std::abs(m_lastStepDistance - mm) > 0.001) {
        m_lastStepDistance = mm;
        emit stepDistanceChanged();
    }
}

void QtJoystickViewModel::setDeadzone(float dz)
{
    if (m_core) {
        m_core->setDeadzone(dz);
    }
    if (std::abs(m_lastDeadzone - dz) > 0.001f) {
        m_lastDeadzone = dz;
        emit deadzoneChanged();
    }
}

void QtJoystickViewModel::setEnabled(bool enabled)
{
    if (m_core) {
        m_core->setEnabled(enabled);
    }
    if (m_lastEnabled != enabled) {
        m_lastEnabled = enabled;
        emit enabledChanged();
    }
}

// =============================================================================
// UI 信息注入（由 QML 层主动调用）
// =============================================================================

void QtJoystickViewModel::bindToAxis(QtAxisViewModel* axisVM)
{
    if (m_core) {
        m_core->bindAxisViewModel(axisVM);
    }
}

void QtJoystickViewModel::unbindAxis()
{
    if (m_core) {
        m_core->unbindAxisViewModel();
    }
}

// =============================================================================
// 帧驱动
// =============================================================================

void QtJoystickViewModel::tick()
{
    if (!m_core) return;

    m_core->tick();

    // ── 缓存节流：只在值变化时发射信号 ──

    bool connected = m_core->isConnected();
    if (m_lastConnected != connected) {
        m_lastConnected = connected;
        emit connectedChanged();
    }

    int devCount = m_core->deviceCount();
    if (m_lastDeviceCount != devCount) {
        m_lastDeviceCount = devCount;
        emit deviceCountChanged();
    }

    int uiMode = m_core->uiMode();
    if (m_lastUIMode != uiMode) {
        m_lastUIMode = uiMode;
        emit uiModeChanged();
    }

    double stepDist = m_core->stepDistance();
    if (std::abs(m_lastStepDistance - stepDist) > 0.001) {
        m_lastStepDistance = stepDist;
        emit stepDistanceChanged();
    }

    float dz = m_core->deadzone();
    if (std::abs(m_lastDeadzone - dz) > 0.001f) {
        m_lastDeadzone = dz;
        emit deadzoneChanged();
    }

    bool enabled = m_core->isEnabled();
    if (m_lastEnabled != enabled) {
        m_lastEnabled = enabled;
        emit enabledChanged();
    }

    int dirCode = static_cast<int>(m_core->currentDirection());
    if (m_lastDirection != dirCode) {
        m_lastDirection = dirCode;
        emit directionChanged();
    }
}