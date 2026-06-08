#include <gtest/gtest.h>
#include "infrastructure/joystick/SDL3JoystickDriver.h"

// ============================================================================
// SDL3JoystickDriver 单元测试
// ============================================================================
//
// 测试覆盖：
//   1. computeDirection()  — 综合 4 轴 → 最终方向
//   2. axisDirection()     — 单轴死区判定
//
// 方向映射规则 (SDL3 坐标系)：
//   水平轴 (X)：正值 → Right(JOG+),  负值 → Left(JOG-)
//   垂直轴 (Y)：负值 → Forward(JOG+), 正值 → Backward(JOG-)
//   （SDL Y 轴：摇杆上推产生负值，下拉产生正值）
// ============================================================================

// ═══════════════════════════════════════════════════════════════
// axisDirection() 测试
// ═══════════════════════════════════════════════════════════════

TEST(AxisDirectionTest, ValueWithinDeadzone_ReturnsNeutral) {
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(0.0f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(0.10f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(-0.10f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(0.15f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Neutral); // 边界：等于死区 → Neutral
}

TEST(AxisDirectionTest, ValueAboveDeadzonePositive_ReturnsPositiveDir) {
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(0.16f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Right);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(1.0f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Right);
}

TEST(AxisDirectionTest, ValueBelowDeadzoneNegative_ReturnsNegativeDir) {
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(-0.16f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Left);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(-1.0f, 0.15f,
        JoystickDirection::Right, JoystickDirection::Left),
        JoystickDirection::Left);
}

TEST(AxisDirectionTest, VerticalAxisMapsToForwardBackward) {
    // SDL Y 轴：上推→负→Forward(JOG+), 下拉→正→Backward(JOG-)
    // axisDirection(value, deadzone, positiveDir, negativeDir)
    //   value > 0 → positiveDir  (下拉 = 正值 → Backward)
    //   value < 0 → negativeDir  (上推 = 负值 → Forward)
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(-0.8f, 0.15f,
        JoystickDirection::Backward, JoystickDirection::Forward),
        JoystickDirection::Forward);
    EXPECT_EQ(SDL3JoystickDriver::axisDirection(0.8f, 0.15f,
        JoystickDirection::Backward, JoystickDirection::Forward),
        JoystickDirection::Backward);
}

// ═══════════════════════════════════════════════════════════════
// computeDirection() 测试
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, AllAxesNeutral_ReturnsNeutral) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0, 0, 0, 0.15f),
        JoystickDirection::Neutral);
}

TEST(ComputeDirectionTest, AllAxesWithinDeadzone_ReturnsNeutral) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.1f, -0.1f, 0.1f, -0.1f, 0.15f),
        JoystickDirection::Neutral);
}

TEST(ComputeDirectionTest, LeftStickUp_ReturnsForward) {
    // SDL: 上推 = Y 轴负值 → Forward (JOG+)
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, -0.8f, 0, 0, 0.15f),
        JoystickDirection::Forward);
}

TEST(ComputeDirectionTest, LeftStickDown_ReturnsBackward) {
    // SDL: 下拉 = Y 轴正值 → Backward (JOG-)
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0.8f, 0, 0, 0.15f),
        JoystickDirection::Backward);
}

TEST(ComputeDirectionTest, LeftStickRight_ReturnsRight) {
    // X 轴正值 → Right (JOG+)
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.8f, 0, 0, 0, 0.15f),
        JoystickDirection::Right);
}

TEST(ComputeDirectionTest, LeftStickLeft_ReturnsLeft) {
    // X 轴负值 → Left (JOG-)
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(-0.8f, 0, 0, 0, 0.15f),
        JoystickDirection::Left);
}

TEST(ComputeDirectionTest, RightStickUp_ReturnsForward) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0, 0, -0.8f, 0.15f),
        JoystickDirection::Forward);
}

TEST(ComputeDirectionTest, RightStickDown_ReturnsBackward) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0, 0, 0.8f, 0.15f),
        JoystickDirection::Backward);
}

TEST(ComputeDirectionTest, RightStickRight_ReturnsRight) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0, 0.8f, 0, 0.15f),
        JoystickDirection::Right);
}

