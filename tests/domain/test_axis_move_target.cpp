/**
 * @file test_axis_move_target.cpp
 * @brief 阶段 1：Domain 层扩展 TDD 测试 —— 四寄存器解耦
 *
 * 测试范围（MoveCommand耦合问题分析与整改方案 — 阶段1）：
 *   1. setAbsTarget(double target)   — 设置绝对移动目标（仅写 ABS_TARGET D 寄存器）
 *   2. triggerAbsMove()              — 触发绝对位置移动（仅触发 ABS_MOVE_TRIGGER M 寄存器）
 *   3. setRelTarget(double distance)  — 设置相对移动距离（仅写 REL_TARGET D 寄存器）
 *   4. triggerRelMove()              — 触发相对位置移动（仅触发 REL_MOVE_TRIGGER M 寄存器）
 *
 * 设计原则：
 *   - 4 个命令与 PLC 的 4 个寄存器一一对应，Domain 命令语义完全透明
 *   - setAbsTarget / setRelTarget 只写目标值，不触发运动
 *   - triggerAbsMove / triggerRelMove 只触发运动，不写目标值
 *   - trigger 接口基于 PLC feedback 回读的 target 值做限位预判
 *     （而非软件层缓存，保证外部 HMI / PLC 直接设 target 后也能正确校验）
 */

#include <gtest/gtest.h>
#include <limits>
#include "entity/Axis.h"

// ============================================================================
// 辅助宏：构建 AxisFeedback，按结构体字段顺序填充，未指定字段用默认值
// AxisFeedback 字段顺序：state, absPos, relPos, relZeroAbsPos,
//   posLimit, negLimit, posLimitValue, negLimitValue,
//   getjogVelocity, getMoveVelocity, absMoveTarget, relMoveTarget
// ============================================================================

// ============================================================================
// 第 0 组：基础结构验证
// ============================================================================

// 验证：AxisCommand variant 包含 4 个新命令类型
TEST(AxisMoveTargetTest, VariantShouldContainFourNewCommandTypes)
{
    // SetAbsTargetCommand
    {
        AxisCommand cmd = SetAbsTargetCommand{ 150.0 };
        EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(cmd));
        EXPECT_DOUBLE_EQ(std::get<SetAbsTargetCommand>(cmd).target, 150.0);
    }
    // TriggerAbsMoveCommand
    {
        AxisCommand cmd = TriggerAbsMoveCommand{};
        EXPECT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(cmd));
    }
    // SetRelTargetCommand
    {
        AxisCommand cmd = SetRelTargetCommand{ 50.0 };
        EXPECT_TRUE(std::holds_alternative<SetRelTargetCommand>(cmd));
        EXPECT_DOUBLE_EQ(std::get<SetRelTargetCommand>(cmd).distance, 50.0);
    }
    // TriggerRelMoveCommand
    {
        AxisCommand cmd = TriggerRelMoveCommand{};
        EXPECT_TRUE(std::holds_alternative<TriggerRelMoveCommand>(cmd));
    }
}

// 验证：AxisFeedback 包含 absMoveTarget / relMoveTarget 字段
TEST(AxisMoveTargetTest, AxisFeedbackShouldHaveTargetMirrorFields)
{
    AxisFeedback fb{};
    fb.absMoveTarget = 123.456;
    fb.relMoveTarget = -78.9;

    EXPECT_DOUBLE_EQ(fb.absMoveTarget, 123.456);
    EXPECT_DOUBLE_EQ(fb.relMoveTarget, -78.9);
}

// 验证：absMoveTarget() / relMoveTarget() getter 初始值
// 默认值必须为 max，确保 PLC 未反馈时 trigger 限位校验必然拒绝
TEST(AxisMoveTargetTest, TargetGettersShouldReturnMaxByDefault)
{
    Axis axis;
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), std::numeric_limits<double>::max());
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), std::numeric_limits<double>::max());
}

// 验证：applyFeedback 后 getter 正确镜像
TEST(AxisMoveTargetTest, ApplyFeedbackShouldMirrorAbsTargetToGetter)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 100.0;
    fb.absMoveTarget = 300.0;

    axis.applyFeedback(fb);
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 300.0);
}

TEST(AxisMoveTargetTest, ApplyFeedbackShouldMirrorRelTargetToGetter)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 100.0;
    fb.relMoveTarget = 50.0;

    axis.applyFeedback(fb);
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 50.0);
}


