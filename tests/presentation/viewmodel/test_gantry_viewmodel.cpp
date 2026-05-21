#include <gtest/gtest.h>
#include "presentation/viewmodel/GantryViewModel.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"

// ============================================================================
// GantryViewModel 单元测试（TDD — 纯 C++ 风格，无 QSignalSpy 依赖）
//
// 测试目标：
//   1. 验证 GantryViewModel 正确投影 GantryPowerController / GantryCouplingController 状态
//   2. 验证 isDecoupledAndEnabled 派生属性逻辑
//   3. 验证密码验证（verifyPassword）
//   4. 验证 Q_INVOKABLE 操作接口正确调用 GantryOrchestrator
//   5. 验证 tick() 驱动状态刷新
//   6. 验证 orchestrator 状态投影（isOrchestratorBusy / orchestratorStepText）
//
// 注意：G11/G12（信号发射时机验证）依赖 QSignalSpy，已通过纯状态缓存对比替代。
// ============================================================================

class GantryViewModelTest : public ::testing::Test {
protected:
    static constexpr const char* GROUP_NAME = "GantryMachine";

    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;
    SystemContext* ctx = nullptr;

    const int TICK_MS = 10;

    void SetUp() override {
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP_NAME, reason));
        ASSERT_TRUE(manager.tryGetGroup(GROUP_NAME, ctx, reason));
        ASSERT_NE(ctx, nullptr);
        ctx->setDriver(&driver);
    }

    void TearDown() override {
        plc.resetAll();
    }

    /// @brief 推进仿真时间，每帧执行 driver.pollFeedback + vm.tick()
    void advanceTime(GantryViewModel& vm, int totalMs) {
        int elapsed = 0;
        while (elapsed < totalMs) {
            driver.pollFeedback(*ctx);
            vm.tick();
            elapsed += TICK_MS;
        }
    }

    /// @brief 等待条件成立
    bool waitUntil(GantryViewModel& vm, std::function<bool()> condition,
                   int timeoutMs = 5000) {
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            if (condition()) return true;
            advanceTime(vm, TICK_MS);
            elapsed += TICK_MS;
        }
        return false;
    }
};

// =========================================================
// 测试 G1：初始状态投影 — NotSynchronized
// =========================================================
TEST_F(GantryViewModelTest, ShouldReflectInitialNotSynchronizedState) {
    GantryViewModel vm(manager, GROUP_NAME);
    vm.tick();

    EXPECT_FALSE(vm.isEnabled());
    EXPECT_FALSE(vm.isCoupled());
    EXPECT_FALSE(vm.isDecoupledAndEnabled());
    EXPECT_FALSE(vm.isSynchronized());
    EXPECT_FALSE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G2：同步后 Disabled 状态投影
// =========================================================
TEST_F(GantryViewModelTest, ShouldReflectDisabledStateAfterSync) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    EXPECT_FALSE(vm.isEnabled());
    EXPECT_FALSE(vm.isCoupled());
    EXPECT_FALSE(vm.isDecoupledAndEnabled());
    EXPECT_TRUE(vm.isSynchronized());
    EXPECT_FALSE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G3：使能后的状态投影（Enabled + NotCoupled = 状态 C）
// =========================================================
TEST_F(GantryViewModelTest, ShouldReflectDecoupledAndEnabledState) {
    GantryViewModel vm(manager, GROUP_NAME);

    plc.forceState(AxisId::X1, AxisState::Disabled);
    plc.forceState(AxisId::X2, AxisState::Disabled);
    driver.pollFeedback(*ctx);
    vm.tick();

    auto& power = ctx->gantryPowerController();
    power.requestEnable(true);
    ASSERT_TRUE(power.hasPendingCommand());
    driver.send(power.popPendingCommand());
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isEnabled(); }, 2000));

    EXPECT_TRUE(vm.isEnabled());
    EXPECT_FALSE(vm.isCoupled());
    EXPECT_TRUE(vm.isDecoupledAndEnabled());
    EXPECT_TRUE(vm.isSynchronized());
}

// =========================================================
// 测试 G4：联动后的状态投影（Enabled + Coupled = 状态 B）
// =========================================================
TEST_F(GantryViewModelTest, ShouldReflectCoupledState) {
    GantryViewModel vm(manager, GROUP_NAME);

    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);
    driver.pollFeedback(*ctx);
    vm.tick();

    auto& power = ctx->gantryPowerController();
    power.requestEnable(true);
    if (power.hasPendingCommand()) {
        driver.send(power.popPendingCommand());
    }
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isEnabled(); }, 2000));

    auto& coupling = ctx->gantryCouplingController();
    coupling.requestCouple(true);
    if (coupling.hasPendingCommand()) {
        driver.send(coupling.popPendingCommand());
    }
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isCoupled(); }, 2000));

    EXPECT_TRUE(vm.isEnabled());
    EXPECT_TRUE(vm.isCoupled());
    EXPECT_FALSE(vm.isDecoupledAndEnabled());
    EXPECT_TRUE(vm.isSynchronized());
}

