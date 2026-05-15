#include <gtest/gtest.h>
#include <variant>
#include "application/safety/EmergencyStopUseCase.h"
#include "application/safety/ReleaseEmergencyStopUseCase.h"
#include "application/SystemManager.h"
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "domain/safety/SafetyState.h"
#include "domain/safety/SafetyRejection.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================
// EmergencyStopUseCase & ReleaseEmergencyStopUseCase TDD 测试套件
//
// 验证完整调用链：
//   UI → EmergencyStopUseCase.execute(manager, groupName)
//      → SystemManager::tryGetGroup
//      → EmergencyStopController::requestEmergencyStop()
//      → EmergencyStopController::hasPendingCommand()
//      → EmergencyStopController::popPendingCommand()
//      → SystemContext::driver()->send(EmergencyStopCommand)
//      → 返回 UseCaseError
//
//   UI → ReleaseEmergencyStopUseCase.execute(manager, groupName)
//      → SystemManager::tryGetGroup
//      → EmergencyStopController::requestReleaseEmergencyStop()
//      → EmergencyStopController::hasPendingCommand()
//      → EmergencyStopController::popPendingCommand()
//      → SystemContext::driver()->send(EmergencyStopCommand)
//      → 返回 UseCaseError
// ============================================================

// ── 辅助：将 UseCaseError 断言为某一具体类型 ──────────────────

template<typename T>
T expectError(const UseCaseError& err) {
    EXPECT_TRUE(std::holds_alternative<T>(err))
        << "Expected error type " << typeid(T).name()
        << " but got variant index " << err.index();
    return std::get<T>(err);
}

inline void expectSuccess(const UseCaseError& err) {
    EXPECT_TRUE(std::holds_alternative<std::monostate>(err))
        << "Expected success (monostate) but got variant index " << err.index();
}

// ============================================================
// 测试夹具
// ============================================================

class EmergencyStopUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    EmergencyStopUseCase emergencyStopUseCase;
    ReleaseEmergencyStopUseCase releaseEmergencyStopUseCase;
    FakePLC plc;
    FakeAxisDriver driver{plc};

    static constexpr const char* GROUP = "Machine_A";

    void SetUp() override {
        // 1. 创建分组
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP, reason));

        // 2. 注入假驱动
        SystemContext* ctx = nullptr;
        ASSERT_TRUE(manager.tryGetGroup(GROUP, ctx, reason));
        ctx->setDriver(&driver);
    }

    /// @brief 完成 PLC 反馈同步 — 初始 NotSynchronized → Running
    void syncToRunning() {
        SystemContext* ctx = getContext();
        // 首次 applyFeedback 完成同步
        ctx->emergencyStopController().applyFeedback(false);
        ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);
    }

    /// @brief 完成 PLC 反馈同步 — 初始 NotSynchronized → EmergencyStopped
    void syncToEmergencyStopped() {
        SystemContext* ctx = getContext();
        ctx->emergencyStopController().applyFeedback(true);
        ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);
    }

    /// @brief 走完完整急停链路: Running → 触发 → EmergencyStopping → PLC确认 → EmergencyStopped
    void fullEmergencyStop() {
        SystemContext* ctx = getContext();
        syncToRunning();

        // 用户触发急停
        UseCaseError err = emergencyStopUseCase.execute(manager, GROUP);
        expectSuccess(err);

        // PLC 确认（模拟 PLC 收到命令后动作完成）
        plc.tick(100);  // 让 PLC 处理命令
        ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    }

    SystemContext* getContext() {
        SystemContext* ctx = nullptr;
        ContextRejection reason;
        manager.tryGetGroup(GROUP, ctx, reason);
        return ctx;
    }
};

// ============================================================
// 第一部分：分组路由 — SystemManager 层错误
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = emergencyStopUseCase.execute(manager, "GhostGroup");

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

TEST_F(EmergencyStopUseCaseTest, ReleaseEmergencyStop_NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = releaseEmergencyStopUseCase.execute(manager, "GhostGroup");

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

// ============================================================
// 第二部分：NotSynchronized — 尚未同步 PLC 状态，拒绝所有操作
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_WhenNotSynchronized_ReturnsNotSynchronized) {
    // Given: 默认构造后 EmergencyStopController 处于 NotSynchronized
    SystemContext* ctx = getContext();
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::NotSynchronized);

    // When: 尝试触发急停
    UseCaseError result = emergencyStopUseCase.execute(manager, GROUP);

    // Then: 被拒绝 — 真相未知时不允许操作
    SafetyRejection err = expectError<SafetyRejection>(result);
    EXPECT_EQ(err, SafetyRejection::NotSynchronized);
}

