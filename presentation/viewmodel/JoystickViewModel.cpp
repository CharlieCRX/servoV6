#include "JoystickViewModel.h"
#include "QtAxisViewModel.h"
#include <algorithm>

// =============================================================================
// 构造 / 析构
// =============================================================================

JoystickViewModel::JoystickViewModel(std::unique_ptr<IJoystickDriver> driver)
    : m_driver(std::move(driver))
{
}

JoystickViewModel::~JoystickViewModel() = default;

// =============================================================================
// 绑定目标轴 ViewModel
// =============================================================================

void JoystickViewModel::bindAxisViewModel(QtAxisViewModel* axisVM)
{
    m_boundAxisVM = axisVM;
}

void JoystickViewModel::unbindAxisViewModel()
{
    m_boundAxisVM = nullptr;
}

// =============================================================================
// UI 模式同步
// =============================================================================

void JoystickViewModel::setUIMode(int mode)
{
    if (mode == 1) {
        m_policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    } else {
        m_policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    }
}

int JoystickViewModel::uiMode() const
{
    return (m_policy.uiMode() == JoystickControlPolicy::UIMode::Position) ? 1 : 0;
}

// =============================================================================
// 步进距离配置
// =============================================================================

void JoystickViewModel::setStepDistance(double mm)
{
    m_policy.setStepDistance(mm);
}

double JoystickViewModel::stepDistance() const
{
    return m_policy.stepDistance();
}

// =============================================================================
// 死区配置
// =============================================================================

void JoystickViewModel::setDeadzone(float dz)
{
    m_deadzone = std::clamp(dz, 0.0f, 0.5f);
    m_driver->setDeadzone(m_deadzone);
}

float JoystickViewModel::deadzone() const
{
    return m_deadzone;
}

// =============================================================================
// 启用/禁用（弹窗控制）
// =============================================================================

void JoystickViewModel::setEnabled(bool enabled)
{
    m_enabled = enabled;
}

bool JoystickViewModel::isEnabled() const
{
    return m_enabled;
}

// =============================================================================
// 状态投影
// =============================================================================

bool JoystickViewModel::isConnected() const
{
    return m_driver->isConnected();
}

int JoystickViewModel::deviceCount() const
{
    return m_driver->deviceCount();
}

JoystickDirection JoystickViewModel::currentDirection() const
{
    return m_lastDirection;
}

// =============================================================================
// Policy 回调绑定
// =============================================================================

JoystickControlPolicy::ActionCallbacks JoystickViewModel::createCallbacks()
{
    JoystickControlPolicy::ActionCallbacks cb;

    // ── 点动模式回调 → 透传给 AxisViewModel ──
    cb.onJogPositivePressed = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogPositivePressed();
        }
    };
    cb.onJogPositiveReleased = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogPositiveReleased();
        }
    };
    cb.onJogNegativePressed = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogNegativePressed();
        }
    };
    cb.onJogNegativeReleased = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->jogNegativeReleased();
        }
    };

    // ── 定位模式回调 → setRelTarget + triggerRelMove ──
    cb.onPositionMoveRequested = [this](double distance) -> bool {
        if (!m_boundAxisVM) return false;

        // 安全检查：如果轴不在空闲状态，拒绝定位请求
        if (m_boundAxisVM->isLoading()) {
            return false;
        }
        if (m_boundAxisVM->hasBlockingError()) {
            return false;
        }

        // 两步操作
        bool ok = m_boundAxisVM->setRelTarget(distance);
        if (ok) {
            m_boundAxisVM->triggerRelMove();
        }
        return ok;
    };

    // ── 绝对定位 GO（预留：可映射到特定摇杆按键）──
    cb.onAbsMoveTriggered = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->triggerAbsMove();
        }
    };

    // ── 急停 ──
    cb.onStop = [this]() {
        if (m_boundAxisVM) {
            m_boundAxisVM->stop();
        }
    };

    return cb;
}

// =============================================================================
// 帧驱动
// =============================================================================

void JoystickViewModel::tick()
{
    // 弹窗打开时跳过处理
    if (!m_enabled) {
        return;
    }

    // Step 1: 轮询摇杆硬件状态
    auto state = m_driver->pollState();

    // Step 2: 策略层处理（方向判定 + 跨轴保护 + 模式分发）
    m_policy.tick(state, createCallbacks());

    // Step 3: 缓存更新（供 QML 属性绑定使用）
    m_lastConnected = state.connected;
    m_lastDirection = state.direction;
}