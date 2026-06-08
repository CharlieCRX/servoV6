#pragma once

#include "application/joystick/IJoystickDriver.h"
#include "application/joystick/JoystickState.h"
#include "application/joystick/JoystickDirection.h"
#include <functional>

/**
 * @brief 摇杆控制策略
 *
 * 职责：
 *   1. 跨轴跳跃保护：从 Forward 直接跳到 Backward 时，先发射 ForwardReleased
 *   2. 模式感知分发：根据 UI 的 currentMode 选择点动或定位分发策略
 *   3. 边沿/电平触发切换：定位模式使用边沿触发（避免连续重复触发）
 *   4. 多轴冲突仲裁：当多个轴同时有输入时，以绝对值最大的轴为准
 *
 * 设计原则：
 *   - 不直接依赖 Qt / QML
 *   - 通过 std::function 回调与 ViewModel 解耦
 *   - 所有状态由外部 tick() 驱动
 *   - 纯数据进/出，无副作用
 */
class JoystickControlPolicy {
public:
    /**
     * @brief UI 模式枚举（与 QML ActionControlBlock.currentMode 对齐）
     */
    enum class UIMode {
        Jog = 0,       // 点动模式
        Position = 1   // 定位模式
    };

    /**
     * @brief 摇杆控制动作回调类型
     *
     * 由 ViewModel 在构造时注入，Policy 不感知 QML/Qt。
     */
    struct ActionCallbacks {
        // ── 点动模式回调 ──
        std::function<void()> onJogPositivePressed;
        std::function<void()> onJogPositiveReleased;
        std::function<void()> onJogNegativePressed;
        std::function<void()> onJogNegativeReleased;

        // ── 定位模式回调（摇杆方向 → setRelTarget + triggerRelMove）──
        // 参数 distance: 正值为前进，负值为后退
        // 返回值: true=操作已接受, false=被拒绝（安全锁/忙）
        std::function<bool(double distance)> onPositionMoveRequested;

        // ── 绝对定位 GO（预留：可映射到特定摇杆按键）──
        std::function<void()> onAbsMoveTriggered;

        // ── 急停 ──
        std::function<void()> onStop;
    };

    JoystickControlPolicy() = default;

    // ── 模式与参数 ──

    void setUIMode(UIMode mode) { m_uiMode = mode; }
    UIMode uiMode() const { return m_uiMode; }

    void setStepDistance(double mm) { m_stepDistance = mm; }
    double stepDistance() const { return m_stepDistance; }

    // ── 状态查询 ──

    /// @brief 获取上一次有效方向（供调试/日志/状态显示）
    JoystickDirection lastDirection() const { return m_lastDirection; }

    /// @brief 定位模式是否已锁定（按住不放期间为 false）
    bool positionTriggerArmed() const { return m_positionTriggerArmed; }

    // ── 核心驱动 ──

    /**
     * @brief 每帧调用：读取摇杆状态，执行方向判定与分发
     *
     * @param state     当前摇杆状态（从 IJoystickDriver::pollState() 获取）
     * @param callbacks 动作回调（ViewModel 注入）
     */
    void tick(const JoystickState& state, const ActionCallbacks& callbacks);

private:
    // ── 模式与参数 ──
    UIMode m_uiMode = UIMode::Jog;
    double m_stepDistance = 1.0;  // 默认步进 1mm

    // ── 状态追踪 ──
    JoystickDirection m_lastDirection = JoystickDirection::Neutral;

    // ── 定位模式边沿触发控制 ──
    bool m_positionTriggerArmed = true;  // 回中后重置为 true

    // ── 点动模式方向追踪（用于 applyCrossAxisGuard）──
    JoystickDirection m_lastJogDirection = JoystickDirection::Neutral;

    // ── 内部方法 ──

    /**
     * @brief 点动模式分发（电平触发）
     *
     * 推住摇杆持续产生运动，回中自动停止。
     */
    void dispatchJogMode(
        JoystickDirection dir,
        const ActionCallbacks& callbacks);

    /**
     * @brief 定位模式分发（边沿触发）
     *
     * 每次推-回周期只触发一次定位移动。
     */
    void dispatchPositionMode(
        JoystickDirection dir,
        const ActionCallbacks& callbacks);

    /**
     * @brief 跨轴跳跃保护
     *
     * 场景：摇杆从 Forward 直接跳到 Backward（不经过 Neutral），
     * 先补发上一个方向的 Released，避免电机收到冲突指令。
     *
     * @param oldDir 上一帧的方向
     * @param newDir 当前帧的方向
     */
    void applyCrossAxisGuard(
        JoystickDirection oldDir,
        JoystickDirection newDir,
        const ActionCallbacks& callbacks);

    // ── 辅助判定（静态，方便测试）──

    /// @brief 方向是否映射到 JOG+（Forward / Right）
    static bool isJogPositive(JoystickDirection dir) {
        return dir == JoystickDirection::Forward ||
               dir == JoystickDirection::Right;
    }

