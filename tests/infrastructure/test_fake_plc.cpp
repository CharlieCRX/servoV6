#include <gtest/gtest.h>
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "domain/entity/Axis.h"

// ============================================================================
// 分组隔离测试套件
// 核心验证点：两个独立 FakePLC 实例之间完全隔离 ——
//    GroupA 的指令不会影响 GroupB 的轴状态
// ============================================================================

class FakePLCGroupIsolationTest : public ::testing::Test {
protected:
    FakePLC plcA;  // 代表 Machine_A 的 PLC
    FakePLC plcB;  // 代表 Machine_B 的 PLC
};

// ============================================================================
// 用例 1：多实例使能隔离
// 验证：A 组使能 Y 轴时，B 组的 Y 轴不受影响
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldIsolateEnableAcrossGroups) {
    // 初始状态：两组均为 Disabled
    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Disabled);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Disabled);

    // 仅 A 组使能 Y 轴
    plcA.onCommand(AxisId::Y, EnableCommand{true});
    plcA.tick(200); // 推进 A 组物理引擎

    // A 组应进入 Idle，B 组应保持 Disabled
    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Disabled);
}

// ============================================================================
// 用例 2：多实例运动隔离
// 验证：A 组 Y 轴运动时，B 组的 Y 轴位置不受影响
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldIsolateMotionAcrossGroups) {
    // 两组都使能
    plcA.onCommand(AxisId::Y, EnableCommand{true});
    plcB.onCommand(AxisId::Y, EnableCommand{true});
    plcA.tick(200);
    plcB.tick(200);

    ASSERT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);

    // A 组 Y 轴运动到 100
    plcA.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plcA.tick(2000);

    // A 组应到达 100，B 组应仍在 0
    EXPECT_NEAR(plcA.getFeedback(AxisId::Y).absPos, 100.0, 0.01);
    EXPECT_NEAR(plcB.getFeedback(AxisId::Y).absPos, 0.0, 0.001);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);
}

// ============================================================================
// 用例 3：多实例限位隔离
// 验证：A 组设置限位不会影响 B 组
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldIsolateLimitsAcrossGroups) {
    plcA.onCommand(AxisId::Y, EnableCommand{true});
    plcB.onCommand(AxisId::Y, EnableCommand{true});
    plcA.tick(200);
    plcB.tick(200);

    // 仅 A 组设置限位 50
    plcA.setLimits(AxisId::Y, 50.0, -100.0);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 80.0);
    plcB.setSimulatedMoveVelocity(AxisId::Y, 80.0);

    // A 组试图走到 100（撞限位），B 组试图走到 100（不限位）
    plcA.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plcB.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});

    plcA.tick(2000);
    plcB.tick(2000);

    // A 组应被限位截停在 50
    EXPECT_TRUE(plcA.getFeedback(AxisId::Y).posLimit);
    EXPECT_LE(plcA.getFeedback(AxisId::Y).absPos, 50.01);

    // B 组应自由到达 100
    EXPECT_NEAR(plcB.getFeedback(AxisId::Y).absPos, 100.0, 0.01);
    EXPECT_FALSE(plcB.getFeedback(AxisId::Y).posLimit);
}

// ============================================================================
// 用例 4：多实例 resetAll 隔离
// 验证：resetAll 只影响被调用的实例
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldResetOnlyTargetInstance) {
    plcA.onCommand(AxisId::Y, EnableCommand{true});
    plcB.onCommand(AxisId::Y, EnableCommand{true});
    plcA.tick(200);
    plcB.tick(200);

    ASSERT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);

    // 仅重置 A 组
    plcA.resetAll();

    // A 组应回到 Disabled，B 组仍为 Idle
    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Disabled);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);
}

// ============================================================================
// 用例 5：FakeAxisDriver 分组路由正确性
// 验证：driverA.send 只路由到 plcA，driverB.send 只路由到 plcB
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldRouteDriverToCorrectPLC) {
    FakeAxisDriver driverA(plcA);
    FakeAxisDriver driverB(plcB);

    // driverA 使能 Y
    driverA.send(AxisId::Y, EnableCommand{true});
    plcA.tick(200);

    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Disabled);

    // driverB 使能 Y（只影响 plcB）
    driverB.send(AxisId::Y, EnableCommand{true});
    plcB.tick(200);

    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);

    // 验证 driver 各自的 history 也是隔离的
    EXPECT_EQ(driverA.history.size(), 1u);
    EXPECT_EQ(driverB.history.size(), 1u);
    EXPECT_EQ(driverA.history[0].id, AxisId::Y);
    EXPECT_EQ(driverB.history[0].id, AxisId::Y);
}


// ============================================================================
// 多轴运动隔离测试套件（继承旧版单实例测试）
// 核心验证点：同一 FakePLC 内，不同 AxisId 的轴各自独立运动
// ============================================================================