TEST_F(EmergencyStopUseCaseTest, ReleaseEmergencyStop_WhenNotSynchronized_ReturnsNotSynchronized) {
    // Given: 处于 NotSynchronized
    SystemContext* ctx = getContext();
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::NotSynchronized);

    // When: 尝试解除急停
    UseCaseError result = releaseEmergencyStopUseCase.execute(manager, GROUP);

    // Then: 被拒绝
    SafetyRejection err = expectError<SafetyRejection>(result);
    EXPECT_EQ(err, SafetyRejection::NotSynchronized);
}

// ============================================================
// 第三部分：完整急停触发链路 — Running → EmergencyStopping → EmergencyStopped
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_FromRunning_ProducesCommandAndTransitionsToEmergencyStopping) {
    // Given: 系统已同步，Running 状态
    syncToRunning();
    SystemContext* ctx = getContext();
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);

    // When: 触发急停
    UseCaseError result = emergencyStopUseCase.execute(manager, GROUP);

    // Then: 成功返回
    expectSuccess(result);

    // 状态应跃迁到 EmergencyStopping
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopping);

    // pending command 应该已被消费（UseCase 发送后 pop）
    EXPECT_FALSE(ctx->emergencyStopController().hasPendingCommand());
}

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_AfterUseCase_PLCReceivesCommand) {
    // Given: Running 状态
    syncToRunning();

    // When: 触发急停
    UseCaseError result = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(result);

    // Then: PLC 应收到急停命令
    // 驱动 send(EmergencyStopCommand{true}) 已调用
    // 验证 PLC 内部命令寄存器被置位（通过 tick 推进后状态寄存器会变化）
    plc.tick(100);  // 推进 PLC 延迟
    EXPECT_TRUE(plc.getEmergencyStopFeedback());
}

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_FullLink_RunningToEmergencyStopped) {
    // Given: 系统 Running
    syncToRunning();
    SystemContext* ctx = getContext();

    // Step 1: 用户触发急停
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopping);

    // Step 2: PLC 处理完成后反馈"设备急停中=true"
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());

    // Then: EmergencyStopping → EmergencyStopped
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(ctx->emergencyStopController().isEmergencyStopped());
    EXPECT_TRUE(ctx->emergencyStopController().isSystemLocked());
}

// ============================================================
// 第四部分：幂等 — 重复触发/解除急停
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_DoubleTrigger_ReturnsAlreadyInState) {
    // Given: 已进入 EmergencyStopping（第一次触发）
    syncToRunning();
    SystemContext* ctx = getContext();
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopping);

    // When: 在 EmergencyStopping 态再次触发急停
    UseCaseError err2 = emergencyStopUseCase.execute(manager, GROUP);

    // Then: 幂等拒绝
    SafetyRejection err = expectError<SafetyRejection>(err2);
    EXPECT_EQ(err, SafetyRejection::AlreadyInState);
}

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_WhenAlreadyEmergencyStopped_ReturnsAlreadyInState) {
    // Given: 已处于 EmergencyStopped
    syncToRunning();
    SystemContext* ctx = getContext();

    // 完整急停链路
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    // When: 再次触发急停
    UseCaseError err2 = emergencyStopUseCase.execute(manager, GROUP);

    // Then: 幂等拒绝
    SafetyRejection err = expectError<SafetyRejection>(err2);
    EXPECT_EQ(err, SafetyRejection::AlreadyInState);
}

TEST_F(EmergencyStopUseCaseTest, ReleaseEmergencyStop_WhenNotEmergencyStopped_ReturnsNotEmergencyStopped) {
    // Given: 系统在 Running 状态
    syncToRunning();
    SystemContext* ctx = getContext();
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);

    // When: 尝试解除急停
    UseCaseError result = releaseEmergencyStopUseCase.execute(manager, GROUP);

    // Then: 前置条件拒绝
    SafetyRejection err = expectError<SafetyRejection>(result);
    EXPECT_EQ(err, SafetyRejection::NotEmergencyStopped);
}

// ============================================================
// 第五部分：完整解除急停链路
// ============================================================

TEST_F(EmergencyStopUseCaseTest, ReleaseEmergencyStop_FullLink_EmergencyStoppedToRunning) {
    // Given: 系统处于 EmergencyStopped
    SystemContext* ctx = getContext();
    syncToRunning();
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    // Step 1: 用户解除急停
    UseCaseError err2 = releaseEmergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err2);
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::ReleasingEmergencyStop);

    // Step 2: PLC 处理完成后反馈"设备急停中=false"
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());

    // Then: ReleasingEmergencyStop → Running
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);
    EXPECT_FALSE(ctx->emergencyStopController().isEmergencyStopped());
    EXPECT_FALSE(ctx->emergencyStopController().isSystemLocked());
}