// ============================================================================
// 第 1 组：setAbsTarget() 测试
// ============================================================================

namespace {
/// 构建 Idle 状态 feedback，带有指定的软限位值
AxisFeedback makeIdleFeedback(double absPos, double posLimitVal, double negLimitVal)
{
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = absPos;
    fb.posLimitValue = posLimitVal;
    fb.negLimitValue = negLimitVal;
    return fb;
}
} // namespace

// 1.1 准入：Idle 状态允许设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldAcceptWhenIdle)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(axis.hasPendingCommand());

    auto cmd = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<SetAbsTargetCommand>(cmd));
    EXPECT_DOUBLE_EQ(std::get<SetAbsTargetCommand>(cmd).target, 500.0);
}

// 1.2 准入：Disabled 状态允许设置目标（电机未使能时可预置目标）
TEST(AxisMoveTargetTest, SetAbsTargetShouldAcceptWhenDisabled)
{
    Axis axis;
    {
        AxisFeedback fb{};
        fb.state = AxisState::Disabled;
        fb.absPos = 0.0;
        fb.posLimitValue = 1000.0;
        fb.negLimitValue = -100.0;
        axis.applyFeedback(fb);
    }

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));
}

// 1.3 拒绝：Unknown 状态不可设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenUnknown)
{
    Axis axis;
    // 默认初始状态为 Unknown

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 1.4 拒绝：Error 状态不可设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenError)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Error, 0.0 });

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 1.5 拒绝：Jogging 状态不可设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenJogging)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Jogging, 0.0 });

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 1.6 拒绝：MovingAbsolute 状态不可设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenMovingAbsolute)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingAbsolute, 0.0 });

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 1.7 拒绝：MovingRelative 状态不可设置目标
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenMovingRelative)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingRelative, 0.0 });

    bool ok = axis.setAbsTarget(500.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 1.8 限位拦截：目标超过正向软限位
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenTargetExceedsPositiveLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    bool ok = axis.setAbsTarget(1001.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 1.9 限位拦截：目标低于负向软限位
TEST(AxisMoveTargetTest, SetAbsTargetShouldRejectWhenTargetExceedsNegativeLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    bool ok = axis.setAbsTarget(-101.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfNegativeLimit);
}

// 1.10 边界：目标等于正向软限位（允许）
TEST(AxisMoveTargetTest, SetAbsTargetShouldAcceptWhenTargetEqualsPositiveLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    bool ok = axis.setAbsTarget(1000.0);

    EXPECT_TRUE(ok);
}

// 1.11 边界：目标等于负向软限位（允许）
TEST(AxisMoveTargetTest, SetAbsTargetShouldAcceptWhenTargetEqualsNegativeLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    bool ok = axis.setAbsTarget(-100.0);

    EXPECT_TRUE(ok);
}

// 1.12 拒绝原因重置：成功后 lastRejection 应为 None
TEST(AxisMoveTargetTest, SetAbsTargetShouldResetRejectionOnSuccess)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    // 先触发一次拒绝
    axis.setAbsTarget(1001.0);
    ASSERT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);

    // 成功后原因重置
    axis.setAbsTarget(500.0);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::None);
}


// ============================================================================
// 第 2 组：triggerAbsMove() 测试
// ============================================================================

namespace {
/// 构建带有 absMoveTarget + 限位值 的 Idle feedback
AxisFeedback makeIdleAbsTriggerFeedback(double absPos, double absTarget,
                                        double posLimitVal, double negLimitVal)
{
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = absPos;
    fb.absMoveTarget = absTarget;
    fb.posLimitValue = posLimitVal;
    fb.negLimitValue = negLimitVal;
    return fb;
}
} // namespace

// 2.1 准入：Idle 状态允许触发
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldAcceptWhenIdle)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 300.0, 1000.0, -100.0));

    bool ok = axis.triggerAbsMove();

    EXPECT_TRUE(ok);
    EXPECT_TRUE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(axis.getPendingCommand()));
}

