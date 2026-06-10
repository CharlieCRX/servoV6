#pragma once

#include <QMetaType>

/// @brief 左摇杆产生的轴选择事件
enum class AxisSelectDirection
{
    Left,    // 左摇杆 ←
    Right    // 左摇杆 →
};

/// @brief 右摇杆产生的运动事件
enum class MotionDirection
{
    Forward,   // 右摇杆 ↑
    Backward   // 右摇杆 ↓
};

enum class MotionEventType
{
    Pressed,
    Released
};

/// @brief 按钮事件
enum class GamepadButton
{
    A,  // 使能
    B,  // 停止
    X,  // 回零
    Y   // 模式切换
};

/// @brief 统一的摇杆输入事件（替代原始的 float lx/ly/rx/ry）
/// 业务层只消费这些事件，不知道事件来自摇杆、键盘还是触摸屏
struct InputEvent
{
    enum class Type
    {
        None,
        AxisSelect,        // 左摇杆选轴
        Motion,            // 右摇杆运动
        Button,            // 按钮
    };

    Type type = Type::None;

    // AxisSelect 字段
    AxisSelectDirection axisDir = AxisSelectDirection::Right;

    // Motion 字段
    MotionDirection motionDir = MotionDirection::Forward;
    MotionEventType motionType = MotionEventType::Pressed;

    // Button 字段
    GamepadButton button = GamepadButton::A;
    bool buttonPressed = false;  // true=按下, false=释放
};

Q_DECLARE_METATYPE(InputEvent)
