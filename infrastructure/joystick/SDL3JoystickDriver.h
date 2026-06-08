#pragma once
#include "application/joystick/IJoystickDriver.h"
#include "application/joystick/JoystickState.h"
#include "ISDLJoystickWrapper.h"
#include <memory>
#include <map>
#include <vector>

/**
 * @brief SDL3 摇杆驱动实现
 *
 * 实现 IJoystickDriver 接口，封装 SDL3 原生 API。
 *
 * 线程模型：单线程（由主线程 tick loop 驱动），内部无独立线程。
 * SDL 事件泵在 pollState() 中每帧调用一次。
 */
class SDL3JoystickDriver : public IJoystickDriver {
public:
    explicit SDL3JoystickDriver(std::unique_ptr<ISDLJoystickWrapper> wrapper);
    ~SDL3JoystickDriver() override;

    // ── IJoystickDriver 接口 ──
    JoystickState pollState() override;
    bool isConnected() const override;
    int  deviceCount() const override;
    void setDeadzone(float deadzone) override;
    void enableHaptic(bool enable) override;   // 预留：力反馈开关

    // ── 方向判定（公开静态，方便单元测试）──

    /**
     * @brief 综合 4 轴值判定最终方向
     *
     * 以绝对值最大的轴为准，按优先级判定：
     *   左摇杆 X/Y → 右摇杆 X/Y
     *
     * 水平轴：正值→Right(JOG+), 负值→Left(JOG-)
     * 垂直轴：负值→Forward(JOG+), 正值→Backward(JOG-)  (SDL Y轴: 上推为负)
     *
     * @param lx 左摇杆 X 轴 [-1.0, 1.0]
     * @param ly 左摇杆 Y 轴 [-1.0, 1.0]
     * @param rx 右摇杆 X 轴 [-1.0, 1.0]
     * @param ry 右摇杆 Y 轴 [-1.0, 1.0]
     * @param deadzone 死区阈值，|值|≤deadzone 时视为 Neutral
     * @return 综合方向
     */
    static JoystickDirection computeDirection(
        float lx, float ly, float rx, float ry, float deadzone);

    /**
     * @brief 判定单个轴的方向
     */
    static JoystickDirection axisDirection(float value, float deadzone,
                                            JoystickDirection positiveDir,
                                            JoystickDirection negativeDir);

private:
    std::unique_ptr<ISDLJoystickWrapper> m_wrapper;
    bool m_connected       = false;
    float m_deadzone       = 0.15f;  // 默认死区 15%
    int  m_activeDeviceId  = -1;
};