// 2.2 拒绝：Disabled 状态不可触发
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenDisabled)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Disabled, 0.0 });

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 2.3 拒绝：Unknown 状态不可触发
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenUnknown)
{
    Axis axis;

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 2.4 拒绝：Error 状态不可触发
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenError)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Error, 0.0 });

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 2.5 拒绝：Jogging 状态不可触发
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenJogging)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Jogging, 0.0 });

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// 2.6 拒绝：AlreadyMoving - MovingAbsolute 状态
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenAlreadyMovingAbsolute)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingAbsolute, 0.0 });

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// 2.7 拒绝：AlreadyMoving - MovingRelative 状态
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectWhenMovingRelative)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingRelative, 0.0 });

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// 2.8 硬限位拦截：正限位触发时拒绝
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectAtPositiveHardLimit)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 1000.0;
    fb.posLimit = true;
    fb.absMoveTarget = 500.0;
    axis.applyFeedback(fb);

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);
}

// 2.9 硬限位拦截：负限位触发时拒绝
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldRejectAtNegativeHardLimit)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = -100.0;
    fb.negLimit = true;
    fb.absMoveTarget = 500.0;
    axis.applyFeedback(fb);

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtNegativeLimit);
}

// 2.10 软限位拦截：feedback absMoveTarget 超过正向软限位
TEST(AxisMoveTargetTest, TriggerAbsMoveRejectFeedbackTargetExceedsPosLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 1001.0, 1000.0, -100.0));

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
}

// 2.11 软限位拦截：feedback absMoveTarget 低于负向软限位
TEST(AxisMoveTargetTest, TriggerAbsMoveRejectFeedbackTargetExceedsNegLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, -101.0, 1000.0, -100.0));

    bool ok = axis.triggerAbsMove();

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfNegativeLimit);
}

// 2.12 拒绝原因重置
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldResetRejectionOnSuccess)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Disabled, 0.0 });
    axis.triggerAbsMove();
    ASSERT_EQ(axis.lastRejection(), RejectionReason::InvalidState);

    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 200.0, 1000.0, -100.0));
    EXPECT_TRUE(axis.triggerAbsMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::None);
}


// ============================================================================
// 第 3 组：setRelTarget() 测试
// ============================================================================

// 3.1 准入：Idle 状态允许设置相对距离
TEST(AxisMoveTargetTest, SetRelTargetShouldAcceptWhenIdle)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(100.0, 1000.0, -100.0));

    bool ok = axis.setRelTarget(50.0);

    EXPECT_TRUE(ok);
    EXPECT_TRUE(axis.hasPendingCommand());

    auto cmd = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<SetRelTargetCommand>(cmd));
    EXPECT_DOUBLE_EQ(std::get<SetRelTargetCommand>(cmd).distance, 50.0);
}

// 3.2 准入：Disabled 状态允许设置（预置目标）
TEST(AxisMoveTargetTest, SetRelTargetShouldAcceptWhenDisabled)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Disabled;
    fb.absPos = 0.0;
    fb.posLimitValue = 1000.0;
    fb.negLimitValue = -100.0;
    axis.applyFeedback(fb);

    bool ok = axis.setRelTarget(50.0);

    EXPECT_TRUE(ok);
}

// 3.3 拒绝：Unknown 状态不可设置
TEST(AxisMoveTargetTest, SetRelTargetShouldRejectWhenUnknown)
{
    Axis axis;

    bool ok = axis.setRelTarget(50.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 3.4 拒绝：Error 状态不可设置
TEST(AxisMoveTargetTest, SetRelTargetShouldRejectWhenError)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Error, 0.0 });

    bool ok = axis.setRelTarget(50.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 3.5～3.7 运动中拒绝
TEST(AxisMoveTargetTest, SetRelTargetShouldRejectWhenJogging)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Jogging, 0.0 });

    EXPECT_FALSE(axis.setRelTarget(50.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

TEST(AxisMoveTargetTest, SetRelTargetShouldRejectWhenMovingAbsolute)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingAbsolute, 0.0 });

    EXPECT_FALSE(axis.setRelTarget(50.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

TEST(AxisMoveTargetTest, SetRelTargetShouldRejectWhenMovingRelative)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingRelative, 0.0 });

    EXPECT_FALSE(axis.setRelTarget(50.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 3.8 软限位拦截：预期终点超过正向软限位
TEST(AxisMoveTargetTest, SetRelTargetRejectEndpointExceedsPosLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(900.0, 1000.0, -100.0));

    // 900.0 + 200.0 = 1100.0 > 1000.0
    bool ok = axis.setRelTarget(200.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
}

// 3.9 软限位拦截：预期终点低于负向软限位
TEST(AxisMoveTargetTest, SetRelTargetRejectEndpointExceedsNegLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(-50.0, 1000.0, -100.0));

    // -50.0 + (-60.0) = -110.0 < -100.0
    bool ok = axis.setRelTarget(-60.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfNegativeLimit);
}

// 3.10 边界：预期终点等于正向软限位（允许）
TEST(AxisMoveTargetTest, SetRelTargetShouldAcceptWhenEndpointEqualsPositiveLimit)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(900.0, 1000.0, -100.0));

    // 900.0 + 100.0 = 1000.0 == 正向限位
    bool ok = axis.setRelTarget(100.0);

    EXPECT_TRUE(ok);
}

