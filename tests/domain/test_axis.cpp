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
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
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

    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

    bool result = axis.jog(Direction::Forward);

    EXPECT_TRUE(result);
    EXPECT_TRUE(axis.hasPendingCommand());
}

// Idle 时记录方向
TEST(AxisTest, ShouldStoreJogDirection)
{
    Axis axis;
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

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
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// MovingAbsolute 时不能 Jog
TEST(AxisTest, ShouldRejectJogWhenMovingAbsolute)
{
    Axis axis;

    axis.applyFeedback({AxisState::MovingAbsolute});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// MovingRelative 时不能 Jog
TEST(AxisTest, ShouldRejectJogWhenMovingRelative)
{
    Axis axis;

    axis.applyFeedback({AxisState::MovingRelative});

    bool result = axis.jog(Direction::Forward);

    EXPECT_FALSE(result);
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
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
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

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
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

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


// 7.1 绝对定位：数值收敛闭环
TEST(AxisTest, AbsoluteMoveShouldOnlyClearWhenArrivedAndIdle)
{
    Axis axis;
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

    double targetPos = 100.0;
    axis.moveAbsolute(targetPos);
    
    // 场景 A：PLC 反馈正在移动，坐标 50.0
    // 约束：状态不是 Idle，意图必须保持
    axis.applyFeedback({AxisState::MovingAbsolute, 50.0, 50.0, 0.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：PLC 反馈已停止（Idle），但坐标在 90.0（未到达容差范围）
    // 约束：坐标未达标，意图必须保持
    axis.applyFeedback({AxisState::Idle, 90.0, 90.0, 0.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 C：PLC 反馈已停止（Idle），坐标在 100.0005（进入 0.001 容差）
    axis.applyFeedback({AxisState::Idle, 100.0005, 100.0005, 0.0});
    
    // 验证：状态与数值双重达标，意图消失
    EXPECT_FALSE(axis.hasPendingCommand());
    EXPECT_FALSE(axis.isMoveInProgress());
}

// 7.2 相对定位：起跳点捕获与数值收敛
TEST(AxisTest, RelativeMoveShouldCaptureStartAndVerifyDistance)
{
    Axis axis;
    // 初始位置在 50.0
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 50.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });

    double distance = 30.0; // 预期终点应该是 80.0
    axis.moveRelative(distance);
    
    // 场景 A：移动中，到达 60.0
    axis.applyFeedback({AxisState::MovingRelative, 60.0, 60.0, 0.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：停止了，但由于机械原因只到了 70.0
    axis.applyFeedback({AxisState::Idle, 70.0, 70.0, 0.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 C：停止了，坐标在 79.9998（接近 80.0）
    axis.applyFeedback({AxisState::Idle, 79.9998, 79.9998, 0.0});

    // 验证：意图消失
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第八组：执行期屏蔽（Shielding during Execution）
// 1. 正在绝对定位时，屏蔽点动指令
TEST(AxisTest, ShouldShieldJogDuringAbsoluteMove)
{
    Axis axis;
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 0.0, 
        .posLimitValue = 1000.0,  // 给够空间
        .negLimitValue = -1000.0
    });
    axis.moveAbsolute(1000.0); // 产生 Move 意图
    
    // 模拟 PLC 响应，进入运行态，但还没到终点
    axis.applyFeedback({AxisState::MovingAbsolute, 50.0}); 
    
    // 此时尝试 Jog
    bool result = axis.jog(Direction::Forward);
    
    // 验证：
    EXPECT_FALSE(result); // 1. Jog 指令必须被拒绝 (Shielding 成功)
    
    // 2. ⭐ 关键：意图必须还在！因为 1000.0 的移动任务还没完成
    EXPECT_TRUE(axis.hasPendingCommand()); 

    // ⭐ 新增验证：拒绝原因必须是 AlreadyMoving，而不是被误判成了 InvalidState
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
    
    // 3. 深度验证：意图依然是之前的 Move，而不是被 Jog 覆盖了
    auto command = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<MoveCommand>(command));
    EXPECT_DOUBLE_EQ(std::get<MoveCommand>(command).target, 1000.0);
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
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);

    EXPECT_FALSE(axis.moveRelative(500.0));
    EXPECT_FALSE(axis.hasPendingCommand()); 
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
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


// ---第十组： 绝对位置坐标体系测试 -- 
// 核心约束：所有 Move 意图的闭环都必须基于绝对坐标来判定，而不能被相对坐标误导
TEST(AxisTest, MoveCommandsShouldAlwaysUseAbsoluteSystemForClosure)
{
    Axis axis;
    // 轴在绝对 100，相对 0 (意味着偏移量在 100)
    axis.applyFeedback({AxisState::Idle, 100.0, 0.0, 100.0});

    // 触发绝对定位到 150.0
    axis.moveAbsolute(150.0);

    // 场景：PLC 报告已经停了，相对坐标显示为 50.0，绝对坐标显示为 150.0
    // 虽然相对坐标变成了 50，但我们的 MoveAbsolute 应该盯死 absPos == 150
    axis.applyFeedback({AxisState::Idle, 150.0, 50.0, 100.0});

    EXPECT_FALSE(axis.hasPendingCommand());
}

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


// 4. 验证：绝对定位目标预检
TEST(AxisTest, ShouldRejectMoveAbsoluteWhenTargetExceedsLimit)
{
    Axis axis;
    // 环境：限位 [-100, 1000]，当前在 0.0 (安全区)
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 0.0, .posLimitValue = 1000.0, .negLimitValue = -100.0});

    // 动作：尝试移动到 1001.0
    bool result = axis.moveAbsolute(1001.0);

    EXPECT_FALSE(result);
    // ⭐ 验证：报错原因为“目标超限”，说明逻辑判断点在【参数校验】
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
}

// 5. 验证：相对定位预期终点预检
TEST(AxisTest, ShouldRejectMoveRelativeWhenExpectedTargetExceedsLimit)
{
    Axis axis;
    // 环境：当前在 900.0，限位 1000.0
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 900.0, .posLimitValue = 1000.0});

    // 动作：尝试相对移动 +200.0 (预期 1100.0)
    bool result = axis.moveRelative(200.0);

    EXPECT_FALSE(result);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::TargetOutOfPositiveLimit);
}


// 6. 验证：已在正限位时，拦截所有正向运动
TEST(AxisTest, ShouldBlockMotionWhenAlreadyAtPositiveLimit)
{
    Axis axis;
    // 环境：限位 1000.0，当前轴已经在 1000.0 或 1001.0 了
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 1001.0, .posLimit = true, .posLimitValue = 1000.0});

    // 1. 拦截正向点动
    EXPECT_FALSE(axis.jog(Direction::Forward));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit); // ⭐ 原因是“已在限位”

    // 2. 拦截绝对定位（哪怕目标 500.0 是合法的，由于已超限，系统应先保护）
    EXPECT_FALSE(axis.moveAbsolute(500.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);
}

