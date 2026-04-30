#include <gtest/gtest.h>
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "entity/Axis.h"

// ============================================================================
// 多轴 FakePLC 测试套件
// 核心验证点：FakePLC 从"单一寄存器集合"升级为"基于 AxisId 寻址的寄存器组集合"
// ============================================================================

class FakePLCTest : public ::testing::Test {
protected:
    FakePLC plc;
};

// ============================================================================
// 用例 1：多轴使能时序隔离
// 验证：Y 使能时，Z 不受影响保持 Disabled；各自独立完成使能延迟
// ============================================================================
TEST_F(FakePLCTest, ShouldEnableAxisIndependently) {
    // 初始状态：所有轴均为 Disabled
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Disabled);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    // 仅对 Y 下发使能
    plc.onCommand(AxisId::Y, EnableCommand{true});

    // 推进 10ms，Y 还未完成使能延迟，Z 保持 Disabled
    plc.tick(10);
    EXPECT_NE(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    // 推进使能延迟到达 (150ms)
    plc.tick(200);

    // Y 必须进入 Idle，Z 必须保持 Disabled
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);
}

// ============================================================================
// 用例 2：多轴点动运动隔离
// 验证：Y 正向点动时，Z 必须静止不动
// ============================================================================
TEST_F(FakePLCTest, ShouldJogAxisWithoutAffectingOthers) {
    // 初始化：使能 Y 和 Z
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200); // 等待使能完成

    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);

    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

    double yPosBefore = plc.getFeedback(AxisId::Y).absPos;
    double zPosBefore = plc.getFeedback(AxisId::Z).absPos;

    // 仅对 Y 启动点动
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.tick(100); // 100ms，Y 应移动 1.0，Z 应保持不动

    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);

    EXPECT_EQ(yFb.state, AxisState::Jogging);
    EXPECT_EQ(zFb.state, AxisState::Idle);
    EXPECT_NEAR(yFb.absPos, yPosBefore + 1.0, 0.001);
    EXPECT_NEAR(zFb.absPos, zPosBefore, 0.001);

    // 停止 Y 点动
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, false});
    plc.tick(50);

    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);
}

// ============================================================================
// 用例 3：多轴绝对定位隔离
// 验证：Y 向 100 运动时，Z 保持静止不动
// ============================================================================
TEST_F(FakePLCTest, ShouldMoveAxisIndependently) {
    // 初始化：使能 Y
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);

    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);

    double zPosBefore = plc.getFeedback(AxisId::Z).absPos;

    // Y 发起绝对定位到 100.0
    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plc.tick(2000); // 2 秒，Y 应到达目标并回 Idle

    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);

    EXPECT_EQ(yFb.state, AxisState::Idle);
    EXPECT_NEAR(yFb.absPos, 100.0, 0.01);
    // Z 必须保持不动
    EXPECT_NEAR(zFb.absPos, zPosBefore, 0.001);
    EXPECT_EQ(zFb.state, AxisState::Disabled);
}

// ============================================================================
// 用例 4：多轴独立限位
// 验证：Y 正限位触发(100)时，Z 不受影响仍可自由运动
// ============================================================================
TEST_F(FakePLCTest, ShouldHandleAxisLimitIndependently) {
    // 初始化 Y
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);

    // 仅对 Y 设置限位 100，Z 不限
    plc.setLimits(AxisId::Y, 100.0, -100.0);
    plc.setSimulatedMoveVelocity(AxisId::Y, 80.0);
    plc.setSimulatedMoveVelocity(AxisId::Z, 80.0);

    // Y 尝试走到 150（会撞限位）
    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 150.0, 0.0});
    plc.tick(2000); // 足够时间撞上 100 限位

    auto yFb = plc.getFeedback(AxisId::Y);
    EXPECT_TRUE(yFb.posLimit) << "Y should hit positive limit!";
    EXPECT_LE(yFb.absPos, 100.01) << "Y should be clamped at 100!";
    EXPECT_EQ(yFb.state, AxisState::Idle);

    // 验证 Z 完全没有受影响
    auto zFb = plc.getFeedback(AxisId::Z);
    EXPECT_FALSE(zFb.posLimit) << "Z should NOT have limit triggered!";
    EXPECT_NEAR(zFb.absPos, 0.0, 0.001) << "Z should remain at 0!";
    EXPECT_EQ(zFb.state, AxisState::Idle);
}

// ============================================================================
// 用例 5：多轴独立停轴命令
// 验证：停止 Y 运动时，Z 运动持续不受影响
// ============================================================================
TEST_F(FakePLCTest, ShouldStopSpecificAxisOnly) {
    // 使能 Y 和 Z
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);

    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

    // Y 和 Z 同时向前点动
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.onCommand(AxisId::Z, JogCommand{Direction::Forward, true});
    plc.tick(100);

    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Jogging);
    ASSERT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Jogging);

    double zPosBeforeStop = plc.getFeedback(AxisId::Z).absPos;

    // 仅停 Y
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, false});
    plc.tick(50);

    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);

    // Z 必须继续运动
    auto zFb = plc.getFeedback(AxisId::Z);
    EXPECT_EQ(zFb.state, AxisState::Jogging);
    EXPECT_GT(zFb.absPos, zPosBeforeStop) << "Z should keep moving forward!";
}