// 3.11 拒绝原因重置
TEST(AxisMoveTargetTest, SetRelTargetShouldResetRejectionOnSuccess)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    axis.setRelTarget(-200.0);
    ASSERT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfNegativeLimit);

    axis.setRelTarget(50.0);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::None);
}


// ============================================================================
// 第 4 组：triggerRelMove() 测试
// ============================================================================

namespace {
AxisFeedback makeIdleRelTriggerFeedback(double absPos, double relTarget,
                                        double posLimitVal, double negLimitVal)
{
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = absPos;
    fb.relMoveTarget = relTarget;
    fb.posLimitValue = posLimitVal;
    fb.negLimitValue = negLimitVal;
    return fb;
}
} // namespace

// 4.1 准入：Idle 状态允许触发
TEST(AxisMoveTargetTest, TriggerRelMoveShouldAcceptWhenIdle)
{
    Axis axis;
    axis.applyFeedback(makeIdleRelTriggerFeedback(100.0, 50.0, 1000.0, -100.0));

    bool ok = axis.triggerRelMove();

    EXPECT_TRUE(ok);
    EXPECT_TRUE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<TriggerRelMoveCommand>(axis.getPendingCommand()));
}

// 4.2～4.7 状态拒绝
TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenDisabled)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Disabled, 0.0 });

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenUnknown)
{
    Axis axis;

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenError)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Error, 0.0 });

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenJogging)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Jogging, 0.0 });

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenMovingAbsolute)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingAbsolute, 0.0 });

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectWhenMovingRelative)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::MovingRelative, 0.0 });

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// 4.8 硬限位拦截
TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectAtPositiveHardLimit)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 500.0;
    fb.posLimit = true;
    fb.relMoveTarget = 50.0;
    axis.applyFeedback(fb);

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);
}

TEST(AxisMoveTargetTest, TriggerRelMoveShouldRejectAtNegativeHardLimit)
{
    Axis axis;
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 0.0;
    fb.negLimit = true;
    fb.relMoveTarget = 50.0;
    axis.applyFeedback(fb);

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtNegativeLimit);
}

// 4.9 软限位：基于 feedback relTarget + absPos 的预期终点超过正限位
TEST(AxisMoveTargetTest, TriggerRelMoveRejectEndpointExceedsPosLimit)
{
    Axis axis;
    // 900.0 + 200.0 = 1100.0 > 1000.0
    axis.applyFeedback(makeIdleRelTriggerFeedback(900.0, 200.0, 1000.0, -100.0));

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
}

// 4.10 软限位：基于 feedback relTarget 的预期终点低于负限位
TEST(AxisMoveTargetTest, TriggerRelMoveRejectEndpointExceedsNegLimit)
{
    Axis axis;
    // -50.0 + (-60.0) = -110.0 < -100.0
    axis.applyFeedback(makeIdleRelTriggerFeedback(-50.0, -60.0, 1000.0, -100.0));

    EXPECT_FALSE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfNegativeLimit);
}

// 4.11 拒绝原因重置
TEST(AxisMoveTargetTest, TriggerRelMoveShouldResetRejectionOnSuccess)
{
    Axis axis;
    axis.applyFeedback(AxisFeedback{ AxisState::Jogging, 0.0 });
    axis.triggerRelMove();
    ASSERT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);

    axis.applyFeedback(makeIdleRelTriggerFeedback(0.0, 50.0, 1000.0, -100.0));
    EXPECT_TRUE(axis.triggerRelMove());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::None);
}


// ============================================================================
// 第 5 组：跨接口交互 —— set/trigger 分离验证
// ============================================================================

// 5.1 setAbsTarget 不触发运动：写入后状态保持 Idle
TEST(AxisMoveTargetTest, SetAbsTargetShouldNotChangeAxisState)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    axis.setAbsTarget(500.0);

    EXPECT_EQ(axis.state(), AxisState::Idle);
}

