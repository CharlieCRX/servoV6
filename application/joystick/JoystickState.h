#pragma once
#include "JoystickDirection.h"

/**
 * @brief 摇杆完整状态快照
 *
 * 每帧由 IJoystickDriver::pollState() 返回，
 * 供上层 Policy 消费。
 */
struct JoystickState {
    bool connected = false;           // 手柄是否已连接

    // 4 轴原始值（归一化到 [-1.0, 1.0]）
    float leftX  = 0.0f;
    float leftY  = 0.0f;
    float rightX = 0.0f;
    float rightY = 0.0f;

    // 综合方向（经过死区 + 多轴优先级判定）
    JoystickDirection direction = JoystickDirection::Neutral;

    // 各轴独立方向（供细粒度控制使用）
    JoystickDirection leftXDir  = JoystickDirection::Neutral;
    JoystickDirection leftYDir  = JoystickDirection::Neutral;
    JoystickDirection rightXDir = JoystickDirection::Neutral;
    JoystickDirection rightYDir = JoystickDirection::Neutral;
};