    /// @brief 方向是否映射到 JOG-（Backward / Left）
    static bool isJogNegative(JoystickDirection dir) {
        return dir == JoystickDirection::Backward ||
               dir == JoystickDirection::Left;
    }

    /// @brief 方向是否是活跃的运动方向（非 Neutral、非对角）
    static bool isActiveDirection(JoystickDirection dir) {
        return isJogPositive(dir) || isJogNegative(dir);
    }
};

// ============================================================================
// 实现（Header-only，与项目现有的 Policy 风格一致）
// ============================================================================

inline void JoystickControlPolicy::tick(
    const JoystickState& state,
    const ActionCallbacks& callbacks)
{
    JoystickDirection dir = state.direction;  // 综合方向

    if (!state.connected) {
        // 手柄断开时：释放所有方向
        if (isActiveDirection(m_lastJogDirection)) {
            applyCrossAxisGuard(m_lastJogDirection,
                                JoystickDirection::Neutral, callbacks);
            m_lastJogDirection = JoystickDirection::Neutral;
        }
        m_lastDirection = JoystickDirection::Neutral;
        m_positionTriggerArmed = true;
        return;
    }

    // ═══════════════════════════════════════
    // Step 1: 跨轴跳跃保护（点动模式）
    // ═══════════════════════════════════════
    if (m_uiMode == UIMode::Jog) {
        applyCrossAxisGuard(m_lastDirection, dir, callbacks);
    }

    // ═══════════════════════════════════════
    // Step 2: 模式感知分发
    // ═══════════════════════════════════════
    if (m_uiMode == UIMode::Jog) {
        dispatchJogMode(dir, callbacks);
        if (isActiveDirection(dir)) {
            m_lastJogDirection = dir;
        }
    } else {
        dispatchPositionMode(dir, callbacks);
    }

    m_lastDirection = dir;
}

inline void JoystickControlPolicy::applyCrossAxisGuard(
    JoystickDirection oldDir,
    JoystickDirection newDir,
    const ActionCallbacks& callbacks)
{
    bool oldIsPositive = isJogPositive(oldDir);
    bool oldIsNegative = isJogNegative(oldDir);
    bool newIsPositive = isJogPositive(newDir);
    bool newIsNegative = isJogNegative(newDir);

    // 从 JOG+ 直接跳到 JOG-（不经过 Neutral）
    if (oldIsPositive && newIsNegative) {
        callbacks.onJogPositiveReleased();
    }
    // 从 JOG- 直接跳到 JOG+（不经过 Neutral）
    else if (oldIsNegative && newIsPositive) {
        callbacks.onJogNegativeReleased();
    }
    // 从 JOG+ 回到 Neutral
    else if (oldIsPositive && newDir == JoystickDirection::Neutral) {
        callbacks.onJogPositiveReleased();
    }
    // 从 JOG- 回到 Neutral
    else if (oldIsNegative && newDir == JoystickDirection::Neutral) {
        callbacks.onJogNegativeReleased();
    }
}

inline void JoystickControlPolicy::dispatchJogMode(
    JoystickDirection dir,
    const ActionCallbacks& callbacks)
{
    // 判断新方向
    bool newPositive = isJogPositive(dir);
    bool newNegative = isJogNegative(dir);

    // 判断旧方向
    bool oldPositive = isJogPositive(m_lastDirection);
    bool oldNegative = isJogNegative(m_lastDirection);

    if (newPositive && !oldPositive) {
        // 新按下 JOG+
        callbacks.onJogPositivePressed();
    } else if (newNegative && !oldNegative) {
        // 新按下 JOG-
        callbacks.onJogNegativePressed();
    }
    // Neutral（回中）已在 applyCrossAxisGuard 中处理 Released
}

inline void JoystickControlPolicy::dispatchPositionMode(
    JoystickDirection dir,
    const ActionCallbacks& callbacks)
{
    // ═══════════════════════════════════════
    // 定位模式：边沿触发
    //
    // 回中 → 解锁 (arm)
    // 非 Neutral → 如果 armed 则触发，然后锁定
    // 按住不放 → 跳过（armed=false）
    // ═══════════════════════════════════════

    if (dir == JoystickDirection::Neutral) {
        m_positionTriggerArmed = true;  // 回中 → 允许下次触发
        return;
    }

    if (!m_positionTriggerArmed) {
        return;  // 按住不放，不重复触发
    }

    // 确定距离：正向还是反向
    double distance = 0.0;
    if (isJogPositive(dir)) {
        distance = +m_stepDistance;
    } else if (isJogNegative(dir)) {
        distance = -m_stepDistance;
    } else {
        return;  // 不处理对角方向（ForwardLeft 等）
    }

    // 边沿触发锁定（无论回调成功与否都锁定，避免连续触发）
    m_positionTriggerArmed = false;

    // 通过回调触发 setRelTarget + triggerRelMove
    callbacks.onPositionMoveRequested(distance);
}