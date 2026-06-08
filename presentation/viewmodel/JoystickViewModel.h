#pragma once

#include "application/joystick/IJoystickDriver.h"
#include "application/policy/JoystickControlPolicy.h"
#include <memory>
#include <string>

class QtAxisViewModel;  // 前向声明

/**
 * @brief 摇杆 ViewModel 纯逻辑核心
 *
 * 职责：
 *   1. 持有 IJoystickDriver 和 JoystickControlPolicy
 *   2. 桥接 Policy 回调到目标 AxisViewModel
 *   3. tick() 驱动摇杆轮询 + Policy 分发
 *   4. 提供状态投影（连接状态、当前方向、死区等）
 *
 * 设计原则：
 *   - 不直接依赖 Qt / QML
 *   - 通过 std::function 回调与 AxisViewModel 解耦
 *   - 所有状态由外部 tick() 驱动
 */
class JoystickViewModel {
public:
    explicit JoystickViewModel(std::unique_ptr<IJoystickDriver> driver);
    ~JoystickViewModel();

    // ── 绑定目标轴 ViewModel ──
    void bindAxisViewModel(QtAxisViewModel* axisVM);
    void unbindAxisViewModel();

    // ── UI 模式同步 ──
    void setUIMode(int mode);  // 0=点动, 1=定位
    int  uiMode() const;

    // ── 步进距离配置 ──
    void setStepDistance(double mm);
    double stepDistance() const;

    // ── 死区配置 ──
    void setDeadzone(float dz);
    float deadzone() const;

    // ── 启用/禁用（弹窗控制）──
    void setEnabled(bool enabled);
    bool isEnabled() const;

    // ── 状态投影 ──
    bool isConnected() const;
    int  deviceCount() const;
    JoystickDirection currentDirection() const;

    // ── 帧驱动（由外部定时器调用）──
    void tick();

private:
    std::unique_ptr<IJoystickDriver> m_driver;
    JoystickControlPolicy m_policy;
    QtAxisViewModel* m_boundAxisVM = nullptr;
    bool m_enabled = true;
    float m_deadzone = 0.15f;  // 默认死区 15%

    // ── 缓存 ──
    bool m_lastConnected = false;
    JoystickDirection m_lastDirection = JoystickDirection::Neutral;

    // ── Policy 回调绑定 ──
    JoystickControlPolicy::ActionCallbacks createCallbacks();
};