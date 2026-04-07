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
    axis.applyFeedback({AxisState::Idle, 0.0});

    axis.jog(Direction::Backward);

    // 获取 Variant 容器
    auto command = axis.getPendingCommand();

    // 第一步：确保类型正确（防止由于 Bug 导致存成了 MoveCommand）
    ASSERT_TRUE(std::holds_alternative<JogCommand>(command));

    // 第二步：安全提取并验证数据
    auto jogCmd = std::get<JogCommand>(command);
    EXPECT_EQ(jogCmd.dir, Direction::Backward);
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
    axis.applyFeedback({AxisState::Idle, 0.0});

    double targetPos = 123.4;
    // 触发绝对定位
    axis.moveAbsolute(targetPos);

    auto command = axis.getPendingCommand();

    // 1. 确保类型正确（防止由于 Bug 导致存成了 JogCommand）
    ASSERT_TRUE(std::holds_alternative<MoveCommand>(command));

    // 2. 安全提取并验证数据
    auto moveCmd = std::get<MoveCommand>(command);
    EXPECT_EQ(moveCmd.type, MoveType::Absolute);
    EXPECT_DOUBLE_EQ(moveCmd.target, targetPos);

}

TEST(AxisTest, ShouldDistinguishRelativeMoveIntent)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    double distance = 50.0;
    // 触发相对定位
    axis.moveRelative(distance);

    auto command = axis.getPendingCommand();
    // 1. 确保类型正确（防止由于 Bug 导致存成了 JogCommand）
    ASSERT_TRUE(std::holds_alternative<MoveCommand>(command));

    // 2. 安全提取并验证数据
    auto moveCmd = std::get<MoveCommand>(command);
    EXPECT_EQ(moveCmd.type, MoveType::Relative);
    EXPECT_DOUBLE_EQ(moveCmd.target, distance);
}


// 第七组：Move 语义闭环重构

TEST(AxisTest, ShouldHandleAbsoluteMoveLifecycle)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 0.0});

    axis.moveAbsolute(100.0);
    ASSERT_TRUE(axis.hasPendingCommand());

    // 模拟 PLC 反馈：进入绝对移动状态
    axis.applyFeedback({AxisState::MovingAbsolute, 5.0});

    // 验证：意图被消费，插槽应回归 monostate
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(axis.getPendingCommand()));
}

TEST(AxisTest, ShouldHandleRelativeMoveLifecycle)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 0.0});

    axis.moveRelative(-50.0);
    ASSERT_TRUE(axis.hasPendingCommand());

    // 模拟 PLC 反馈：进入相对移动状态
    axis.applyFeedback({AxisState::MovingRelative, 2.0});

    // 验证：意图被消费
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(axis.getPendingCommand()));
}


// 第八组：执行期屏蔽（Shielding during Execution）
// 1. 正在绝对定位时，屏蔽点动指令
TEST(AxisTest, ShouldShieldJogDuringAbsoluteMove)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});
    axis.moveAbsolute(1000.0); // 产生意图
    
    // 模拟 PLC 响应，进入运行态
    axis.applyFeedback({AxisState::MovingAbsolute});
    
    // 此时尝试 Jog
    bool result = axis.jog(Direction::Forward);
    
    // 验证：必须拒绝，且不能破坏当前的移动意图（因为移动还没完）
    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand()); 
}

//2. 正在 Jog 时，屏蔽 Move 指令
TEST(AxisTest, ShouldShieldMoveDuringJog)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});
    axis.jog(Direction::Forward); // 产生 Jog 意图
    
    // 模拟 PLC 响应，进入 Jogging 态
    axis.applyFeedback({AxisState::Jogging});
       
    // 验证：必须拒绝，且不能破坏当前的 Jog 意图
    EXPECT_FALSE(axis.moveAbsolute(500.0));
    EXPECT_FALSE(axis.moveRelative(500.0));
    EXPECT_FALSE(axis.hasPendingCommand()); 
}


// 第九组：Stop 意图重构测试

// 1. 验证 Stop 指令的穿透性与互斥覆盖
TEST(AxisTest, StopShouldPenetrateShieldingAndClearOthers)
{
    Axis axis;
    // 模拟正在运行态
    axis.applyFeedback({AxisState::MovingAbsolute, 500.0}); 

    // 执行 Stop
    bool result = axis.stop();
    
    // 验证：
    // A. Stop 必须被接受（穿透非 Idle 限制）
    EXPECT_TRUE(result);
    // B. 内部命令类型必须转换为 StopCommand
    auto command = axis.getPendingCommand();
    EXPECT_TRUE(std::holds_alternative<StopCommand>(command));
    // C. 验证原有的 Move 意图已被自动覆盖（由 variant 特性保证）
    EXPECT_FALSE(std::holds_alternative<MoveCommand>(command));
}

// 2. 验证 Stop 意图的生命周期闭环
TEST(AxisTest, ShouldHandleImmediateStopAndDisable)
{
    Axis axis;
    axis.applyFeedback({AxisState::MovingAbsolute, 100.0});
    axis.stop();

    // 模拟现实：PLC 响应并关闭使能（或回到 Idle）
    axis.applyFeedback({AxisState::Disabled, 100.0});

    // 验证：
    // A. 状态更新成功
    EXPECT_EQ(axis.state(), AxisState::Disabled);
    // B. Stop 意图已被消费，槽位回归 monostate
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(axis.getPendingCommand()));
}