// =========================================================
// 测试 G5：密码验证 — 正确密码
// =========================================================
TEST_F(GantryViewModelTest, VerifyPasswordShouldAcceptCorrectPassword) {
    GantryViewModel vm(manager, GROUP_NAME);

    EXPECT_TRUE(vm.verifyPassword(QStringLiteral("123456")));
}

// =========================================================
// 测试 G6：密码验证 — 错误密码
// =========================================================
TEST_F(GantryViewModelTest, VerifyPasswordShouldRejectWrongPassword) {
    GantryViewModel vm(manager, GROUP_NAME);

    EXPECT_FALSE(vm.verifyPassword(QStringLiteral("wrong")));
    EXPECT_FALSE(vm.verifyPassword(QStringLiteral("")));
    EXPECT_FALSE(vm.verifyPassword(QStringLiteral("12345")));
    EXPECT_FALSE(vm.verifyPassword(QStringLiteral("1234567")));
}

// =========================================================
// 测试 G7：startCoupling() 调用后 orchestrator 进入忙碌状态
// =========================================================
TEST_F(GantryViewModelTest, StartCouplingShouldMakeOrchestratorBusy) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    EXPECT_FALSE(vm.isOrchestratorBusy());

    vm.startCoupling();
    vm.tick();

    EXPECT_TRUE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G8：stopCouplingAndDisable() 调用后 orchestrator 忙碌
// =========================================================
TEST_F(GantryViewModelTest, StopCouplingAndDisableShouldMakeOrchestratorBusy) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    EXPECT_FALSE(vm.isOrchestratorBusy());

    vm.stopCouplingAndDisable();
    vm.tick();

    EXPECT_TRUE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G9：enableAndDecouple() 调用后 orchestrator 忙碌
// =========================================================
TEST_F(GantryViewModelTest, EnableAndDecoupleShouldMakeOrchestratorBusy) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    EXPECT_FALSE(vm.isOrchestratorBusy());

    vm.enableAndDecouple();
    vm.tick();

    EXPECT_TRUE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G10：disable() 和 enable() 直接控制 PowerController
// =========================================================
TEST_F(GantryViewModelTest, DisableAndEnableShouldDirectlyControlPower) {
    GantryViewModel vm(manager, GROUP_NAME);

    plc.forceState(AxisId::X1, AxisState::Disabled);
    plc.forceState(AxisId::X2, AxisState::Disabled);
    driver.pollFeedback(*ctx);
    vm.tick();

    auto& power = ctx->gantryPowerController();
    power.requestEnable(true);
    if (power.hasPendingCommand()) {
        driver.send(power.popPendingCommand());
    }
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isEnabled(); }, 2000));
    ASSERT_TRUE(vm.isEnabled());

    // disable()
    vm.disable();
    vm.tick();

    EXPECT_FALSE(power.isEnabled());

    if (power.hasPendingCommand()) {
        driver.send(power.popPendingCommand());
    }
    ASSERT_TRUE(waitUntil(vm, [&]() { return !vm.isEnabled(); }, 2000));
    EXPECT_FALSE(vm.isEnabled());

    // enable()
    vm.enable();
    vm.tick();

    if (power.hasPendingCommand()) {
        driver.send(power.popPendingCommand());
    }
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isEnabled(); }, 2000));
    EXPECT_TRUE(vm.isEnabled());
}

// =========================================================
// 测试 G11：连续 tick() 状态不变时缓存值保持稳定（间接验证信号抑制）
//
// 替代 QSignalSpy 方案：通过断言多次 tick 后状态归属不变来验证
// refreshGantryState 的缓存比较逻辑。
// =========================================================
TEST_F(GantryViewModelTest, StateCacheShouldRemainStableWhenUnchanged) {
    GantryViewModel vm(manager, GROUP_NAME);

    // 初始状态
    vm.tick();
    bool initialSync = vm.isSynchronized();
    bool initialEnabled = vm.isEnabled();
    bool initialCoupled = vm.isCoupled();
    bool initialDae = vm.isDecoupledAndEnabled();

    // 多次 tick 无 feedback 变化，缓存值不应改变
    for (int i = 0; i < 3; ++i) {
        vm.tick();
        EXPECT_EQ(vm.isSynchronized(), initialSync);
        EXPECT_EQ(vm.isEnabled(), initialEnabled);
        EXPECT_EQ(vm.isCoupled(), initialCoupled);
        EXPECT_EQ(vm.isDecoupledAndEnabled(), initialDae);
    }
}

