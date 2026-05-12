#include <gtest/gtest.h>
#include <variant>
#include "application/axis/EnableUseCase.h"
#include "application/SystemManager.h"
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================
// EnableUseCase TDD 测试套件
//
// 验证完整调用链：
//   UI → EnableUseCase.execute(manager, groupName, axisId, active)
//      → 阶段 0: SystemManager::tryGetGroup
//      → 阶段 1: SystemContext::tryGetAxis (含龙门校验)
//      → 阶段 2: Axis::enable (领域状态判定)
//      → 阶段 3: 驱动下发
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

class EnableUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    EnableUseCase useCase;
    FakePLC plc;                         // 假 PLC，状态寄存器内存驻留
    FakeAxisDriver driver{plc};          // 假驱动，路由到 FakePLC

    static constexpr const char* GROUP = "Machine_A";
    static constexpr AxisId X1 = AxisId::X1;
    static constexpr AxisId X2 = AxisId::X2;
    static constexpr AxisId Y  = AxisId::Y;

    void SetUp() override {
        // 1. 创建分组
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP, reason));

        // 2. 注入假驱动
        SystemContext* ctx = nullptr;
        ASSERT_TRUE(manager.tryGetGroup(GROUP, ctx, reason));
        ctx->setDriver(&driver);
    }

    // 快捷方法：获取分组内指定的轴
    Axis* getAxis(AxisId id) {
        SystemContext* ctx = nullptr;
        ContextRejection reason;
        manager.tryGetGroup(GROUP, ctx, reason);
        Axis* axis = nullptr;
        ContextRejection ctxReason;
        ctx->tryGetAxis(id, axis, ctxReason);
        return axis;
    }
};

// ============================================================
// 第一部分：成功路径
// ============================================================

TEST_F(EnableUseCaseTest, Enable_StandardAxisY_Success) {
    // Given: Y 轴处于 Disabled 状态
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Disabled});

    // When: 使能 Y 轴
    UseCaseError result = useCase.execute(manager, GROUP, Y, true);

    // Then: 成功，且生成了待发送命令
    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
}

TEST_F(EnableUseCaseTest, Disable_StandardAxisY_Success) {
    // Given: Y 轴处于 Idle 状态（已使能）
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Idle});

    // When: 掉电
    UseCaseError result = useCase.execute(manager, GROUP, Y, false);

    // Then: 成功
    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
}

TEST_F(EnableUseCaseTest, Enable_WhenAlreadyEnabled_IdempotentSuccess) {
    // Given: Y 轴已经在 Idle 状态
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Idle});

    // When: 再次使能
    UseCaseError result = useCase.execute(manager, GROUP, Y, true);

    // Then: 幂等返回成功（Axis 层不产生新命令）
    expectSuccess(result);
    // 已经是使能状态了，不会产生新的待发送命令
}

TEST_F(EnableUseCaseTest, Disable_WhenAlreadyDisabled_IdempotentSuccess) {
    // Given: Y 轴已处于 Disabled 状态
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Disabled});

    // When: 再次掉电
    UseCaseError result = useCase.execute(manager, GROUP, Y, false);

    // Then: 幂等返回成功
    expectSuccess(result);
}

// ============================================================
// 第二部分：SystemManager 层错误 — 分组不存在
// ============================================================

TEST_F(EnableUseCaseTest, NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = useCase.execute(manager, "GhostGroup", Y, true);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

// ============================================================
// 第三部分：SystemContext 层错误 — 龙门联动锁定
// ============================================================

TEST_F(EnableUseCaseTest, EnableX1_WhenGantryCoupled_ReturnsPhysicalAxisLocked) {
    // Given: 龙门联动中，X1 物理轴被锁定
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->setCoupledState(true);  // 模拟联动状态

    // When: 尝试直接操作物理轴 X1
    UseCaseError result = useCase.execute(manager, GROUP, X1, true);

    // Then: 被 SystemContext 拦截
    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(EnableUseCaseTest, EnableX1_WhenGantryDecoupled_PassesThrough) {
    // Given: 龙门解耦
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->setCoupledState(false);

    Axis* x1 = getAxis(X1);
    ASSERT_NE(x1, nullptr);
    x1->applyFeedback({.state = AxisState::Disabled});

    // When: 操作 X1
    UseCaseError result = useCase.execute(manager, GROUP, X1, true);

    // Then: 通过，物理轴可独立操作
    expectSuccess(result);
    EXPECT_TRUE(x1->hasPendingCommand());
}

// ============================================================
// 第四部分：Axis 领域层错误
// ============================================================

TEST_F(EnableUseCaseTest, Enable_WhenAxisInError_ReturnsInvalidState) {
    // Given: Y 轴处于 Error 状态
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Error});

    // When: 尝试使能
    UseCaseError result = useCase.execute(manager, GROUP, Y, true);

    // Then: 被 Axis 层拦截（安全屏障：故障态禁上电）
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::InvalidState);
}

TEST_F(EnableUseCaseTest, Disable_WhenAxisMoving_ReturnsAlreadyMoving) {
    // Given: Y 轴正在 Jogging 运动
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Jogging});

    // When: 尝试直接掉电
    UseCaseError result = useCase.execute(manager, GROUP, Y, false);

    // Then: 被 Axis 层拦截（安全屏障：运动中禁掉电）
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::AlreadyMoving);
}

// ============================================================
// 第五部分：多轴操作 — 不同轴错误互不干扰
// ============================================================

TEST_F(EnableUseCaseTest, X1Failed_YStillSucceeds) {
    // Given: 龙门联动中，X1 被锁定
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->setCoupledState(true);

    // Y 轴正常
    Axis* y = getAxis(Y);
    y->applyFeedback({.state = AxisState::Disabled});

    // When & Then: X1 失败
    UseCaseError r1 = useCase.execute(manager, GROUP, X1, true);
    expectError<ContextRejection>(r1);

    // When & Then: Y 仍然成功
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, true);
    expectSuccess(r2);
    EXPECT_TRUE(y->hasPendingCommand());
}

// ============================================================
// 第六部分：往返测试 — Enable + Disable 完整循环
// ============================================================

TEST_F(EnableUseCaseTest, RoundTrip_EnableThenDisable_BothSucceed) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);

    // Step 1: Disabled → Enable
    y->applyFeedback({.state = AxisState::Disabled});
    UseCaseError r1 = useCase.execute(manager, GROUP, Y, true);
    expectSuccess(r1);

    // 模拟 PLC 反馈：上电成功，进入 Idle
    y->applyFeedback({.state = AxisState::Idle});

    // Step 2: Idle → Disable
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, false);
    expectSuccess(r2);
}

// ============================================================
// 第七部分：UseCase 无状态 — 多次调用互不影响
// ============================================================

TEST_F(EnableUseCaseTest, Stateless_RepeatedCallsProduceConsistentResult) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({.state = AxisState::Disabled});

    // 第一次
    UseCaseError r1 = useCase.execute(manager, GROUP, Y, true);
    expectSuccess(r1);

    // 模拟反馈驱动命令已消费
    y->applyFeedback({.state = AxisState::Idle});

    // 第二次（幂等）
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, true);
    expectSuccess(r2);
}
