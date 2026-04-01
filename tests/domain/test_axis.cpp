#include <gtest/gtest.h>
#include "entity/Axis.h"

// 软件承认自己不知道状态
/**
 * 
 * ❌ jog() 不能改 state
 * ❌ move() 不能改 state
 * ❌ usecase 不能改 state
 * ✅ 只有 applyFeedback() 能改 state
 */
TEST(AxisTest, ShouldBeIdleInitially)
{
    Axis axis;

    EXPECT_EQ(axis.state(), AxisState::Unknown);
}

TEST(AxisTest, ShouldUpdateStateFromFeedback)
{
    Axis axis;

    axis.applyFeedback(AxisFeedback{
        .state = AxisState::Disabled
    });

    EXPECT_EQ(axis.state(), AxisState::Disabled);
}

TEST(AxisTest, ShouldOverridePreviousState)
{
    Axis axis;

    axis.applyFeedback({AxisState::Disabled});
    axis.applyFeedback({AxisState::Idle});

    EXPECT_EQ(axis.state(), AxisState::Idle);
}


// 第一组：状态屏障
// Disable 不能 Jog
TEST(AxisTest, ShouldRejectJogWhenDisabled)
{
    Axis axis;

    axis.applyFeedback({AxisState::Disabled});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// Unknown 也不能 Jog
TEST(AxisTest, ShouldRejectJogWhenUnknown)
{
    Axis axis;

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第二组：Idle 才允许操作
// Idle 可以 Jog
TEST(AxisTest, ShouldAcceptJogWhenIdle)
{
    Axis axis;

    axis.applyFeedback({AxisState::Idle});

    bool result = axis.jog(Direction::Forward);

    EXPECT_TRUE(result);
    EXPECT_TRUE(axis.hasPendingCommand());
}

// Idle 时记录方向
TEST(AxisTest, ShouldStoreJogDirection)
{
    Axis axis;

    axis.applyFeedback({AxisState::Idle});

    axis.jog(Direction::Backward);

    auto dir = axis.pendingDirection();

    ASSERT_TRUE(dir.has_value());
    EXPECT_EQ(dir.value(), Direction::Backward);
}

// 第三组：执行中禁止新命令
// Jogging 时不能再次 Jog
TEST(AxisTest, ShouldRejectJogWhenJogging)
{
    Axis axis;

    axis.applyFeedback({AxisState::Jogging});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// MovingAbsolute 时不能 Jog
TEST(AxisTest, ShouldRejectJogWhenMovingAbsolute)
{
    Axis axis;

    axis.applyFeedback({AxisState::MovingAbsolute});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// MovingRelative 时不能 Jog
TEST(AxisTest, ShouldRejectJogWhenMovingRelative)
{
    Axis axis;

    axis.applyFeedback({AxisState::MovingRelative});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 第四组：Error 状态锁死
TEST(AxisTest, ShouldRejectJogWhenError)
{
    Axis axis;

    axis.applyFeedback({AxisState::Error});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 第五组：生命周期闭环
// 当进入执行状态时，意图应当被消费/清除
TEST(AxisTest, ShouldClearPendingCommandWhenJoggingStarts)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});
    axis.jog(Direction::Forward);

    // 模拟 PLC 响应了指令，反馈回来的状态变成了 Jogging
    axis.applyFeedback({AxisState::Jogging});

    // 意图应当消失，因为“现实”已经开始执行“意图”了
    EXPECT_FALSE(axis.hasPendingCommand());
}




// 第六组：Move 语义区分
// 最小约束：轴必须能区分绝对定位和相对定位意图
TEST(AxisTest, ShouldDistinguishAbsoluteMoveIntent)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    double targetPos = 123.4;
    // 触发绝对定位
    axis.moveAbsolute(targetPos);

    // 验证：意图类型必须是 Absolute
    EXPECT_EQ(axis.pendingMoveType(), MoveType::Absolute);


    // 1. 先验证 optional 有值
    auto target = axis.pendingTarget();
    ASSERT_TRUE(target.has_value());

    // 2. 再取出值进行浮点数比较
    EXPECT_DOUBLE_EQ(target.value(), targetPos);
}

TEST(AxisTest, ShouldDistinguishRelativeMoveIntent)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    double distance = 50.0;
    // 触发相对定位
    axis.moveRelative(distance);

    // 验证：意图类型必须是 Relative
    EXPECT_EQ(axis.pendingMoveType(), MoveType::Relative);

    // 1. 先验证 optional 有值
    auto target = axis.pendingTarget();
    ASSERT_TRUE(target.has_value());

    // 2. 再取出值进行浮点数比较
    EXPECT_DOUBLE_EQ(target.value(), distance);
}

// 绝对定位的闭环测试
TEST(AxisTest, ShouldHandleAbsoluteMoveLifecycle)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    // 1. 发起意图
    double targetPos = 100.0;
    bool accepted = axis.moveAbsolute(targetPos);

    EXPECT_TRUE(accepted);
    EXPECT_EQ(axis.pendingMoveType(), MoveType::Absolute);

    // 2. 模拟 PLC 反馈：进入绝对移动状态
    axis.applyFeedback({AxisState::MovingAbsolute});

    // 3. 验证：状态更新，且意图被消费
    EXPECT_EQ(axis.state(), AxisState::MovingAbsolute);
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 相对定位的闭环测试
TEST(AxisTest, ShouldHandleRelativeMoveLifecycle)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    // 1. 发起意图
    double distance = -50.0;
    bool accepted = axis.moveRelative(distance);

    EXPECT_TRUE(accepted);
    EXPECT_EQ(axis.pendingMoveType(), MoveType::Relative);

    // 2. 模拟 PLC 反馈：进入相对移动状态
    axis.applyFeedback({AxisState::MovingRelative});

    // 3. 验证
    EXPECT_EQ(axis.state(), AxisState::MovingRelative);
    EXPECT_FALSE(axis.hasPendingCommand());
}