// 5.2 setRelTarget 不触发运动
TEST(AxisMoveTargetTest, SetRelTargetShouldNotChangeAxisState)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    axis.setRelTarget(100.0);

    EXPECT_EQ(axis.state(), AxisState::Idle);
}

// 5.3 triggerAbsMove 生成 TriggerAbsMoveCommand（不携带 target）
TEST(AxisMoveTargetTest, TriggerAbsMoveCommandShouldNotContainTargetValue)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 300.0, 1000.0, -100.0));

    axis.triggerAbsMove();

    auto cmd = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(cmd));
    // TriggerAbsMoveCommand 是空结构体，不携带 target
    SUCCEED();
}

// 5.4 triggerRelMove 生成 TriggerRelMoveCommand（不携带 distance）
TEST(AxisMoveTargetTest, TriggerRelMoveCommandShouldNotContainDistanceValue)
{
    Axis axis;
    axis.applyFeedback(makeIdleRelTriggerFeedback(0.0, 80.0, 1000.0, -100.0));

    axis.triggerRelMove();

    auto cmd = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<TriggerRelMoveCommand>(cmd));
    SUCCEED();
}

// 5.5 set→trigger 分离工作流：绝对移动（先设目标，后触发）
TEST(AxisMoveTargetTest, SetAbsTargetThenTriggerAbsMoveWorkflow)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    // Step 1: setAbsTarget
    ASSERT_TRUE(axis.setAbsTarget(500.0));
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));

    // Step 2: 模拟 PLC 消费 SetAbsTargetCommand（applyFeedback 镜像 target + 刷新 pending）
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 500.0, 1000.0, -100.0));

    // Step 3: triggerAbsMove（此时 pending_intent 已被消费，可再次写入）
    ASSERT_TRUE(axis.triggerAbsMove());
    EXPECT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(axis.getPendingCommand()));
}

// 5.6 set→trigger 分离工作流：相对移动
TEST(AxisMoveTargetTest, SetRelTargetThenTriggerRelMoveWorkflow)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(100.0, 1000.0, -100.0));

    // Step 1: setRelTarget
    ASSERT_TRUE(axis.setRelTarget(50.0));
    EXPECT_TRUE(std::holds_alternative<SetRelTargetCommand>(axis.getPendingCommand()));

    // Step 2: PLC 消费 SetRelTargetCommand
    axis.applyFeedback(makeIdleRelTriggerFeedback(100.0, 50.0, 1000.0, -100.0));

    // Step 3: triggerRelMove
    ASSERT_TRUE(axis.triggerRelMove());
    EXPECT_TRUE(std::holds_alternative<TriggerRelMoveCommand>(axis.getPendingCommand()));
}

// 5.7 setAbsTarget 覆盖已有 pending trigger（set 优先级高于 trigger）
TEST(AxisMoveTargetTest, SetAbsTargetShouldOverwriteExistingPendingTrigger)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 300.0, 1000.0, -100.0));

    axis.triggerAbsMove();
    ASSERT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(axis.getPendingCommand()));

    // 已有 TriggerAbsMoveCommand pending，setAbsTarget 允许覆盖
    bool ok = axis.setAbsTarget(600.0);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));
}


// ============================================================================
// 第 6 组：闭环消费测试（applyFeedback 消费逻辑）
// ============================================================================

// 6.1 SetAbsTargetCommand 闭环：feedback.absMoveTarget 匹配后消费
TEST(AxisMoveTargetTest, SetAbsTargetConsumedWhenFeedbackMirrorsTarget)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));

    axis.setAbsTarget(500.0);
    ASSERT_TRUE(axis.hasPendingCommand());
    ASSERT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));

    // PLC 反馈 target 已写入
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 500.0, 1000.0, -100.0));

    // SetAbsTargetCommand 应在 applyFeedback 中被消费，且 target 镜像正确
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 500.0);
}

// 6.2 SetRelTargetCommand 闭环：feedback.relMoveTarget 匹配后消费
TEST(AxisMoveTargetTest, SetRelTargetConsumedWhenFeedbackMirrorsTarget)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(100.0, 1000.0, -100.0));

    axis.setRelTarget(50.0);
    ASSERT_TRUE(axis.hasPendingCommand());
    ASSERT_TRUE(std::holds_alternative<SetRelTargetCommand>(axis.getPendingCommand()));

    // PLC 反馈 rel target 已写入
    axis.applyFeedback(makeIdleRelTriggerFeedback(100.0, 50.0, 1000.0, -100.0));

    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 50.0);
}

