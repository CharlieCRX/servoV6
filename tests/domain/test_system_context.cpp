#include <gtest/gtest.h>
#include "entity/SystemContext.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/logger/Logger.h"

// ============================================================================
// SystemContext（分组容器）单元测试
// 
// 测试覆盖范围：
// 1. 轴注册与访问
// 2. 多分组独立隔离（平行宇宙）
// 3. 龙门全局状态隔离
// 4. 多组独立运动互不干扰
// 5. Driver 绑定
// ============================================================================

// ---------------------------------------------------------------------------
// 第 1 组：轴注册与访问
// ---------------------------------------------------------------------------

TEST(SystemContextTest, ShouldRegisterAndAccessAllStandardAxes) {
    SystemContext ctx;

    // 注册所有标准轴
    ctx.registerAxis(AxisId::Y);
    ctx.registerAxis(AxisId::Z);
    ctx.registerAxis(AxisId::R);
    ctx.registerAxis(AxisId::X);
    ctx.registerAxis(AxisId::X1);
    ctx.registerAxis(AxisId::X2);

    // 验证：所有轴均可正常访问
    EXPECT_NO_THROW(ctx.getAxis(AxisId::Y));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::Z));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::R));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X1));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X2));

    // 验证：每个轴都是独立实例（地址不同）
    EXPECT_NE(&ctx.getAxis(AxisId::Y), &ctx.getAxis(AxisId::Z));
    EXPECT_NE(&ctx.getAxis(AxisId::X1), &ctx.getAxis(AxisId::X2));
}

TEST(SystemContextTest, RegisterAllStandardAxesShouldRegisterAllSix) {
    SystemContext ctx;

    ctx.registerAllStandardAxes();

    // 验证全部 6 个轴都可访问
    EXPECT_NO_THROW(ctx.getAxis(AxisId::Y));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::Z));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::R));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X1));
    EXPECT_NO_THROW(ctx.getAxis(AxisId::X2));
}

TEST(SystemContextTest, ShouldRejectUnregisteredAxis) {
    SystemContext ctx;

    // 只注册 Y 轴
    ctx.registerAxis(AxisId::Y);

    // 访问未注册的轴应抛出异常
    EXPECT_THROW(ctx.getAxis(AxisId::Z), std::out_of_range);
    EXPECT_THROW(ctx.getAxis(AxisId::X1), std::out_of_range);
}

// ---------------------------------------------------------------------------
// 第 2 组：多分组独立隔离（平行宇宙）
// ---------------------------------------------------------------------------

TEST(SystemContextTest, TwoContextsShouldBeIndependent) {
    SystemContext groupA;
    SystemContext groupB;

    // 各自注册不同轴
    groupA.registerAxis(AxisId::Y);
    groupA.registerAxis(AxisId::Z);

    groupB.registerAxis(AxisId::Y);
    groupB.registerAxis(AxisId::R);

    // 验证：groupA 能访问自己注册的轴
    EXPECT_NO_THROW(groupA.getAxis(AxisId::Y));
    EXPECT_NO_THROW(groupA.getAxis(AxisId::Z));

    // 验证：groupA 不能访问 groupB 独有的轴
    EXPECT_THROW(groupA.getAxis(AxisId::R), std::out_of_range);

    // 验证：groupB 能访问自己注册的轴
    EXPECT_NO_THROW(groupB.getAxis(AxisId::Y));
    EXPECT_NO_THROW(groupB.getAxis(AxisId::R));

    // 验证：groupB 不能访问 groupA 独有的轴
    EXPECT_THROW(groupB.getAxis(AxisId::Z), std::out_of_range);

    // 验证：同名轴（如 Y）在不同分组中是不同实例
    EXPECT_NE(&groupA.getAxis(AxisId::Y), &groupB.getAxis(AxisId::Y));
}

