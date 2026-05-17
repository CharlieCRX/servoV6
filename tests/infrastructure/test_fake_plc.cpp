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
    plcA.tick(200); // 推进 A 组物理引擎（使能延迟 150ms）

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

    // A 组应回到 Disabled（含龙门状态重置），B 组仍为 Idle
    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Disabled);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Idle);

    // 验证龙门反馈也被重置
    auto gf = plcA.getGantryFeedback();
    EXPECT_FALSE(gf.enable);
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 5：FakeAxisDriver 分组路由正确性
// 验证：driverA.send 只路由到 plcA，driverB.send 只路由到 plcB
// ============================================================================
TEST_F(FakePLCGroupIsolationTest, ShouldRouteDriverToCorrectPLC) {
    FakeAxisDriver driverA(plcA);
    FakeAxisDriver driverB(plcB);

    // driverA 使能 Y
    driverA.send(AxisCommandWithId{AxisId::Y, EnableCommand{true}});
    plcA.tick(200);

    EXPECT_EQ(plcA.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plcB.getFeedback(AxisId::Y).state, AxisState::Disabled);

    // driverB 使能 Y（只影响 plcB）
    driverB.send(AxisCommandWithId{AxisId::Y, EnableCommand{true}});
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
// 多轴运动隔离测试套件
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

    // 推进 10ms，Y 还未完成使能延迟（需要 150ms），Z 保持 Disabled
    plc.tick(10);
    EXPECT_NE(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    // 推进使能延迟到达（总 210ms > 150ms）
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
    plc.onCommand(AxisId::Y, StopCommand{});
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

    driver.send(AxisCommandWithId{AxisId::Y, EnableCommand{true}});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::Y).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::Z).state, AxisState::Disabled);

    driver.send(AxisCommandWithId{AxisId::Z, EnableCommand{true}});
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
// 验证：使能命令下达后，经过 ENABLE_DELAY_MS (150ms) 后轴才进入 Idle
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
    // 6 个轴全部验证（含 X 轴）
    auto yFb  = plc.getFeedback(AxisId::Y);
    auto zFb  = plc.getFeedback(AxisId::Z);
    auto rFb  = plc.getFeedback(AxisId::R);
    auto xFb  = plc.getFeedback(AxisId::X);
    auto x1Fb = plc.getFeedback(AxisId::X1);
    auto x2Fb = plc.getFeedback(AxisId::X2);

    EXPECT_EQ(yFb.state,  AxisState::Disabled);
    EXPECT_EQ(zFb.state,  AxisState::Disabled);
    EXPECT_EQ(rFb.state,  AxisState::Disabled);
    EXPECT_EQ(xFb.state,  AxisState::Disabled);
    EXPECT_EQ(x1Fb.state, AxisState::Disabled);
    EXPECT_EQ(x2Fb.state, AxisState::Disabled);

    EXPECT_DOUBLE_EQ(yFb.absPos,  0.0);
    EXPECT_DOUBLE_EQ(zFb.absPos,  0.0);
    EXPECT_DOUBLE_EQ(rFb.absPos,  0.0);
    EXPECT_DOUBLE_EQ(xFb.absPos,  0.0);
    EXPECT_DOUBLE_EQ(x1Fb.absPos, 0.0);
    EXPECT_DOUBLE_EQ(x2Fb.absPos, 0.0);
}


// ============================================================================
// 龙门联动测试套件
// 核心验证点：
//   1. 龙门使能（GantryPowerCommand）同步控制 X1/X2 使能
//   2. 联动条件检查（6 项前置条件）与拒绝逻辑
//   3. 联动建立后持续监测（超差/报警/掉电→自动解耦）
//   4. 联动/分动互斥（联动态下拒绝 X1/X2 独立点动/定位）
//   5. 龙门反馈 enable 标志从 X1/X2 真实物理状态聚合
//   6. forceGantryFeedback 锁定机制
// ============================================================================

class FakePLCGantryTest : public ::testing::Test {
protected:
    FakePLC plc;

    /// @brief 辅助：将 X1 和 X2 都使能并确保进入 Idle
    void enableX1AndX2() {
        plc.onCommand(AxisId::X1, EnableCommand{true});
        plc.onCommand(AxisId::X2, EnableCommand{true});
        plc.tick(200);
        ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Idle);
        ASSERT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Idle);
    }

    /// @brief 辅助：将 X1 和 X2 位置对齐（设置为相同值）
    void alignX1X2Position(double pos = 0.0) {
        plc.setAbsolutePosition(AxisId::X1, pos);
        plc.setAbsolutePosition(AxisId::X2, pos);
    }

    /// @brief 辅助：使能 X1/X2 并对齐位置，做好联动准备
    void prepareForCoupling() {
        enableX1AndX2();
        alignX1X2Position(0.0);
    }
};