// 6.3 TriggerAbsMoveCommand 闭环：feedback.absMoveTarget 不变时保持
TEST(AxisMoveTargetTest, TriggerAbsMoveShouldBeConsumedOnAck)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 500.0, 1000.0, -100.0));

    axis.triggerAbsMove();
    ASSERT_TRUE(std::holds_alternative<TriggerAbsMoveCommand>(axis.getPendingCommand()));

    // PLC ack：状态变为 MovingAbsolute，pending 消费
    AxisFeedback fb{};
    fb.state = AxisState::MovingAbsolute;
    fb.absPos = 0.0;
    fb.absMoveTarget = 500.0;
    fb.posLimitValue = 1000.0;
    fb.negLimitValue = -100.0;
    axis.applyFeedback(fb);

    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_EQ(axis.state(), AxisState::MovingAbsolute);
}

// 6.4 TriggerRelMoveCommand 闭环：PLC ack 切换状态并消费
TEST(AxisMoveTargetTest, TriggerRelMoveShouldBeConsumedOnAck)
{
    Axis axis;
    axis.applyFeedback(makeIdleRelTriggerFeedback(100.0, 50.0, 1000.0, -100.0));

    axis.triggerRelMove();
    ASSERT_TRUE(std::holds_alternative<TriggerRelMoveCommand>(axis.getPendingCommand()));

    AxisFeedback fb{};
    fb.state = AxisState::MovingRelative;
    fb.absPos = 100.0;
    fb.relMoveTarget = 50.0;
    fb.posLimitValue = 1000.0;
    fb.negLimitValue = -100.0;
    axis.applyFeedback(fb);

    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_EQ(axis.state(), AxisState::MovingRelative);
}

// 6.5 SetAbsTarget 不匹配时不消费
TEST(AxisMoveTargetTest, SetAbsTargetShouldNotBeConsumedWhenMismatch)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));
    axis.setAbsTarget(500.0);
    ASSERT_TRUE(axis.hasPendingCommand());

    // feedback target 值与 intent 不匹配 → 不消费
    AxisFeedback fb{};
    fb.state = AxisState::Idle;
    fb.absPos = 0.0;
    fb.absMoveTarget = 300.0;
    fb.posLimitValue = 1000.0;
    fb.negLimitValue = -100.0;
    axis.applyFeedback(fb);

    EXPECT_TRUE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));
}


// ============================================================================
// 第 7 组：通用一致性验证
// ============================================================================

// 7.1 双轴独立：单 Axis 实例不共享状态
TEST(AxisMoveTargetTest, TwoAxesShouldMaintainIndependentTargets)
{
    Axis axisA, axisB;

    axisA.applyFeedback(makeIdleFeedback(0.0, 200.0, -200.0));
    axisB.applyFeedback(makeIdleFeedback(0.0, 300.0, -300.0));

    EXPECT_TRUE(axisA.setAbsTarget(100.0));
    EXPECT_TRUE(axisB.setRelTarget(50.0));

    // B 的 setRelTarget 不应影响 A 的 pending_intent
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axisA.getPendingCommand()));
    EXPECT_TRUE(std::holds_alternative<SetRelTargetCommand>(axisB.getPendingCommand()));

    EXPECT_DOUBLE_EQ(std::get<SetAbsTargetCommand>(axisA.getPendingCommand()).target, 100.0);
    EXPECT_DOUBLE_EQ(std::get<SetRelTargetCommand>(axisB.getPendingCommand()).distance, 50.0);
}

// 7.2 getPendingCommand 不消费 pending_intent
TEST(AxisMoveTargetTest, GetPendingCommandShouldNotConsumeIntent)
{
    Axis axis;
    axis.applyFeedback(makeIdleFeedback(0.0, 1000.0, -100.0));
    axis.setAbsTarget(200.0);

    ASSERT_TRUE(axis.hasPendingCommand());
    auto cmd1 = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<SetAbsTargetCommand>(cmd1));

    // 再次获取，状态不变
    ASSERT_TRUE(axis.hasPendingCommand());
    auto cmd2 = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<SetAbsTargetCommand>(cmd2));
    EXPECT_DOUBLE_EQ(std::get<SetAbsTargetCommand>(cmd2).target, 200.0);
}