TEST(SystemContextTest, TwoContextsShouldHaveIndependentAxisState) {
    SystemContext groupA;
    SystemContext groupB;

    // 注册并设置初始状态
    groupA.registerAllStandardAxes();
    groupB.registerAllStandardAxes();

    // 给 groupA 的 Y 轴设置反馈
    groupA.getAxis(AxisId::Y).applyFeedback({
        .state = AxisState::Idle,
        .absPos = 100.0,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // 给 groupB 的 Y 轴设置不同的反馈
    groupB.getAxis(AxisId::Y).applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 200.0,
        .posLimitValue = 500.0,
        .negLimitValue = -500.0
    });

    // 验证：两个分组的同名轴状态完全独立
    EXPECT_DOUBLE_EQ(groupA.getAxis(AxisId::Y).currentAbsolutePosition(), 100.0);
    EXPECT_EQ(groupA.getAxis(AxisId::Y).state(), AxisState::Idle);

    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::Y).currentAbsolutePosition(), 200.0);
    EXPECT_EQ(groupB.getAxis(AxisId::Y).state(), AxisState::Disabled);
}

// ---------------------------------------------------------------------------
// 第 3 组：龙门全局状态隔离
// ---------------------------------------------------------------------------

TEST(SystemContextTest, GantryStateShouldDefaultToCoupled) {
    SystemContext ctx;

    // 默认应为联动模式
    EXPECT_TRUE(ctx.isGantryCoupled());
}

TEST(SystemContextTest, ShouldToggleGantryCoupledState) {
    SystemContext ctx;

    // 切换到解耦模式
    ctx.setGantryCoupled(false);
    EXPECT_FALSE(ctx.isGantryCoupled());

    // 切回联动模式
    ctx.setGantryCoupled(true);
    EXPECT_TRUE(ctx.isGantryCoupled());
}

TEST(SystemContextTest, GantryStateShouldBeIndependentBetweenContexts) {
    SystemContext groupA;
    SystemContext groupB;

    // 初始状态：两个分组默认都是联动模式
    EXPECT_TRUE(groupA.isGantryCoupled());
    EXPECT_TRUE(groupB.isGantryCoupled());

    // 将 groupA 切换为解耦模式
    groupA.setGantryCoupled(false);

    // 断言：groupA 已解耦
    EXPECT_FALSE(groupA.isGantryCoupled());

    // 断言：groupB 依然保持默认的联动模式（不受 groupA 影响）
    EXPECT_TRUE(groupB.isGantryCoupled());

    // 将 groupB 也切换为解耦
    groupB.setGantryCoupled(false);
    EXPECT_FALSE(groupB.isGantryCoupled());

    // 验证切换的独立性
    groupA.setGantryCoupled(true);
    EXPECT_TRUE(groupA.isGantryCoupled());
    EXPECT_FALSE(groupB.isGantryCoupled());
}

// ---------------------------------------------------------------------------
// 第 4 组：Driver 绑定
// ---------------------------------------------------------------------------

TEST(SystemContextTest, ShouldSetAndGetDriver) {
    SystemContext ctx;
    FakePLC plc;
    FakeAxisDriver driver(plc);

    // 绑定 Driver
    ctx.setDriver(&driver);

    // 验证
    EXPECT_EQ(ctx.driver(), &driver);

    // 未绑定 Driver 时为 nullptr
    SystemContext ctxWithoutDriver;
    EXPECT_EQ(ctxWithoutDriver.driver(), nullptr);
}

// ---------------------------------------------------------------------------
// 第 5 组：多分组独立运动互不干扰（集成级）
// 
// 核心验收标准（文档中的断言 3）：
// 无论操作 groupA 的 X1 还是 Y，它们都能独立运动，且绝不影响 groupB。
// ---------------------------------------------------------------------------

class SystemContextIntegrationTest : public ::testing::Test {
protected:
    SystemContext groupA;
    SystemContext groupB;
    FakePLC plcA;
    FakePLC plcB;
    FakeAxisDriver driverA{plcA};
    FakeAxisDriver driverB{plcB};

    void SetUp() override {
        // 初始化：两个分组各自注册全部轴
        groupA.registerAllStandardAxes();
        groupB.registerAllStandardAxes();

        // 各自绑定独立的 PLC
        groupA.setDriver(&driverA);
        groupB.setDriver(&driverB);

        // 设置合理的限位范围
        for (auto id : {AxisId::Y, AxisId::Z, AxisId::R, AxisId::X, AxisId::X1, AxisId::X2}) {
            plcA.setLimits(id, 1000.0, -1000.0);
            plcB.setLimits(id, 1000.0, -1000.0);
        }
    }

    // 辅助：将 PLC A 的反馈同步回 groupA 的 Axis
    void syncA(AxisId id) {
        groupA.getAxis(id).applyFeedback(plcA.getFeedback(id));
    }