TEST_F(EmergencyStopUseCaseTest, ReleaseEmergencyStop_AfterUseCase_PLCReceivesReleaseCommand) {
    // Given: EmergencyStopped
    SystemContext* ctx = getContext();
    syncToRunning();
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    // When: 解除急停
    UseCaseError err2 = releaseEmergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err2);

    // Then: PLC 最终反馈解除
    plc.tick(100);
    EXPECT_FALSE(plc.getEmergencyStopFeedback());
}

// ============================================================
// 第六部分：冲突检测 — ReleasingEmergencyStop 时不能再触发急停
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStop_WhileReleasing_ReturnsInvalidStateTransition) {
    // Given: EmergencyStopped → 正在解除中
    SystemContext* ctx = getContext();
    syncToRunning();
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    UseCaseError err2 = releaseEmergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err2);
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::ReleasingEmergencyStop);

    // When: 在 ReleasingEmergencyStop 时尝试触发急停
    UseCaseError err3 = emergencyStopUseCase.execute(manager, GROUP);

    // Then: 非法状态跃迁
    SafetyRejection err = expectError<SafetyRejection>(err3);
    EXPECT_EQ(err, SafetyRejection::InvalidStateTransition);
}

// ============================================================
// 第七部分：UseCase 无状态 — 多次调用互不影响
// ============================================================

TEST_F(EmergencyStopUseCaseTest, Stateless_RepeatedCallsProduceConsistentResult) {
    syncToRunning();
    SystemContext* ctx = getContext();

    // 第一次触发急停
    UseCaseError r1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(r1);

    // 模拟反馈驱动完成
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());

    // 第二次触发（幂等拒绝）
    UseCaseError r2 = emergencyStopUseCase.execute(manager, GROUP);
    SafetyRejection err = expectError<SafetyRejection>(r2);
    EXPECT_EQ(err, SafetyRejection::AlreadyInState);
}

// ============================================================
// 第八部分：物理急停按钮路径 — Running + feedback(true) → EmergencyStopped
// ============================================================

TEST_F(EmergencyStopUseCaseTest, PhysicalEmergencyStop_RunningToEmergencyStopped_WithoutControllerCommand) {
    // Given: Running 状态
    syncToRunning();
    SystemContext* ctx = getContext();
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);

    // When: 物理急停按钮被按下 — PLC 直接反馈 true（绕过 Controller 命令）
    ctx->emergencyStopController().applyFeedback(true);

    // Then: 直接跃迁到 EmergencyStopped，不经过 EmergencyStopping
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(ctx->emergencyStopController().isEmergencyStopped());

    // Command 仍在 PLC 命令寄存器中？不，物理按钮不由 Controller 产生命令
    EXPECT_FALSE(ctx->emergencyStopController().hasPendingCommand());
}

TEST_F(EmergencyStopUseCaseTest, ReleaseAfterPhysicalEmergencyStop_Succeeds) {
    // Given: 物理急停按钮导致的 EmergencyStopped
    syncToRunning();
    SystemContext* ctx = getContext();
    ctx->emergencyStopController().applyFeedback(true);
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    // When: 软件解除
    UseCaseError err = releaseEmergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err);
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::ReleasingEmergencyStop);

    // PLC 处理完成
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());

    // Then: 恢复 Running
    EXPECT_EQ(ctx->emergencyStopController().state(), SafetyState::Running);
}

// ============================================================
// 第九部分：UseCase → PLC 驱动 — 命令正确路由到 Driver
// ============================================================

TEST_F(EmergencyStopUseCaseTest, EmergencyStopCommandSentViaDriver) {
    // Given: 系统 Running
    syncToRunning();
    SystemContext* ctx = getContext();
    ASSERT_NE(ctx->driver(), nullptr);

    // When: 执行 UseCase
    UseCaseError result = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(result);

    // Then: pending command 已被消费（证明 driver->send() 被调用）
    EXPECT_FALSE(ctx->emergencyStopController().hasPendingCommand());
}

TEST_F(EmergencyStopUseCaseTest, ReleaseCommandSentViaDriver) {
    // Given: EmergencyStopped
    SystemContext* ctx = getContext();
    syncToRunning();
    UseCaseError err1 = emergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err1);
    plc.tick(100);
    ctx->emergencyStopController().applyFeedback(plc.getEmergencyStopFeedback());
    ASSERT_EQ(ctx->emergencyStopController().state(), SafetyState::EmergencyStopped);

    // When: 执行解除 UseCase
    UseCaseError err2 = releaseEmergencyStopUseCase.execute(manager, GROUP);
    expectSuccess(err2);

    // Then: pending command 已被消费
    EXPECT_FALSE(ctx->emergencyStopController().hasPendingCommand());
}