// 7. 验证：边界“反向逃逸”成功后重置原因
TEST(AxisTest, ShouldResetRejectionWhenEscapingFromLimit)
{
    Axis axis;
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 1001.0, .posLimitValue = 1000.0});

    // 执行反向点动（合法逃逸）
    bool result = axis.jog(Direction::Backward);

    EXPECT_TRUE(result);
    // ⭐ 验证：成功后原因必须重置为 None
    EXPECT_EQ(axis.lastRejection(), RejectionReason::None);
}


// 第十一组: 相对位置坐标体系测试
// 第 1 组：相对坐标反馈同步测试
TEST(AxisTest, ShouldSyncRelativePositionAndZeroBaseFromFeedback)
{
    Axis axis;
    
    // 模拟 PLC 反馈：绝对 150.0，相对 50.0，基准寄存器记录在 100.0
    AxisFeedback fb;
    fb.state = AxisState::Idle;
    fb.absPos = 150.0;
    fb.relPos = 50.0; 
    fb.relZeroAbsPos = 100.0; // 新增字段：基准绝对位置
    
    axis.applyFeedback(fb);

    // 验证双镜像
    EXPECT_DOUBLE_EQ(axis.currentRelativePosition(), 50.0);
    EXPECT_DOUBLE_EQ(axis.relativeZeroAbsolutePosition(), 100.0);
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
// 在 120.0 处设置原点，PLC 必须反馈 relPos ≈ 0 且 relZeroAbsPos ≈ 120.0 意图才消失。
TEST(AxisTest, SetRelativeZeroShouldWaitUntilBothPosAndBaseAreReady)
{
    Axis axis;
    // 初始：绝对位置 120.0，基准还在旧的 0.0，相对位置也是 120.0
    axis.applyFeedback({AxisState::Idle, 120.0, 120.0, 0.0});
    
    // 1. 在 120.0 处发起设置指令
    axis.setRelativeZero(); 
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 A：PLC 逻辑处理中，relPos 已经变 0 了，但基准寄存器还没写进去（还是 0.0）
    // 这种“半完成”状态下，意图必须保持，不能消失
    axis.applyFeedback({AxisState::Idle, 120.0, 0.0, 0.0}); 
    EXPECT_TRUE(axis.hasPendingCommand()); 

    // 场景 B：PLC 终于把基准寄存器也更新为 120.0 了
    axis.applyFeedback({AxisState::Idle, 120.0, 0.0, 120.0});
    
    // 验证：坐标归零 + 基准对齐，双重达标后意图才消失
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第 4 组：清除相对原点的生命周期闭环
// 清除后，PLC 必须反馈 relPos ≈ absPos 且 relZeroAbsPos ≈ 0 意图才消失。
TEST(AxisTest, ClearRelativeZeroShouldWaitUntilRelPosRestoresAndBaseResets)
{
    Axis axis;
    // 初始状态：原点设在了 100.0，所以 abs=100, rel=0, base=100
    axis.applyFeedback({AxisState::Idle, 100.0, 0.0, 100.0});
    
    axis.clearRelativeZero(); 
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 A：PLC 反馈 relPos 已经追平 absPos 了，但基准寄存器还没清零
    axis.applyFeedback({AxisState::Idle, 100.0, 100.0, 100.0});
    EXPECT_TRUE(axis.hasPendingCommand());

    // 场景 B：基准寄存器也归零了（由 PLC 侧逻辑保证清零动作）
    axis.applyFeedback({AxisState::Idle, 100.0, 100.0, 0.0});

    // 验证：双重校验通过，意图消失
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第十二组：软限位功能
// 第 1 组：软限位数值镜像同步
TEST(AxisTest, ShouldSyncSoftLimitValuesFromFeedback)
{
    Axis axis;
    AxisFeedback fb;
    fb.state = AxisState::Idle;
    fb.absPos = 0.0;
    fb.relPos = 0.0;
    fb.relZeroAbsPos = 0.0;
    
    // 模拟 PLC 传回的软限位配置值
    fb.posLimitValue = 5000.0;
    fb.negLimitValue = -100.0;
    
    axis.applyFeedback(fb);

    // 验证 Domain 接口返回的数值是否与 PLC 一致
    EXPECT_DOUBLE_EQ(axis.positiveSoftLimit(), 5000.0);
    EXPECT_DOUBLE_EQ(axis.negativeSoftLimit(), -100.0);
}


// 第 2 组：定位指令的数值越界拦截
TEST(AxisTest, ShouldRejectMoveCommandsExceedingNumericalLimits)
{
    Axis axis;
    // 设定限位范围：[-100, 1000]
    axis.applyFeedback({AxisState::Idle, 1000.0, 1000.0, 0.0, false, false, 1000.0, -100.0});

    // 场景 A：moveAbsolute 目标超出正极限
    EXPECT_FALSE(axis.moveAbsolute(1001.0)); 
    EXPECT_FALSE(axis.hasPendingCommand());

    // 场景 B：moveAbsolute 目标超出负极限
    EXPECT_FALSE(axis.moveAbsolute(-101.0));

    // 场景 C：moveRelative 预期终点超出极限 (当前 0.0 + 增量 1100.0 > 1000.0)
    EXPECT_FALSE(axis.moveRelative(1100.0));
    
    // 场景 D：合法范围内的指令应被接受
    EXPECT_TRUE(axis.moveAbsolute(500.0));
}

// 第 3 组：限位 Bit 触发时的指令锁死与逃逸
TEST(AxisTest, ShouldOnlyAllowReverseJogToRecoverFromLimitBit)
{
    Axis axis;

    // 场景 A：正限位触发 (posLimit = true)
    // 绝对 1000，相对 1000，基准 0，正限位触发，负限位未触发
    axis.applyFeedback({AxisState::Idle, 1000.0, 1000.0, 0.0, true, false, 1000.0, -100.0});

    // 1. 严禁所有定位指令（强制要求手动撤离）
    EXPECT_FALSE(axis.moveAbsolute(500.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);


    EXPECT_FALSE(axis.moveRelative(-10.0));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);

    // 2. 严禁继续向正向点动
    EXPECT_FALSE(axis.jog(Direction::Forward));

    // 3. 允许向反方向（负向）点动以撤离限位区
    EXPECT_TRUE(axis.jog(Direction::Backward));


    // 场景 B：负限位触发 (negLimit = true)
    axis.applyFeedback({AxisState::Idle, -100.0, -100.0, 0.0, false, true, 1000.0, -100.0});

    // 1. 严禁向负向点动
    EXPECT_FALSE(axis.jog(Direction::Backward));
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtNegativeLimit);

    // 2. 允许向正向点动撤离
    EXPECT_TRUE(axis.jog(Direction::Forward));
}


// 第 4 组：运行中触发限位的意图清理
TEST(AxisTest, ShouldCancelMoveIntentImmediatelyWhenLimitIsHitDuringMotion)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle, 0.0, 0.0, 0.0, false, false, 1000.0, -100.0});

    // 发起移动
    axis.moveAbsolute(800.0);
    axis.applyFeedback({AxisState::MovingAbsolute, 100.0});
    ASSERT_TRUE(axis.hasPendingCommand());

    // 模拟意外：在 100.0 处由于某种原因（如软限位动态调整）触发了正限位
    axis.applyFeedback({AxisState::MovingAbsolute, 100.0, 100.0, 0.0, true, false});

    // 验证：意图必须被强制清理，即使轴还没到 800.0
    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第 5 组：数值极限状态下的JOG指令拦截