    // 辅助：将 PLC B 的反馈同步回 groupB 的 Axis
    void syncB(AxisId id) {
        groupB.getAxis(id).applyFeedback(plcB.getFeedback(id));
    }

    // 辅助：统一步进 A 分组的物理引擎
    void tickA(int ms = 10) {
        plcA.tick(ms);
        for (auto id : {AxisId::Y, AxisId::Z, AxisId::R, AxisId::X, AxisId::X1, AxisId::X2}) {
            syncA(id);
        }
    }

    // 辅助：统一步进 B 分组的物理引擎
    void tickB(int ms = 10) {
        plcB.tick(ms);
        for (auto id : {AxisId::Y, AxisId::Z, AxisId::R, AxisId::X, AxisId::X1, AxisId::X2}) {
            syncB(id);
        }
    }
};

TEST_F(SystemContextIntegrationTest, ShouldMoveGroupAIndependentlyFromGroupB) {
    // 阶段 1：初始状态确认
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcB.forceState(AxisId::Y, AxisState::Disabled);
    plcA.forceState(AxisId::X1, AxisState::Disabled);
    plcB.forceState(AxisId::X1, AxisState::Disabled);

    syncA(AxisId::Y);
    syncB(AxisId::Y);
    syncA(AxisId::X1);
    syncB(AxisId::X1);

    EXPECT_EQ(groupA.getAxis(AxisId::Y).state(), AxisState::Disabled);
    EXPECT_EQ(groupB.getAxis(AxisId::Y).state(), AxisState::Disabled);

    // 阶段 2：启动 groupA 的 Y 轴
    auto& axisA_Y = groupA.getAxis(AxisId::Y);
    axisA_Y.enable(true);
    driverA.send(AxisId::Y, axisA_Y.getPendingCommand());

    // 等待使能完成
    int maxTicks = 500;
    while (maxTicks-- > 0) {
        tickA(10);
        if (groupA.getAxis(AxisId::Y).state() == AxisState::Idle) break;
    }
    ASSERT_EQ(groupA.getAxis(AxisId::Y).state(), AxisState::Idle);

    // 阶段 3：groupA 的 Y 轴发起运动
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    plcA.forceState(AxisId::Y, AxisState::Idle);
    syncA(AxisId::Y);

    double targetPos = 80.0;
    axisA_Y.moveAbsolute(targetPos);
    driverA.send(AxisId::Y, axisA_Y.getPendingCommand());

    // 等待 groupA 的 Y 轴运动到目标
    maxTicks = 500;
    while (maxTicks-- > 0) {
        tickA(10);
        if (!groupA.getAxis(AxisId::Y).hasPendingCommand()) break;
    }

    // 验证：groupA 的 Y 轴到达目标
    EXPECT_NEAR(groupA.getAxis(AxisId::Y).currentAbsolutePosition(), targetPos, 0.01);

    // 验证：groupB 的 Y 轴完全没有动
    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::Y).currentAbsolutePosition(), 0.0);
    EXPECT_EQ(groupB.getAxis(AxisId::Y).state(), AxisState::Disabled);

    // 验证：groupA 的 X1 轴完全没有动
    EXPECT_DOUBLE_EQ(groupA.getAxis(AxisId::X1).currentAbsolutePosition(), 0.0);

    // 验证：groupB 的 X1 轴完全没有动
    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::X1).currentAbsolutePosition(), 0.0);
}

