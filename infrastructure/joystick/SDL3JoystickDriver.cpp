#include "SDL3JoystickDriver.h"
#include <cmath>
#include <algorithm>

SDL3JoystickDriver::SDL3JoystickDriver(std::unique_ptr<ISDLJoystickWrapper> wrapper)
    : m_wrapper(std::move(wrapper))
{
    m_wrapper->init();
    m_wrapper->setConnectionCallback([this](SDL_JoystickID id, bool connected) {
        if (connected) {
            m_wrapper->openGamepad(id);
            m_wrapper->setAxisDeadzone(id, m_deadzone);
            m_activeDeviceId = id;
            m_connected = true;
        } else if (id == m_activeDeviceId) {
            m_wrapper->closeGamepad(id);
            m_connected = false;
            m_activeDeviceId = -1;
        }
    });
}

SDL3JoystickDriver::~SDL3JoystickDriver() = default;

JoystickState SDL3JoystickDriver::pollState()
{
    // 驱动 SDL 事件循环（热插拔检测 + 输入更新）
    m_wrapper->pumpEvents();

    JoystickState state;
    if (!m_connected || m_activeDeviceId < 0) {
        return state;  // 返回默认状态（全部为 Neutral / 0.0）
    }

    // 读取 4 轴值（归一化 [-1.0, 1.0]）
    float lx = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_LEFTX);
    float ly = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_LEFTY);
    float rx = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_RIGHTX);
    float ry = m_wrapper->getAxis(m_activeDeviceId, SDL_GAMEPAD_AXIS_RIGHTY);

    state.leftX  = lx;
    state.leftY  = ly;
    state.rightX = rx;
    state.rightY = ry;

    // 各轴独立方向
    state.leftXDir  = axisDirection(lx, m_deadzone, JoystickDirection::Right,  JoystickDirection::Left);
    state.leftYDir  = axisDirection(ly, m_deadzone, JoystickDirection::Forward, JoystickDirection::Backward);
    state.rightXDir = axisDirection(rx, m_deadzone, JoystickDirection::Right,  JoystickDirection::Left);
    state.rightYDir = axisDirection(ry, m_deadzone, JoystickDirection::Forward, JoystickDirection::Backward);

    // 综合方向
    state.direction = computeDirection(lx, ly, rx, ry, m_deadzone);
    state.connected = true;

    return state;
}

bool SDL3JoystickDriver::isConnected() const
{
    return m_connected;
}

int SDL3JoystickDriver::deviceCount() const
{
    return m_wrapper->gamepadCount();
}

void SDL3JoystickDriver::setDeadzone(float deadzone)
{
    if (deadzone < 0.0f) deadzone = 0.0f;
    if (deadzone > 0.5f) deadzone = 0.5f;
    m_deadzone = deadzone;

    if (m_activeDeviceId >= 0) {
        m_wrapper->setAxisDeadzone(m_activeDeviceId, m_deadzone);
    }
}

void SDL3JoystickDriver::enableHaptic(bool /*enable*/)
{
    // 预留：力反馈开关，Phase 5 实现
}

JoystickDirection SDL3JoystickDriver::axisDirection(
    float value, float deadzone,
    JoystickDirection positiveDir, JoystickDirection negativeDir)
{
    if (std::abs(value) <= deadzone) {
        return JoystickDirection::Neutral;
    }
    return (value > 0) ? positiveDir : negativeDir;
}

JoystickDirection SDL3JoystickDriver::computeDirection(
    float lx, float ly, float rx, float ry, float deadzone)
{
    // 逐个轴检查（左右摇杆独立判定，以绝对值最大的轴为准）
    struct AxisCandidate {
        float value;
        JoystickDirection positive;  // 正值方向
        JoystickDirection negative;  // 负值方向
    };

    // 水平轴：正值→右(Forward), 负值→左(Backward)
    // 垂直轴：负值→前(Forward), 正值→后(Backward) — SDL Y轴: 上推为负
    AxisCandidate candidates[] = {
        { lx, JoystickDirection::Right,    JoystickDirection::Left     },
        { ly, JoystickDirection::Backward, JoystickDirection::Forward  },  // SDL: 上推=负→Forward, 下拉=正→Backward
        { rx, JoystickDirection::Right,    JoystickDirection::Left     },
        { ry, JoystickDirection::Backward, JoystickDirection::Forward  },
    };

    // 找出绝对值最大的非死区轴
    float maxAbs = deadzone;
    JoystickDirection best = JoystickDirection::Neutral;

    for (const auto& c : candidates) {
        float absVal = std::abs(c.value);
        if (absVal > maxAbs) {
            maxAbs = absVal;
            best = (c.value > 0) ? c.positive : c.negative;
        }
    }

    return best;  // Neutral if no axis exceeds deadzone
}