class FakePLCMultiAxisTest : public ::testing::Test {
protected:
    FakePLC plc;
};

// ============================================================================
// 用例 6：多轴使能时序隔离
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldEnableAxisIndependently) {
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
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);
}

// ============================================================================
// 用例 7：多轴点动运动隔离
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldJogAxisWithoutAffectingOthers) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);

    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);

    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

    double yPosBefore = plc.getFeedback(AxisId::Y).absPos;
    double zPosBefore = plc.getFeedback(AxisId::Z).absPos;

    // 仅对 Y 启动点动
    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.tick(100);

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
// 用例 8：多轴绝对定位隔离
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldMoveAxisIndependently) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);

    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);

    double zPosBefore = plc.getFeedback(AxisId::Z).absPos;

    // Y 发起绝对定位到 100.0
    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plc.tick(2000);

    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);

    EXPECT_EQ(yFb.state, AxisState::Idle);
    EXPECT_NEAR(yFb.absPos, 100.0, 0.01);
    EXPECT_NEAR(zFb.absPos, zPosBefore, 0.001);
    EXPECT_EQ(zFb.state, AxisState::Disabled);
}

// ============================================================================
// 用例 9：多轴独立限位
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldHandleAxisLimitIndependently) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);

    plc.setLimits(AxisId::Y, 100.0, -100.0);
    plc.setSimulatedMoveVelocity(AxisId::Y, 80.0);
    plc.setSimulatedMoveVelocity(AxisId::Z, 80.0);

    // Y 尝试走到 150（会撞限位）
    plc.onCommand(AxisId::Y, MoveCommand{MoveType::Absolute, 150.0, 0.0});
    plc.tick(2000);

    auto yFb = plc.getFeedback(AxisId::Y);
    EXPECT_TRUE(yFb.posLimit) << "Y should hit positive limit!";
    EXPECT_LE(yFb.absPos, 100.01) << "Y should be clamped at 100!";
    EXPECT_EQ(yFb.state, AxisState::Idle);

    auto zFb = plc.getFeedback(AxisId::Z);
    EXPECT_FALSE(zFb.posLimit) << "Z should NOT have limit triggered!";
    EXPECT_NEAR(zFb.absPos, 0.0, 0.001) << "Z should remain at 0!";
    EXPECT_EQ(zFb.state, AxisState::Idle);
}

// ============================================================================
// 用例 10：多轴独立停轴命令
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldStopSpecificAxisOnly) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);

    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

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

    auto zFb = plc.getFeedback(AxisId::Z);
    EXPECT_EQ(zFb.state, AxisState::Jogging);
    EXPECT_GT(zFb.absPos, zPosBeforeStop) << "Z should keep moving forward!";
}

// ============================================================================
// 用例 11：FakeAxisDriver 集成 — 通过 Driver.send 验证 AxisId 路由正确性
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldRouteAxisIdCorrectlyThroughDriver) {
    FakeAxisDriver driver(plc);

    driver.send(AxisId::Y, EnableCommand{true});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    driver.send(AxisId::Z, EnableCommand{true});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Idle);
}

// ============================================================================
// 用例 12：多轴 Jog 速度独立配置
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldConfigureJogVelocityIndependently) {
    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.onCommand(AxisId::Z, EnableCommand{true});
    plc.tick(200);

    plc.setSimulatedJogVelocity(AxisId::Y, 100.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 10.0);

    plc.onCommand(AxisId::Y, JogCommand{Direction::Forward, true});
    plc.onCommand(AxisId::Z, JogCommand{Direction::Forward, true});
    plc.tick(100);

    auto yFb = plc.getFeedback(AxisId::Y);
    auto zFb = plc.getFeedback(AxisId::Z);

    EXPECT_GT(yFb.absPos, zFb.absPos * 5) << "Y should be much faster than Z!";
}

// ============================================================================
// 用例 13：使能延迟
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldDelayStateTransitionWhenEnabling) {
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Disabled);

    plc.onCommand(AxisId::Y, EnableCommand{true});
    plc.tick(1);
    EXPECT_NE(plc.getFeedback(AxisId::Y).state, AxisState::Idle);

    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
}

// ============================================================================
// 用例 14：点动时位置连续更新
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldUpdatePositionContinuouslyDuringJog) {
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

// ============================================================================
// 用例 15：定位到达目标后回 Idle
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldReturnToIdleOnlyWhenTargetReached) {
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

// ============================================================================
// 用例 16：限位触发停止
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldTriggerPosLimitAndStopWhenExceedingLimitValue) {
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
// 用例 17：所有 AxisId 轴默认初始为 Disabled 且位置归零
// ============================================================================
TEST_F(FakePLCMultiAxisTest, ShouldInitializeAllAxesToDisabledZeroPosition) {
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