// ============================================================================
// 用例 G1：龙门电机使能——GantryPowerCommand{true} 同步使能 X1 和 X2
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldEnableX1AndX2ViaGantryPowerCommand) {
    EXPECT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Disabled);
    EXPECT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Disabled);

    // 下发龙门使能命令
    plc.onGantryCommand(GantryPowerCommand{true});
    plc.tick(200); // 龙门使能延迟 150ms

    EXPECT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Idle);
    EXPECT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Idle);

    // 龙门反馈 enable 应从 X1/X2 真实状态聚合
    auto gf = plc.getGantryFeedback();
    EXPECT_TRUE(gf.enable);
}

// ============================================================================
// 用例 G2：龙门电机掉电——GantryPowerCommand{false} 同步掉电 X1 和 X2
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldDisableX1AndX2ViaGantryPowerCommand) {
    enableX1AndX2();
    ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Idle);

    plc.onGantryCommand(GantryPowerCommand{false});
    plc.tick(200); // 龙门掉电延迟 150ms

    EXPECT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Disabled);
    EXPECT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Disabled);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.enable);
}

// ============================================================================
// 用例 G3：联动成功——X1/X2 使能、静止、位置对齐 → isCoupled=true, errorCode=0
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldCoupleWhenAllConditionsMet) {
    prepareForCoupling();

    // 请求联动
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150); // 联动延迟 100ms

    auto gf = plc.getGantryFeedback();
    EXPECT_TRUE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 G4：X1 未使能时请求联动 → 被拒绝，errorCode=2 (X1NotEnabled)
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectCouplingWhenX1NotEnabled) {
    // X2 使能，但 X1 未使能
    plc.onCommand(AxisId::X2, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Disabled);

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 2); // X1NotEnabled
}

// ============================================================================
// 用例 G5：X2 未使能时请求联动 → 被拒绝，errorCode=3 (X2NotEnabled)
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectCouplingWhenX2NotEnabled) {
    // X1 使能，但 X2 未使能
    plc.onCommand(AxisId::X1, EnableCommand{true});
    plc.tick(200);
    ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Idle);
    ASSERT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Disabled);

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 3); // X2NotEnabled
}

// ============================================================================
// 用例 G6：X1 运动中请求联动 → 被拒绝，errorCode=4 (X1NotStationary)
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectCouplingWhenX1Moving) {
    enableX1AndX2();
    alignX1X2Position(0.0);

    // 让 X1 开始点动
    plc.setSimulatedJogVelocity(AxisId::X1, 10.0);
    plc.onCommand(AxisId::X1, JogCommand{Direction::Forward, true});
    plc.tick(50);
    ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Jogging);

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 4); // X1NotStationary
}

// ============================================================================
// 用例 G7：位置差过大请求联动 → 被拒绝，errorCode=1 (PositionToleranceExceeded)
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectCouplingWhenPositionDeltaExceeded) {
    enableX1AndX2();

    // X1 在 0.0，X2 在 5.0 → 位置差 5.0 > 0.1 (GANTRY_MAX_POSITION_DELTA)
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 5.0);

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 1); // PositionToleranceExceeded
}

// ============================================================================
// 用例 G8：位置差恰好满足阈值 → 联动成功（边界值）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldCoupleWhenPositionDeltaAtThreshold) {
    enableX1AndX2();

    // X1 在 0.0，X2 在 0.099 → 位置差 0.099 < 0.1
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.099);

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_TRUE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 G9：联动建立后超差 → 自动解耦，errorCode=1
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldAutoDecoupleWhenPositionExceededAfterCoupled) {
    prepareForCoupling();

    // 建立联动
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 手动修改 X1 位置，制造超差（> 0.5mm 联动监测阈值）
    plc.setAbsolutePosition(AxisId::X1, 10.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);

    // 下一次 tick 应自动解耦
    plc.tick(10);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 1); // PositionToleranceExceeded
}

// ============================================================================
// 用例 G10：联动建立后 X1 报警 → 自动解耦，errorCode=999
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldAutoDecoupleWhenX1HasAlarm) {
    prepareForCoupling();

    // 建立联动
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 强制 X1 进入 Error 状态
    plc.forceState(AxisId::X1, AxisState::Error);
    plc.tick(10);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 999);
}