TEST(AxisTest, ShouldRejectJogWhenAtNumericalLimit)
{
    Axis axis;
    // 环境：当前位置在 1000.0，正限位设定也是 1000.0
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 1000.0, 
        .posLimitValue = 1000.0
    });

    // 动作：尝试向正向点动
    bool result = axis.jog(Direction::Forward);

    // 验证：应当因为“已在正限位”被拦截
    EXPECT_FALSE(result);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AtPositiveLimit);
}


// 第十三组：使能 (Enable/Disable) 语义约束测试

// 1. 意图产生：Disabled 时申请上电
TEST(AxisTest, ShouldCreateEnableIntentWhenDisabled)
{
    Axis axis;
    axis.applyFeedback({AxisState::Disabled});

    bool result = axis.enable(true); // 申请上电

    EXPECT_TRUE(result);
    auto cmd = axis.getPendingCommand();
    ASSERT_TRUE(std::holds_alternative<EnableCommand>(cmd));
    EXPECT_TRUE(std::get<EnableCommand>(cmd).active);
}

// 2. 安全屏障：Error 状态下严禁上电
TEST(AxisTest, ShouldRejectEnableWhenInErrorState)
{
    Axis axis;
    axis.applyFeedback({AxisState::Error});

    bool result = axis.enable(true);

    EXPECT_FALSE(result);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 3. 安全屏障：运动中严禁直接断电
TEST(AxisTest, ShouldRejectDisableWhenMoving)
{
    Axis axis;
    axis.applyFeedback({AxisState::MovingAbsolute});

    bool result = axis.enable(false); // 尝试运行中直接断电

    EXPECT_FALSE(result);
    // 应当提示“已经在动”，不能强制切断动力
    EXPECT_EQ(axis.lastRejection(), RejectionReason::AlreadyMoving);
}

// 4. 幂等性：已经是 Idle 时再次上电不产生新意图
TEST(AxisTest, ShouldBeIdempotentWhenEnablingAlreadyActiveAxis)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});

    bool result = axis.enable(true);

    EXPECT_TRUE(result); // 业务逻辑返回真（已经是你要的状态）
    EXPECT_FALSE(axis.hasPendingCommand()); // ⭐ 但不产生任何发送给 PLC 的指令
}