// ============================================================================
// 第 8 组：feedback-only 更新验证（核心重构目标）
// 验证：setAbsTarget / setRelTarget 不应更新内部 target 镜像，
//       只有 applyFeedback 通过反馈回路写入后，getter 才反映 PLC 实际值
// ============================================================================

// 8.1 setAbsTarget 成功后，absMoveTarget() 不更新为意图值
//     — 必须等 PLC 反馈确认后才变更
TEST(AxisMoveTargetTest, SetAbsTargetDoesNotUpdateAbsTargetGetter)
{
    Axis axis;
    // 先反馈一个旧值
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 100.0, 1000.0, -100.0));
    ASSERT_DOUBLE_EQ(axis.absMoveTarget(), 100.0);

    // setAbsTarget(500.0) 校验通过，但不应立即写入 absMoveTarget
    ASSERT_TRUE(axis.setAbsTarget(500.0));
    // 关键断言：getter 仍为旧值（反馈值），不是意图值
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 100.0)
        << "absMoveTarget() should NOT change after setAbsTarget — "
        << "it must wait for PLC feedback confirmation";
}

// 8.2 setRelTarget 成功后，relMoveTarget() 不更新为意图值
TEST(AxisMoveTargetTest, SetRelTargetDoesNotUpdateRelTargetGetter)
{
    Axis axis;
    // 先反馈一个旧值
    axis.applyFeedback(makeIdleRelTriggerFeedback(0.0, 30.0, 1000.0, -100.0));
    ASSERT_DOUBLE_EQ(axis.relMoveTarget(), 30.0);

    // setRelTarget(80.0) 校验通过，但不应立即写入 relMoveTarget
    ASSERT_TRUE(axis.setRelTarget(80.0));
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 30.0)
        << "relMoveTarget() should NOT change after setRelTarget — "
        << "it must wait for PLC feedback confirmation";
}

// 8.3 start from zero: setAbsTarget 不改变 getter，applyFeedback 后才变
TEST(AxisMoveTargetTest, AbsTargetOnlyUpdatesAfterFeedbackConfirmation)
{
    Axis axis;
    // 初始状态：无反馈，getter 为 max（安全默认值）
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), std::numeric_limits<double>::max());

    // make idle with no prior abs target
    AxisFeedback fbInit{};
    fbInit.state = AxisState::Idle;
    fbInit.absPos = 0.0;
    fbInit.posLimitValue = 1000.0;
    fbInit.negLimitValue = -100.0;
    fbInit.absMoveTarget = 0.0;
    axis.applyFeedback(fbInit);

    // setAbsTarget → getter 仍为 0
    ASSERT_TRUE(axis.setAbsTarget(500.0));
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 0.0)
        << "After setAbsTarget, getter must stay 0 until PLC feedback";

    // PLC 反馈确认 → getter 更新
    AxisFeedback fbAck{};
    fbAck.state = AxisState::Idle;
    fbAck.absPos = 0.0;
    fbAck.posLimitValue = 1000.0;
    fbAck.negLimitValue = -100.0;
    fbAck.absMoveTarget = 500.0;  // <-- PLC 确认写入
    axis.applyFeedback(fbAck);

    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 500.0)
        << "Only after applyFeedback mirrors the target should getter update";
}

// 8.4 start from zero: setRelTarget 不改变 getter，applyFeedback 后才变
TEST(AxisMoveTargetTest, RelTargetOnlyUpdatesAfterFeedbackConfirmation)
{
    Axis axis;
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), std::numeric_limits<double>::max());

    AxisFeedback fbInit{};
    fbInit.state = AxisState::Idle;
    fbInit.absPos = 100.0;
    fbInit.posLimitValue = 1000.0;
    fbInit.negLimitValue = -100.0;
    fbInit.relMoveTarget = 0.0;
    axis.applyFeedback(fbInit);

    // setRelTarget → getter 仍为 0
    ASSERT_TRUE(axis.setRelTarget(50.0));
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 0.0)
        << "After setRelTarget, getter must stay 0 until PLC feedback";

    // PLC 反馈确认
    AxisFeedback fbAck{};
    fbAck.state = AxisState::Idle;
    fbAck.absPos = 100.0;
    fbAck.posLimitValue = 1000.0;
    fbAck.negLimitValue = -100.0;
    fbAck.relMoveTarget = 50.0;
    axis.applyFeedback(fbAck);

    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 50.0);
}

