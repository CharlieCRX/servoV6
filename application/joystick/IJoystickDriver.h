#pragma once
#include "JoystickState.h"

/**
 * @brief 摇杆驱动抽象接口（应用层定义，基础设施层实现）
 *
 * 遵循依赖反转原则 (DIP)：
 *   - 应用层定义此接口
 *   - 基础设施层提供 SDL3JoystickDriver 实现
 *   - 测试时可以 mock 此接口
 */
class IJoystickDriver {
public:
    virtual ~IJoystickDriver() = default;

    /// @brief 读取当前摇杆状态（每 tick 调用一次）
    virtual JoystickState pollState() = 0;

    /// @brief 是否有手柄已连接
    virtual bool isConnected() const = 0;

    /// @brief 当前连接的手柄数量
    virtual int deviceCount() const = 0;

    /// @brief 设置死区值（归一化范围 [0.0, 0.5]）
    virtual void setDeadzone(float deadzone) = 0;

    /// @brief 启用/禁用触觉反馈（预留）
    virtual void enableHaptic(bool enable) = 0;
};