// 5. 生命周期闭环：上电意图在状态变为 Idle 后消失
TEST(AxisTest, EnableIntentShouldClearWhenStateBecomesIdle)
{
    Axis axis;
    axis.applyFeedback({AxisState::Disabled});
    axis.enable(true);
    ASSERT_TRUE(axis.hasPendingCommand());

    // 模拟 PLC 响应：状态变为 Idle
    axis.applyFeedback({AxisState::Idle});

    // 意图应当消失
    EXPECT_FALSE(axis.hasPendingCommand());
}

// 6. 生命周期闭环：掉电意图在状态变为 Disabled 后消失
TEST(AxisTest, DisableIntentShouldClearWhenStateBecomesDisabled)
{
    Axis axis;
    axis.applyFeedback({AxisState::Idle});
    axis.enable(false); // 申请掉电

    axis.applyFeedback({AxisState::Disabled});

    EXPECT_FALSE(axis.hasPendingCommand());
}


// 第十四组：单独停止接口测试 (stopJog)

TEST(AxisTest, ShouldGenerateFalseActiveCommandWhenStopJogCalled)
{
    Axis axis;
    axis.applyFeedback({.state = AxisState::Idle, .posLimitValue = 1000.0});
    
    // 1. 先启动一个正向点动
    axis.jog(Direction::Forward);
    
    // 2. 调用新接口停止
    bool result = axis.stopJog(Direction::Forward);

    // 验证：
    EXPECT_TRUE(result);
    auto cmd = std::get<JogCommand>(axis.getPendingCommand());
    EXPECT_EQ(cmd.dir, Direction::Forward); // 应该能记住之前的方向
    EXPECT_FALSE(cmd.active);               // 必须是停止信号
}