// --- 绝对位置坐标体系测试 -- 
// 第1组：绝对位置反馈同步
TEST(AxisTest, ShouldSyncAbsolutePositionFromFeedback)
{
    Axis axis;
    
    // 模拟反馈数据：绝对位置为 1234.56 mm
    AxisFeedback fb;
    fb.state = AxisState::Idle;
    fb.absPos = 1234.56;

    axis.applyFeedback(fb);

    // 验证 Domain 模型是否真实反映了该位置
    EXPECT_DOUBLE_EQ(axis.currentAbsolutePosition(), 1234.56);
}


// 绝对位置清零 (Zeroing) 的业务约束
// 1. 验证：非静止状态下拒绝清零（安全屏障）
TEST(AxisTest, ShouldRejectZeroingWhenMoving)
{
    Axis axis;
    axis.applyFeedback({AxisState::MovingAbsolute, 100.0});

    bool result = axis.zeroAbsolutePosition();
    
    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 2. 验证：清零意图的产生与覆盖
TEST(AxisTest, ShouldStoreZeroingIntentAndClearMotion)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 50.0});

    axis.zeroAbsolutePosition();

    auto cmd = axis.getPendingCommand();
    EXPECT_TRUE(std::holds_alternative<ZeroAbsoluteCommand>(cmd));
}

// 3. 验证：基于容差 (Epsilon) 的闭环消费
TEST(AxisTest, ZeroingIntentShouldClearWhenPositionIsNearZero)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 50.0});
    axis.zeroAbsolutePosition();

    // 场景 A：PLC 反馈坐标还在跳变，尚未接近 0 (例如 0.5)
    axis.applyFeedback({AxisState::Idle, 0.5});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：物理坐标进入容差范围 (例如 0.0002 < 0.001)
    axis.applyFeedback({AxisState::Idle, 0.0002});
    
    // 验证：意图消失，清零成功
    EXPECT_FALSE(axis.hasPendingCommand());
}

// --- 相对位置坐标体系测试 -- 
// 第 1 组：相对坐标反馈同步测试
TEST(AxisTest, ShouldSyncRelativePositionFromFeedback)
{
    Axis axis;
    
    // 模拟 PLC 反馈：绝对位置 100.0，但 PLC 已设原点，所以相对位置为 20.0
    AxisFeedback fb;
    fb.state = AxisState::Idle;
    fb.absPos = 100.0;
    fb.relPos = 20.0; // 来自 PLC D126/D128 寄存器
    axis.applyFeedback(fb);

    // 验证：直接读取反馈值，不进行逻辑计算
    EXPECT_DOUBLE_EQ(axis.currentRelativePosition(), 20.0);
}


// 第 2 组：相对原点指令的准入屏蔽
TEST(AxisTest, ShouldRejectRelativeZeroCommandsWhenNotIdle)
{
    Axis axis;

    // 场景 A：在 Disabled 状态下拒绝
    axis.applyFeedback({AxisState::Disabled, 0.0, 0.0});
    EXPECT_FALSE(axis.setRelativeZero());
    EXPECT_FALSE(axis.clearRelativeZero());

    // 场景 B：在 Error 故障态下拒绝 
    axis.applyFeedback({AxisState::Error, 0.0, 0.0});
    EXPECT_FALSE(axis.setRelativeZero());

    // 场景 C：在运动中（Jogging）拒绝 
    axis.applyFeedback({AxisState::Jogging, 0.0, 0.0});
    EXPECT_FALSE(axis.clearRelativeZero());

    // 场景 D：在 MovingAbsolute 态下拒绝 
    axis.applyFeedback({AxisState::MovingAbsolute, 0.0, 0.0});
    EXPECT_FALSE(axis.setRelativeZero());
    
    // 场景 E：在 MovingRelative 态下拒绝 
    axis.applyFeedback({AxisState::MovingRelative, 0.0, 0.0});
    EXPECT_FALSE(axis.clearRelativeZero());

    // 场景 F：只有在 Idle 状态下才接受 
    axis.applyFeedback({AxisState::Idle, 0.0, 0.0});
    EXPECT_TRUE(axis.setRelativeZero());
}


// 第 3 组：设置相对原点的生命周期闭环
TEST(AxisTest, SetRelativeZeroIntentShouldClearWhenRelPosConvergesToZero)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 100.0, 100.0});
    
    axis.setRelativeZero(); // 产生意图 
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 A：PLC 尚未处理，relPos 还是旧值 
    axis.applyFeedback({AxisState::Idle, 100.0, 100.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：PLC 处理中，relPos 开始变动但未到 0 
    axis.applyFeedback({AxisState::Idle, 100.0, 0.5});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 C：relPos 进入容差范围 (0.0008 < 0.001) 
    axis.applyFeedback({AxisState::Idle, 100.0, 0.0008});
    
    // 验证：意图消失，操作成功 
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 第 4 组：清除相对原点的生命周期闭环
TEST(AxisTest, ClearRelativeZeroIntentShouldClearWhenRelPosEqualsAbsPos)
{
    Axis axis;
    // 初始状态：PLC 已设原点，abs=100, rel=0
    axis.applyFeedback({AxisState::Idle, 100.0, 0.0});
    
    axis.clearRelativeZero(); 

    // 场景 A：PLC 反馈 relPos 尚未恢复
    axis.applyFeedback({AxisState::Idle, 100.0, 0.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：relPos 已经恢复到接近 absPos (误差 0.0005 < 0.001)
    axis.applyFeedback({AxisState::Idle, 100.0, 99.9995});

    // 验证：意图消失
    EXPECT_FALSE(axis.hasPendingCommand());
}