TEST(ComputeDirectionTest, RightStickLeft_ReturnsLeft) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0, -0.8f, 0, 0.15f),
        JoystickDirection::Left);
}

// ═══════════════════════════════════════════════════════════════
// 多轴冲突仲裁测试（以绝对值最大的轴为准）
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, LargerAxisDominates_LeftStickXoverY) {
    // 左摇杆上推 + 右推：Y=-0.3, X=0.8 → |X|>|Y| → Right
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.8f, -0.3f, 0, 0, 0.15f),
        JoystickDirection::Right);
}

TEST(ComputeDirectionTest, LargerAxisDominates_LeftStickYoverX) {
    // 左摇杆下拉 + 左推：Y=0.9, X=-0.5 → |Y|>|X| → Backward
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(-0.5f, 0.9f, 0, 0, 0.15f),
        JoystickDirection::Backward);
}

TEST(ComputeDirectionTest, RightStickDominatesWhenLeftIsSmall) {
    // 左摇杆轻微推，右摇杆大幅度 → 右摇杆方向优先
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.1f, 0.05f, -0.9f, 0, 0.15f),
        JoystickDirection::Left);
}

// ═══════════════════════════════════════════════════════════════
// 死区边界值测试
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, ExactlyAtDeadzone_ReturnsNeutral) {
    // 刚好在死区边界 = 0.15
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.15f, 0, 0, 0, 0.15f),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(-0.15f, 0, 0, 0, 0.15f),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0.15f, 0, 0, 0.15f),
        JoystickDirection::Neutral);
}

TEST(ComputeDirectionTest, SlightlyAboveDeadzone_TriggersDirection) {
    // 略大于死区
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.16f, 0, 0, 0, 0.15f),
        JoystickDirection::Right);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, -0.16f, 0, 0, 0.15f),
        JoystickDirection::Forward);
}

// ═══════════════════════════════════════════════════════════════
// 极端值测试
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, FullScaleRight_ReturnsRight) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(1.0f, 0, 0, 0, 0.15f),
        JoystickDirection::Right);
}

TEST(ComputeDirectionTest, FullScaleLeft_ReturnsLeft) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(-1.0f, 0, 0, 0, 0.15f),
        JoystickDirection::Left);
}

TEST(ComputeDirectionTest, FullScaleForward_ReturnsForward) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, -1.0f, 0, 0, 0.15f),
        JoystickDirection::Forward);
}

TEST(ComputeDirectionTest, FullScaleBackward_ReturnsBackward) {
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 1.0f, 0, 0, 0.15f),
        JoystickDirection::Backward);
}

// ═══════════════════════════════════════════════════════════════
// 零死区测试
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, ZeroDeadzone_AnyNonZeroTriggers) {
    // 零死区：任何非零值都应产生方向
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.001f, 0, 0, 0, 0.0f),
        JoystickDirection::Right);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(-0.001f, 0, 0, 0, 0.0f),
        JoystickDirection::Left);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, -0.001f, 0, 0, 0.0f),
        JoystickDirection::Forward);
}

// ═══════════════════════════════════════════════════════════════
// 最大死区测试
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, MaxDeadzone_OnlyFullScaleTriggers) {
    // 死区=0.5：即使 0.49 也不触发
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.49f, 0, 0, 0, 0.5f),
        JoystickDirection::Neutral);
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0.51f, 0, 0, 0, 0.5f),
        JoystickDirection::Right);
}

// ═══════════════════════════════════════════════════════════════
// SDL Y 轴取反验证（上推=负 → Forward, 下拉=正 → Backward）
// ═══════════════════════════════════════════════════════════════

TEST(ComputeDirectionTest, SDLYAxisInversion_PushUpIsForward) {
    // 验证 SDL 坐标系：摇杆物理上推 → 软件报告负值 → 最终方向 Forward
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, -0.5f, 0, 0, 0.15f),
        JoystickDirection::Forward);
}

TEST(ComputeDirectionTest, SDLYAxisInversion_PullDownIsBackward) {
    // 验证 SDL 坐标系：摇杆物理下拉 → 软件报告正值 → 最终方向 Backward
    EXPECT_EQ(SDL3JoystickDriver::computeDirection(0, 0.5f, 0, 0, 0.15f),
        JoystickDirection::Backward);
}