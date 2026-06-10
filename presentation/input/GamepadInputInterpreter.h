#pragma once

#include <QObject>
#include "InputEvent.h"

class AndroidGamepadJoystick;  // forward-declare from infrastructure/joystick/

/// @brief 将 HAL 层原始摇杆数据翻译为统一的 InputEvent
/// 负责：死区判断 / 去抖（支持长按自动重复）/ 边缘触发状态机
class GamepadInputInterpreter : public QObject
{
    Q_OBJECT

public:
    explicit GamepadInputInterpreter(QObject* parent = nullptr);

    /// @brief 启动解释器，连接 AndroidGamepadJoystick::changed() 信号
    void start();

signals:
    /// @brief 当有新的输入事件时发出（统一事件总线）
    void inputEvent(const InputEvent& event);

private slots:
    void onGamepadChanged();

private:
    // ── 左摇杆状态 ──
    float m_lastLX = 0.0f;
    float m_lastLY = 0.0f;

    // LX 左右去抖：m_lastLXSelectTime 记录上次触发时间，m_lxWasOutside 记录上一帧是否在死区外
    qint64 m_lastLXSelectTime = 0;
    bool m_lxWasOutside = false;

    // LY 上下推去抖：m_lastLYSelectTime + m_lyWasOutside（与 LX 独立，互不干扰）
    qint64 m_lastLYSelectTime = 0;
    bool m_lyWasOutside = false;

    // ★ 全局轴选择防抖：任意轴触发后，所有轴在去抖时间内都被锁定
    //   解决斜推（LY+LX 同时出死区）时两个轴分别触发导致跳两步的问题
    qint64 m_lastAxisSelectTime = 0;

    static constexpr float kDeadzone = 0.15f;           // LX 左右死区
    static constexpr float kDeadzoneLY = 0.5f;           // LY 上下死区
    static constexpr float kDeadzoneRY = 0.3f;           // RY 上下死区
    static constexpr int kAxisSelectDebounceMs = 300;    // 去抖间隔（长按自动重复周期）

    // ── 右摇杆状态 ──
    enum class RYDirection { Neutral, Forward, Backward };
    RYDirection m_lastRYDir = RYDirection::Neutral;

    // ── 按钮状态（用于边缘检测）──
    bool m_lastA = false;
    bool m_lastB = false;
    bool m_lastX = false;
    bool m_lastY = false;
};