// ============================================================================
// 用例 G11：联动建立后 X1 限位触发 → 自动解耦（限位等同于报警）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldAutoDecoupleWhenX1HitsLimit) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 设置限位并触发
    plc.setLimits(AxisId::X1, 5.0, -100.0);
    plc.setSimulatedMoveVelocity(AxisId::X1, 100.0);
    plc.setAbsolutePosition(AxisId::X1, 0.0);

    // X1 运动撞限位（由于联动态下分动被拒绝，这里先手动设置位置超限位模拟）
    plc.setAbsolutePosition(AxisId::X1, 6.0); // 已超过 posLimit=5.0
    plc.tick(10);

    // limit 检查在 tick 中运行，但 setAbsolutePosition 绕过运动学，需要再 tick 触发限位检测
    // 注：checkHardwareLimits 在轴推演中运行，setAbsolutePosition 后直接超出限位值
    //     但限位检测只在运动过程中触发。这里通过一个小的 jog 触发限位检测。
    // 实际上我们需要验证：x1HasAlarm = (state==Error) || posLimit || negLimit
    // 因此需要触发限位标志。让我们用 move 命令触发限位：
    // 联动态下分动会被拒绝，所以需要一个更直接的方式：
    // 最简单的方式是 forceState 到 Error 状态（已在 G10 中测试）
    // 或者直接测试 posLimit 被 setLimits + setAbsolutePosition 触发的情况
    // 但实际上 checkHardwareLimits 只在 tick 中的 updateKinematics 之后运行
    // setAbsolutePosition 直接修改 absPos，下一次 tick 时 checkHardwareLimits 将检测到超限
    // 所以我们只需额外 tick 一次来触发限位检查

    // 重新安排：先解除联动，再触发限位
    plc.onGantryCommand(GantryCouplingCommand{false});
    plc.tick(150);
    ASSERT_FALSE(plc.getGantryFeedback().isCoupled);

    // 重新建立联动
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 现在手动设置 X1 位置超过限位值，然后让限位检测生效
    plc.setAbsolutePosition(AxisId::X1, 6.0);
    // 限位检测在 checkHardwareLimits 中，仅当运动时才会触发力停
    // 但 posLimit 标志会在 setAbsolutePosition + tick 后被检测
    plc.tick(10);

    // posLimit 应该在 tick 中由 checkHardwareLimits 设置为 true
    // 然后 refreshGantryPhysicalState 会检测到 posLimit → x1HasAlarm=true
    // → tickGantryCoupledMonitoring 自动解耦
    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 999);
}

// ============================================================================
// 用例 G12：联动建立后 X1 掉电 → 自动解耦，errorCode=3 (X2NotEnabled)
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldAutoDecoupleWhenX1Disabled) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // X1 掉电
    plc.onCommand(AxisId::X1, EnableCommand{false});
    plc.tick(10);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    // X1 未使能 → 统一用 X2 的码? 看代码：
    // errorCode = m_gantryPhysical.x1Enabled ? 3 : 2;
    // X1 未使能（x1Enabled=false）→ errorCode=2
    EXPECT_EQ(gf.errorCode, 2);
}

// ============================================================================
// 用例 G13：解耦请求 — 无条件下通过，isCoupled=false
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldDecoupleUnconditionally) {
    // 即使 X1/X2 未使能，解耦也应成功（无条件）
    ASSERT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Disabled);
    ASSERT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Disabled);

    // 先强制建立联动（绕过条件检查）
    plc.forceGantryFeedback(GantryFeedback{false, true, 0});
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 请求解耦（这应解除 locked 状态，让 PLC 重新接管）
    plc.onGantryCommand(GantryCouplingCommand{false});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 G14：联动态下拒绝 X1 独立点动（运动互斥）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectX1JogWhenCoupled) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    plc.setSimulatedJogVelocity(AxisId::X1, 10.0);
    double x1PosBefore = plc.getFeedback(AxisId::X1).absPos;

    // 尝试独立点动 X1
    plc.onCommand(AxisId::X1, JogCommand{Direction::Forward, true});
    plc.tick(100);

    // X1 不应运动（命令被拒绝）
    EXPECT_NEAR(plc.getFeedback(AxisId::X1).absPos, x1PosBefore, 0.001);
    // 状态也不应变
    EXPECT_NE(plc.getFeedback(AxisId::X1).state, AxisState::Jogging);
}

// ============================================================================
// 用例 G15：联动态下拒绝 X2 独立绝对定位（运动互斥）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldRejectX2MoveWhenCoupled) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    plc.setSimulatedMoveVelocity(AxisId::X2, 50.0);
    double x2PosBefore = plc.getFeedback(AxisId::X2).absPos;

    // 尝试独立定位 X2
    plc.onCommand(AxisId::X2, MoveCommand{MoveType::Absolute, 100.0, 0.0});
    plc.tick(2000);

    // X2 不应运动
    EXPECT_NEAR(plc.getFeedback(AxisId::X2).absPos, x2PosBefore, 0.001);
    EXPECT_NE(plc.getFeedback(AxisId::X2).state, AxisState::MovingAbsolute);
}