// 8.5 多次 setAbsTarget 期间 getter 始终反映反馈值，不受意图覆盖
TEST(AxisMoveTargetTest, AbsTargetGetterPreservesFeedbackAcrossMultipleSetCalls)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 100.0, 1000.0, -100.0));
    ASSERT_DOUBLE_EQ(axis.absMoveTarget(), 100.0);

    axis.setAbsTarget(300.0);
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 100.0);

    // PLC 未反馈 — 再次设不同目标，getter 依然是旧反馈值
    axis.setAbsTarget(700.0);
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 100.0)
        << "Even after multiple setAbsTarget calls, getter stays at last feedback value";

    // 最终 PLC 反馈确认 700.0
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 700.0, 1000.0, -100.0));
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 700.0);
}

// 8.6 applyFeedback 不匹配时 getter 取 feedback 值（不受 intent 干扰）
TEST(AxisMoveTargetTest, AbsTargetGetterReflectsFeedbackEvenWhenMismatched)
{
    Axis axis;
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 100.0, 1000.0, -100.0));

    axis.setAbsTarget(500.0);
    // PLC 写入的是另一个值（比如 HMI 手动改的），不是 domain 发的 500.0
    AxisFeedback fbMismatch{};
    fbMismatch.state = AxisState::Idle;
    fbMismatch.absPos = 0.0;
    fbMismatch.posLimitValue = 1000.0;
    fbMismatch.negLimitValue = -100.0;
    fbMismatch.absMoveTarget = 300.0;
    axis.applyFeedback(fbMismatch);

    // getter 应该反映 PLC 实际值 300.0（feedback），不是意图值 500.0
    EXPECT_DOUBLE_EQ(axis.absMoveTarget(), 300.0)
        << "absMoveTarget() must reflect actual PLC feedback (300.0), not intent (500.0)";
    // intent 仍在 pending（因为 300 ≠ 500，闭环不消费）
    EXPECT_TRUE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<SetAbsTargetCommand>(axis.getPendingCommand()));
}

// 8.7 applyFeedback 总权重：无论何种路径，getter 永远反映最近一次 feedback
TEST(AxisMoveTargetTest, TargetGettersAlwaysReflectLatestFeedback)
{
    Axis axis;

    // 阶段 1：初始 feedback 设为 50.0
    axis.applyFeedback(makeIdleRelTriggerFeedback(0.0, 50.0, 1000.0, -100.0));
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 50.0);

    // 阶段 2：setRelTarget 设意图 200.0 → getter 仍是 50.0
    axis.setRelTarget(200.0);
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 50.0);

    // 阶段 3：PLC 先反馈 150.0（可能是上一次遗留的反馈）
    AxisFeedback fbMid{};
    fbMid.state = AxisState::Idle;
    fbMid.absPos = 0.0;
    fbMid.posLimitValue = 1000.0;
    fbMid.negLimitValue = -100.0;
    fbMid.relMoveTarget = 150.0;
    axis.applyFeedback(fbMid);
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 150.0);

    // 阶段 4：PLC 最终反馈 200.0，闭环消费
    axis.applyFeedback(makeIdleRelTriggerFeedback(0.0, 200.0, 1000.0, -100.0));
    EXPECT_DOUBLE_EQ(axis.relMoveTarget(), 200.0);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 8.8 完整工作流：setAbsTarget → triggerAbsMove（基于反馈值做限位校验）
//     验证 trigger 限位校验使用的是 feedback 目标值，而非意图值
TEST(AxisMoveTargetTest, TriggerUsesFeedbackTargetNotIntentForLimitCheck)
{
    Axis axis;
    // 反馈值在限位内
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 500.0, 1000.0, -100.0));

    // 设意图 2000.0（超出限位）— setAbsTarget 自己会拒绝
    EXPECT_FALSE(axis.setAbsTarget(2000.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);

    // 设意图 500.0（等同反馈值），setAbsTarget 通过
    ASSERT_TRUE(axis.setAbsTarget(500.0));
    // 消费 setAbsTarget
    axis.applyFeedback(makeIdleAbsTriggerFeedback(0.0, 500.0, 1000.0, -100.0));
    ASSERT_FALSE(axis.hasPendingCommand());

    // triggerAbsMove 使用 feedback.absMoveTarget(=500)=限位校验通过
    EXPECT_TRUE(axis.triggerAbsMove());
}