TEST(AxisTest, StopJogShouldWorkEvenInErrorState)
{
    Axis axis;
    // 模拟轴在点动时突然报错
    axis.applyFeedback({AxisState::Error}); 

    // 动作：松开按钮触发停止
    bool result = axis.stopJog(Direction::Forward);

    // 验证：即使报错也必须允许停止（下发 false 信号给 PLC）
    EXPECT_TRUE(result);
    EXPECT_FALSE(std::get<JogCommand>(axis.getPendingCommand()).active);
}


// 速度模式相关测试
// 1. Idle 时允许发起命令
TEST(AxisTest, SetJogVelocityShouldEmitCommandWhenAxisIsIdle)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Idle
    });

    bool ok = axis.setJogVelocity(100.0);

    EXPECT_TRUE(ok);

    auto intent = axis.getPendingCommand();

    ASSERT_TRUE(std::holds_alternative<SetJogVelocityCommand>(intent));
    EXPECT_DOUBLE_EQ(std::get<SetJogVelocityCommand>(intent).velocity, 100.0);
}


// 2. Disabled → 也允许
TEST(AxisTest, SetJogVelocityShouldWorkEvenWhenAxisIsDisabled)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Disabled
    });

    bool ok = axis.setJogVelocity(100.0);

    EXPECT_TRUE(ok);
}

// 3. Moving → 必须拒绝
TEST(AxisTest, SetJogVelocityShouldBeRejectedWhenAxisIsMoving)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::MovingAbsolute
    });

    bool ok = axis.setJogVelocity(100.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}

// 5. Error → 必须拒绝
TEST(AxisTest, SetJogVelocityShouldWorkEvenInErrorState)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Error
    });

    bool ok = axis.setJogVelocity(100.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidState);
}


// 6. 命令内容正确
TEST(AxisTest, SetJogVelocityShouldGenerateCorrectCommand)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Idle
    });

    axis.setJogVelocity(123.0);

    auto intent = axis.getPendingCommand();
    auto cmd = std::get<SetJogVelocityCommand>(intent);

    EXPECT_DOUBLE_EQ(cmd.velocity, 123.0);
}


// 7. Feedback 更新（PLC 才是真相）
TEST(AxisTest, JogVelocityShouldUpdateFromFeedback)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Idle,
        .getjogVelocity = 150.0
    });

    EXPECT_DOUBLE_EQ(axis.getjogVelocity(), 150.0);
}


// 8. 读取必须不依赖状态
TEST(AxisTest, JogVelocityShouldBeReadableAnytime)
{
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::MovingAbsolute,
        .getjogVelocity = 180.0
    });

    EXPECT_DOUBLE_EQ(axis.getjogVelocity(), 180.0);
}

// 9. 速度为零时必须拒绝
TEST(AxisTest, SetJogVelocityShouldBeRejectedWhenVelocityIsZero)
{
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Idle });

    bool ok = axis.setJogVelocity(0.0);

    EXPECT_FALSE(ok);
    EXPECT_EQ(axis.lastRejection(), RejectionReason::InvalidArgument);
}

// 10. 速度为负数时必须拒绝
TEST(AxisTest, SetJogVelocityShouldBeRejectedWhenVelocityIsNegative)
{
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Idle });

    bool ok = axis.setJogVelocity(-10.0);

    EXPECT_FALSE(ok);
}

// 11. 速度为负数时必须拒绝
TEST(AxisTest, SetMoveVelocityShouldBeRejectedWhenVelocityIsNegative)
{
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Idle });

    bool ok = axis.setMoveVelocity(-10.0);

    EXPECT_FALSE(ok);
}