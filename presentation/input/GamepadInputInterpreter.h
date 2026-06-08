#pragma once

#include <QObject>
#include <QElapsedTimer>
#include "InputEvent.h"

class AndroidGamepadJoystick;  // forward-declare from infrastructure/joystick/

/// @brief 将 HAL 层原始摇杆数据翻译为统一的 InputEvent
/// 负责：死区判断 / 去抖 / 边缘触发状态机
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
    qint64 m_lastAxisSelectTime = 0;
    static constexpr float kDeadzone = 0.3f;
    static constexpr int kAxisSelectDebounceMs = 300;

    // ── 右摇杆状态 ──
    enum class RYDirection { Neutral, Forward, Backward };
    RYDirection m_lastRYDir = RYDirection::Neutral;

    // ── 按钮状态（用于边缘检测）──
    bool m_lastA = false;
    bool m_lastB = false;
    bool m_lastX = false;
    bool m_lastY = false;
};