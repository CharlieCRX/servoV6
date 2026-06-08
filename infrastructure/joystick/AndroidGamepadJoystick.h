#pragma once

#include <QObject>

/// @brief 硬件抽象层：Android 游戏手柄原始数据捕获
/// 职责仅为 JNI 回调 → C++ 数据投递，不包含任何语义解释（死区、去抖等由上层负责）
class AndroidGamepadJoystick : public QObject
{
    Q_OBJECT

    Q_PROPERTY(float lx READ lx NOTIFY changed)
    Q_PROPERTY(float ly READ ly NOTIFY changed)
    Q_PROPERTY(float rx READ rx NOTIFY changed)
    Q_PROPERTY(float ry READ ry NOTIFY changed)
    Q_PROPERTY(float lt READ lt NOTIFY changed)
    Q_PROPERTY(float rt READ rt NOTIFY changed)
    Q_PROPERTY(bool a READ a NOTIFY changed)
    Q_PROPERTY(bool b READ b NOTIFY changed)
    Q_PROPERTY(bool x READ x NOTIFY changed)
    Q_PROPERTY(bool y READ y NOTIFY changed)

public:
    static AndroidGamepadJoystick &instance();

    // Getters
    float lx() const { return m_lx; }
    float ly() const { return m_ly; }
    float rx() const { return m_rx; }
    float ry() const { return m_ry; }
    float lt() const { return m_lt; }
    float rt() const { return m_rt; }
    bool a() const { return m_a; }
    bool b() const { return m_b; }
    bool x() const { return m_x; }
    bool y() const { return m_y; }

    // 仅供 JNI 调用（内部使用）
    void updateAxis(float lx, float ly, float rx, float ry, float lt, float rt);
    void updateButton(int keyCode, bool pressed);

signals:
    /// @brief 任何轴值或按钮状态变化时发出
    void changed();

private:
    AndroidGamepadJoystick() = default;

    float m_lx = 0;
    float m_ly = 0;
    float m_rx = 0;
    float m_ry = 0;
    float m_lt = 0;
    float m_rt = 0;
    bool m_a = false;
    bool m_b = false;
    bool m_x = false;
    bool m_y = false;
};