// =========================================================
// 测试 G12：orchestrator 步骤文本在空闲/忙碌间正确切换
//
// 替代 QSignalSpy：直接验证 stepText 在 orchestrator 创建前后的变化。
// =========================================================
TEST_F(GantryViewModelTest, OrchestratorStepTextShouldTrackStateTransitions) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    // 无 orchestrator 时 stepText 应为"就绪"（stepToText(-1)）
    QString idleText = vm.orchestratorStepText();
    EXPECT_FALSE(idleText.isEmpty());

    // 启动联动后 stepText 应变化为步骤文本（如"正在使能龙门电机..."）
    vm.startCoupling();
    vm.tick();

    QString busyText = vm.orchestratorStepText();
    EXPECT_FALSE(busyText.isEmpty());
    EXPECT_NE(busyText.toStdString(), idleText.toStdString());

    // 推动完成
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);
    ASSERT_TRUE(waitUntil(vm, [&]() { return !vm.isOrchestratorBusy(); }, 5000));

    QString doneText = vm.orchestratorStepText();
    EXPECT_FALSE(doneText.isEmpty());
    EXPECT_NE(doneText.toStdString(), busyText.toStdString());
}

// =========================================================
// 测试 G13：tick() 推进 orchestrator 至完成状态
// =========================================================
TEST_F(GantryViewModelTest, TickShouldAdvanceOrchestratorToDone) {
    GantryViewModel vm(manager, GROUP_NAME);

    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);
    driver.pollFeedback(*ctx);
    vm.tick();

    vm.startCoupling();

    ASSERT_TRUE(waitUntil(vm, [&]() { return !vm.isOrchestratorBusy(); }, 5000));

    EXPECT_TRUE(vm.isEnabled());
    EXPECT_TRUE(vm.isCoupled());
    EXPECT_FALSE(vm.isOrchestratorBusy());
}

// =========================================================
// 测试 G14：orchestratorStepText 返回正确的步骤描述
// =========================================================
TEST_F(GantryViewModelTest, OrchestratorStepTextShouldReturnCorrectDescription) {
    GantryViewModel vm(manager, GROUP_NAME);

    driver.pollFeedback(*ctx);
    vm.tick();

    EXPECT_FALSE(vm.orchestratorStepText().isEmpty());

    vm.startCoupling();
    vm.tick();

    EXPECT_FALSE(vm.orchestratorStepText().isEmpty());
}

// =========================================================
// 测试 G15：isDecoupledAndEnabled 边界条件
// =========================================================
TEST_F(GantryViewModelTest, IsDecoupledAndEnabledBoundaryConditions) {
    GantryViewModel vm(manager, GROUP_NAME);

    vm.tick();
    EXPECT_FALSE(vm.isDecoupledAndEnabled());

    driver.pollFeedback(*ctx);
    vm.tick();
    EXPECT_FALSE(vm.isDecoupledAndEnabled());

    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setAbsolutePosition(AxisId::X1, 0.0);
    plc.setAbsolutePosition(AxisId::X2, 0.0);
    driver.pollFeedback(*ctx);
    auto& power = ctx->gantryPowerController();
    power.requestEnable(true);
    if (power.hasPendingCommand()) driver.send(power.popPendingCommand());
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isEnabled(); }, 2000));

    auto& coupling = ctx->gantryCouplingController();
    coupling.requestCouple(true);
    if (coupling.hasPendingCommand()) driver.send(coupling.popPendingCommand());
    ASSERT_TRUE(waitUntil(vm, [&]() { return vm.isCoupled(); }, 2000));

    EXPECT_FALSE(vm.isDecoupledAndEnabled());

    coupling.requestCouple(false);
    if (coupling.hasPendingCommand()) driver.send(coupling.popPendingCommand());
    ASSERT_TRUE(waitUntil(vm, [&]() { return !vm.isCoupled(); }, 2000));

    EXPECT_TRUE(vm.isDecoupledAndEnabled());
}

// =========================================================
// 测试 G16：isSynchronized 综合判断（power + coupling 都同步）
// =========================================================
TEST_F(GantryViewModelTest, IsSynchronizedShouldCheckBothControllers) {
    GantryViewModel vm(manager, GROUP_NAME);

    vm.tick();
    EXPECT_FALSE(vm.isSynchronized());

    driver.pollFeedback(*ctx);
    vm.tick();
    EXPECT_TRUE(vm.isSynchronized());
}