// ============================================================================
// 用例 6：FakeAxisDriver 集成 — 通过 Driver.send 验证 AxisId 路由正确性
// ============================================================================
TEST_F(FakePLCTest, ShouldRouteAxisIdCorrectlyThroughDriver) {
    FakeAxisDriver driver(plc);

    // 通过 driver 发送使能到 Y
    driver.send(AxisId::Y, EnableCommand{true});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    // 通过 driver 发送使能到 Z
    driver.send(AxisId::Z, EnableCommand{true});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);
}

// ============================================================================
// 用例 7：多轴 Jog 速度独立配置
// ============================================================================
TEST_F(FakePLCTest, ShouldConfigureJogVelocityIndependently) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);

    // Y 用 100 unit/s，Z 用 10 unit/s
    plc.setSimulatedJogVelocity(AxisId::Y, 100.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

    // 同时开始点动
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.onCommand(AxisId::Z, JogCommand{Direction::Forward, true});
    plc.tick(100); // 100ms

    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);

    // Y 走了约 10.0，Z 走了约 1.0
    EXPECT_GT(yFb.absPos, zFb.absPos * 5) << "Y should be much faster than Z!";
}

// ============================================================================
// 用例 8：迁移旧的单轴测试 — 使用 AxisId::Y 作为默认轴
// ============================================================================
TEST_F(FakePLCTest, ShouldDelayStateTransitionWhenEnabling) {
    // 迁移自旧的 ShouldDelayStateTransitionWhenEnabling
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Disabled);

    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.tick(1);
    EXPECT_NE(plc.getFeedback(AxisId::Y).state, AxisState::Idle);

    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
}

TEST_F(FakePLCTest, ShouldUpdatePositionContinuouslyDuringJog) {
    // 迁移自旧的 ShouldUpdatePositionContinuouslyDuringJog
    plc.forceState(AxisId::Y, AxisState::Idle);
    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);

    double initialPos = plc.getFeedback(AxisId::Y).absPos;

    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.tick(100);

    auto fb = plc.getFeedback(AxisId::Y);
    EXPECT_EQ(fb.state, AxisState::Jogging);
    EXPECT_NEAR(fb.absPos, initialPos + 1.0, 0.001);

    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, false});
    plc.tick(50);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
}

TEST_F(FakePLCTest, ShouldReturnToIdleOnlyWhenTargetReached) {
    // 迁移自旧的 ShouldReturnToIdleOnlyWhenTargetReached
    plc.forceState(AxisId::Y, AxisState::Idle);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);

    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plc.tick(1000);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::MovingAbsolute);
    EXPECT_NEAR(plc.getFeedback(AxisId::Y).absPos, 50.0, 0.001);

    plc.tick(1000);
    auto fb = plc.getFeedback(AxisId::Y);
    EXPECT_NEAR(fb.absPos, 100.0, 0.01);
    EXPECT_EQ(fb.state, AxisState::Idle);
}

TEST_F(FakePLCTest, ShouldTriggerPosLimitAndStopWhenExceedingLimitValue) {
    // 迁移自旧的 ShouldTriggerPosLimitAndStopWhenExceedingLimitValue
    plc.forceState(AxisId::Y, AxisState::Idle);
    plc.setLimits(AxisId::Y, 100.0, -100.0);
    plc.setSimulatedMoveVelocity(AxisId::Y, 100.0);

    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 150.0, 0.0});
    plc.tick(2000);

    auto fb = plc.getFeedback(AxisId::Y);
    EXPECT_TRUE(fb.posLimit);
    EXPECT_LE(fb.absPos, 100.01);
    EXPECT_EQ(fb.state, AxisState::Idle);
}

// ============================================================================
// 用例 9：所有 AxisId 轴默认初始为 Disabled 且位置归零
// ============================================================================
TEST_F(FakePLCTest, ShouldInitializeAllAxesToDisabledZeroPosition) {
    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);
    auto rFb = plc.getFeedback(AxisId::R);
    auto x1Fb = plc.getFeedback(AxisId::X1);
    auto x2Fb = plc.getFeedback(AxisId::X2);

    EXPECT_EQ(yFb.state, AxisState::Disabled);
    EXPECT_EQ(zFb.state, AxisState::Disabled);
    EXPECT_EQ(rFb.state, AxisState::Disabled);
    EXPECT_EQ(x1Fb.state, AxisState::Disabled);
    EXPECT_EQ(x2Fb.state, AxisState::Disabled);

    EXPECT_DOUBLE_EQ(yFb.absPos, 0.0);
    EXPECT_DOUBLE_EQ(zFb.absPos, 0.0);
    EXPECT_DOUBLE_EQ(rFb.absPos, 0.0);
    EXPECT_DOUBLE_EQ(x1Fb.absPos, 0.0);
    EXPECT_DOUBLE_EQ(x2Fb.absPos, 0.0);
}
 
