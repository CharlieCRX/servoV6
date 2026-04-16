#include <gtest/gtest.h>
#include "infrastructure/FakePLC.h"
#include "entity/Axis.h"

class FakePLCTest : public ::testing::Test {
protected:
    FakePLC plc;
};

// 🔴 红灯案例 1：使能操作的时序延迟 (Async Enable)
TEST_F(FakePLCTest, ShouldDelayStateTransitionWhenEnabling) {
    // 初始状态应为 Disabled
    EXPECT_EQ(plc.getFeedback().state, AxisState::Disabled);

    // 下发上电意图
    plc.onCommand(EnableCommand{true});
    
    // 刚下发命令，时间未流逝，状态不应直接变成 Idle
    plc.tick(1); 
    EXPECT_NE(plc.getFeedback().state, AxisState::Idle);

    // 经过足够长的物理时间 (200ms)
    plc.tick(200);
    
    // 状态必须收敛为 Idle
    EXPECT_EQ(plc.getFeedback().state, AxisState::Idle);
}

// 🔴 红灯案例 2：Jog 模式的连续物理仿真 (Continuous Kinematics)
TEST_F(FakePLCTest, ShouldUpdatePositionContinuouslyDuringJog) {
    plc.forceState(AxisState::Idle); // 强制进入 Idle 以便测试点动
    plc.setSimulatedJogVelocity(10.0);  // 设置速度为 10.0 units/s

    double initialPos = plc.getFeedback().absPos;

    // 开始正向点动
    plc.onCommand(JogCommand{Direction::Forward, true});
    
    // 推进 100ms，预期移动 1.0 unit
    plc.tick(100);
    
    auto fb = plc.getFeedback();
    EXPECT_EQ(fb.state, AxisState::Jogging);
    EXPECT_NEAR(fb.absPos, initialPos + 1.0, 0.001); 

    // 停止点动
    plc.onCommand(JogCommand{Direction::Forward, false});
    plc.tick(50); 
    
    EXPECT_EQ(plc.getFeedback().state, AxisState::Idle);
}

// 🔴 红灯案例 3：Move 指令的闭环收敛判定 (Move Convergence)
TEST_F(FakePLCTest, ShouldReturnToIdleOnlyWhenTargetReached) {
    plc.forceState(AxisState::Idle);
    plc.setSimulatedMoveVelocity(50.0);

    // 发起绝对定位，目标 100.0
    plc.onCommand(MoveCommand{MoveType::Absolute, 100.0, 0.0});
    
    plc.tick(1000); // 1秒应跑 50.0
    EXPECT_EQ(plc.getFeedback().state, AxisState::MovingAbsolute);
    EXPECT_NEAR(plc.getFeedback().absPos, 50.0, 0.001);

    // 再推进 1 秒到达目标
    plc.tick(1000); 
    
    auto fb = plc.getFeedback();
    EXPECT_NEAR(fb.absPos, 100.0, 0.01);
    EXPECT_EQ(fb.state, AxisState::Idle); // 物理到位后自动切回 Idle
}

// 🔴 红灯案例 4：硬件限位触发与物理截断 (Limit Interlock)
TEST_F(FakePLCTest, ShouldTriggerPosLimitAndStopWhenExceedingLimitValue) {
    plc.forceState(AxisState::Idle);
    plc.setLimits(100.0, -100.0); // 设置正限位 100.0
    plc.setSimulatedMoveVelocity(100.0);

    // 尝试移向 150.0
    plc.onCommand(MoveCommand{MoveType::Absolute, 150.0, 0.0});
    
    plc.tick(2000); // 给足够时间跑过 100.0

    auto fb = plc.getFeedback();
    EXPECT_TRUE(fb.posLimit);         // 限位位必须置 1
    EXPECT_LE(fb.absPos, 100.01);     // 坐标被截断在 100 附近
    EXPECT_EQ(fb.state, AxisState::Idle); // 运动强制停止
}