// ============================================================================
// 用例 G16：解耦态下允许 X1 独立点动（互斥解除）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldAllowX1JogWhenDecoupled) {
    enableX1AndX2();
    ASSERT_FALSE(plc.getGantryFeedback().isCoupled);

    plc.setSimulatedJogVelocity(AxisId::X1, 10.0);
    double x1PosBefore = plc.getFeedback(AxisId::X1).absPos;

    plc.onCommand(AxisId::X1, JogCommand{Direction::Forward, true});
    plc.tick(100);

    EXPECT_GT(plc.getFeedback(AxisId::X1).absPos, x1PosBefore);
    EXPECT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Jogging);
}

// ============================================================================
// 用例 G17：forceGantryFeedback 锁定机制——自动刷新被跳过
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldLockAutoRefreshWhenForceGantryFeedback) {
    prepareForCoupling();

    // 强制注入反馈
    plc.forceGantryFeedback(GantryFeedback{true, true, 99});
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);
    ASSERT_EQ(plc.getGantryFeedback().errorCode, 99);

    // tick 不应覆盖注入的值（locked=true）
    plc.tick(100);

    auto gf = plc.getGantryFeedback();
    EXPECT_TRUE(gf.isCoupled) << "should remain locked";
    EXPECT_EQ(gf.errorCode, 99) << "should remain locked";
}

// ============================================================================
// 用例 G18：新龙门命令自动解除锁定（PLC 重新接管）
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldUnlockWhenNewGantryCommandArrives) {
    prepareForCoupling();

    // 强制注入反馈（锁定）
    plc.forceGantryFeedback(GantryFeedback{false, false, 99});
    ASSERT_EQ(plc.getGantryFeedback().errorCode, 99);

    // 发送新的龙门耦合命令 → 自动解锁
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);

    auto gf = plc.getGantryFeedback();
    // PLC 重新接管，应为真实耦合成功（条件满足）
    EXPECT_TRUE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 G19：龙门反馈 enable 始终反映 X1/X2 真实使能状态
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldReflectX1X2EnableStateInGantryFeedback) {
    // 初始状态：都未使能
    plc.tick(10);
    EXPECT_FALSE(plc.getGantryFeedback().enable);

    // X1 使能，X2 未使能
    plc.onCommand(AxisId::X1, EnableCommand{true});
    plc.tick(200);
    EXPECT_FALSE(plc.getGantryFeedback().enable);

    // X2 也使能
    plc.onCommand(AxisId::X2, EnableCommand{true});
    plc.tick(200);
    EXPECT_TRUE(plc.getGantryFeedback().enable);

    // X1 掉电
    plc.onCommand(AxisId::X1, EnableCommand{false});
    plc.tick(10);
    EXPECT_FALSE(plc.getGantryFeedback().enable);
}

// ============================================================================
// 用例 G20：急停激活时龙门自动解耦 + 取消待处理命令
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldDecoupleAndCancelPendingOnEmergencyStop) {
    prepareForCoupling();

    // 建立联动
    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    // 激活急停
    plc.forceEmergencyStopCommand(true);
    plc.tick(100); // 急停延迟 50ms

    // 急停生效后龙门应自动解耦
    EXPECT_TRUE(plc.getEmergencyStopFeedback());
    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);

    // 急停激活时请求龙门使能应被拒绝
    plc.onGantryCommand(GantryPowerCommand{true});
    plc.tick(200);
    EXPECT_EQ(plc.getFeedback(AxisId::X1).state, AxisState::Disabled);
    EXPECT_EQ(plc.getFeedback(AxisId::X2).state, AxisState::Disabled);
}

// ============================================================================
// 用例 G21：resetAll 重置龙门所有状态
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldResetGantryStateOnResetAll) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(150);
    ASSERT_TRUE(plc.getGantryFeedback().isCoupled);

    plc.resetAll();

    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.enable);
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);
}

// ============================================================================
// 用例 G22：龙门耦合命令延迟——100ms 内未完成
// ============================================================================
TEST_F(FakePLCGantryTest, ShouldDelayCouplingBeforeGANTRY_COUPLING_DELAY_MS) {
    prepareForCoupling();

    plc.onGantryCommand(GantryCouplingCommand{true});
    plc.tick(50); // 仅 50ms < 100ms 延迟

    // 未到达延迟时间，耦合不应完成
    auto gf = plc.getGantryFeedback();
    EXPECT_FALSE(gf.isCoupled);
    EXPECT_EQ(gf.errorCode, 0);

    // 继续推进
    plc.tick(50); // 总 100ms = 延迟
    gf = plc.getGantryFeedback();
    EXPECT_TRUE(gf.isCoupled);
}