TEST_F(SystemContextIntegrationTest, ShouldMoveX1InGroupAWithoutAffectingGroupB) {
    // 阶段 1：初始化两组的所有轴
    plcA.forceState(AxisId::X1, AxisState::Disabled);
    plcB.forceState(AxisId::X1, AxisState::Disabled);
    plcB.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::X1, 30.0);
    syncA(AxisId::X1);
    syncB(AxisId::X1);
    syncB(AxisId::Y);

    auto& axisA_X1 = groupA.getAxis(AxisId::X1);
    axisA_X1.enable(true);
    driverA.send(AxisId::X1, axisA_X1.getPendingCommand());

    // 等待使能完成
    int maxTicks = 500;
    while (maxTicks-- > 0) {
        tickA(10);
        if (groupA.getAxis(AxisId::X1).state() == AxisState::Idle) break;
    }
    ASSERT_EQ(groupA.getAxis(AxisId::X1).state(), AxisState::Idle);

    // 阶段 2：groupA 的 X1 轴发起运动
    plcA.forceState(AxisId::X1, AxisState::Idle);
    syncA(AxisId::X1);

    double targetPos = 60.0;
    axisA_X1.moveAbsolute(targetPos);
    driverA.send(AxisId::X1, axisA_X1.getPendingCommand());

    // 等待运动完成
    maxTicks = 500;
    while (maxTicks-- > 0) {
        tickA(10);
        if (!groupA.getAxis(AxisId::X1).hasPendingCommand()) break;
    }

    // 验证：groupA 的 X1 轴到达目标
    EXPECT_NEAR(groupA.getAxis(AxisId::X1).currentAbsolutePosition(), targetPos, 0.01);

    // 验证：groupB 的 X1 轴完全没有动
    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::X1).currentAbsolutePosition(), 0.0);
    EXPECT_EQ(groupB.getAxis(AxisId::X1).state(), AxisState::Disabled);

    // 验证：groupA 的 Y 轴完全没有动
    EXPECT_DOUBLE_EQ(groupA.getAxis(AxisId::Y).currentAbsolutePosition(), 0.0);

    // 验证：groupB 的 Y 轴完全没有动
    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::Y).currentAbsolutePosition(), 0.0);
}

TEST_F(SystemContextIntegrationTest, BothGroupsShouldMoveSimultaneouslyWithoutInterference) {
    // 本测试模拟双组并行运动，验证隔离性

    // 阶段 1：初始化两组各一个轴
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 40.0);
    plcB.forceState(AxisId::Z, AxisState::Disabled);
    plcB.setSimulatedMoveVelocity(AxisId::Z, 40.0);
    syncA(AxisId::Y);
    syncB(AxisId::Z);

    // 使能 groupA Y 轴
    auto& axisA_Y = groupA.getAxis(AxisId::Y);
    axisA_Y.enable(true);
    driverA.send(AxisId::Y, axisA_Y.getPendingCommand());

    // 使能 groupB Z 轴
    auto& axisB_Z = groupB.getAxis(AxisId::Z);
    axisB_Z.enable(true);
    driverB.send(AxisId::Z, axisB_Z.getPendingCommand());

    // 等待两组使能完成
    int maxTicks = 500;
    while (maxTicks-- > 0) {
        tickA(10);
        tickB(10);
        if (groupA.getAxis(AxisId::Y).state() == AxisState::Idle &&
            groupB.getAxis(AxisId::Z).state() == AxisState::Idle) break;
    }

    // 阶段 2：两组同时发起运动
    plcA.forceState(AxisId::Y, AxisState::Idle);
    plcB.forceState(AxisId::Z, AxisState::Idle);
    syncA(AxisId::Y);
    syncB(AxisId::Z);

    // groupA: Y 轴目标 100.0
    axisA_Y.moveAbsolute(100.0);
    driverA.send(AxisId::Y, axisA_Y.getPendingCommand());

    // groupB: Z 轴目标 150.0
    axisB_Z.moveAbsolute(150.0);
    driverB.send(AxisId::Z, axisB_Z.getPendingCommand());

    // 并行 tick，等待两组都完成
    int ticks = 0;
    const int MAX_TICKS = 1000;
    while (ticks < MAX_TICKS) {
        tickA(10);
        tickB(10);

        bool aDone = !groupA.getAxis(AxisId::Y).hasPendingCommand();
        bool bDone = !groupB.getAxis(AxisId::Z).hasPendingCommand();
        if (aDone && bDone) break;
        ticks++;
    }

    // 验证：两组均到达各自目标
    EXPECT_NEAR(groupA.getAxis(AxisId::Y).currentAbsolutePosition(), 100.0, 0.01);
    EXPECT_NEAR(groupB.getAxis(AxisId::Z).currentAbsolutePosition(), 150.0, 0.01);

    // 验证：互不干扰 —— groupA 的 Z 轴未动
    EXPECT_DOUBLE_EQ(groupA.getAxis(AxisId::Z).currentAbsolutePosition(), 0.0);

    // 验证：互不干扰 —— groupB 的 Y 轴未动
    EXPECT_DOUBLE_EQ(groupB.getAxis(AxisId::Y).currentAbsolutePosition(), 0.0);
}
