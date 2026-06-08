#pragma once

/**
 * @brief 摇杆方向枚举
 *
 * 四点方向 + 回中 + 对角方向（扩展预留），
 * 用于将连续的摇杆轴值映射为离散的运动方向。
 */
enum class JoystickDirection {
    Neutral,       // 死区/回中
    Forward,       // 前 (Y轴负值)  → JOG+
    Backward,      // 后 (Y轴正值)  → JOG-
    Left,          // 左 (X轴负值)  → JOG-
    Right,         // 右 (X轴正值)  → JOG+
    // 以下为定位模式扩展预留
    ForwardLeft,
    ForwardRight,
    BackwardLeft,
    BackwardRight,
};