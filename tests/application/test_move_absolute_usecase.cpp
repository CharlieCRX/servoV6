#include <gtest/gtest.h>
#include <variant>
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/SystemManager.h"
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================
// MoveAbsoluteUseCase TDD 测试套件
//
// 验证完整调用链：
//   UI -> MoveAbsoluteUseCase.execute(manager, groupName, axisId, target)
//      -> 阶段 0: SystemManager::tryGetGroup
//      -> 阶段 1: SystemContext::tryGetAxis (含龙门校验)
//      -> 阶段 2: Axis::moveAbsolute (领域状态判定 + 限位预检)
//      -> 阶段 3: 驱动下发
//      -> 返回 UseCaseError
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

class MoveAbsoluteUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    MoveAbsoluteUseCase useCase;
    FakePLC plc;
    FakeAxisDriver driver{plc};

    static constexpr const char* GROUP = "Machine_A";
    static constexpr AxisId X1 = AxisId::X1;
    static constexpr AxisId X2 = AxisId::X2;
    static constexpr AxisId Y  = AxisId::Y;

    void SetUp() override {
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP, reason));

        SystemContext* ctx = nullptr;
        ASSERT_TRUE(manager.tryGetGroup(GROUP, ctx, reason));
        ctx->setDriver(&driver);
        // 同步急停控制器到 Running 状态（首次 PLC 反馈）
        ctx->emergencyStopController().applyFeedback(false);
    }

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

TEST_F(MoveAbsoluteUseCaseTest, MoveAbsolute_StandardAxisY_Success) {
    // Given: Y 轴 Idle，限位 [-1000, 1000]，当前位置 0
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 绝对定位到 500
    UseCaseError result = useCase.execute(manager, GROUP, Y, 500.0);

    // Then: 成功，且生成了待发送命令
    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
}

// ============================================================
// 第二部分：SystemManager 层错误 -- 分组不存在
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = useCase.execute(manager, "GhostGroup", Y, 500.0);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

// ============================================================
// 第三部分：SystemContext 层错误 -- 龙门联动锁定
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, MoveX1_WhenGantryCoupled_ReturnsPhysicalAxisLocked) {
    // Given: 龙门联动中，X1 物理轴被锁定
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantryCouplingController().applyFeedback({.isCoupled = true, .errorCode = 0});

    // When: 尝试直接操作物理轴 X1
    UseCaseError result = useCase.execute(manager, GROUP, X1, 300.0);

    // Then: 被 SystemContext 拦截
    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(MoveAbsoluteUseCaseTest, MoveX1_WhenGantryDecoupled_PassesThrough) {
    // Given: 龙门解耦
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantryCouplingController().applyFeedback({.isCoupled = false, .errorCode = 0});

    Axis* x1 = getAxis(X1);
    ASSERT_NE(x1, nullptr);
    x1->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 操作 X1
    UseCaseError result = useCase.execute(manager, GROUP, X1, 300.0);

    // Then: 通过
    expectSuccess(result);
    EXPECT_TRUE(x1->hasPendingCommand());
}

// ============================================================
// 第四部分：Axis 领域层错误 -- 状态非法
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, Move_WhenAxisDisabled_ReturnsInvalidState) {
    // Given: Y 轴未使能
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 直接发定位指令
    UseCaseError result = useCase.execute(manager, GROUP, Y, 500.0);

    // Then: 被 Axis 层拦截
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::InvalidState);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveAbsoluteUseCaseTest, Move_WhenAxisInError_ReturnsInvalidState) {
    // Given: Y 轴故障
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When
    UseCaseError result = useCase.execute(manager, GROUP, Y, 500.0);

    // Then
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::InvalidState);
}

// ============================================================
// 第五部分：Axis 领域层错误 -- 限位预检
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, Move_TargetExceedsPositiveLimit_ReturnsTargetOutOfPositiveLimit) {
    // Given: 正限位 1000，目标 1500
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 目标超出正限位
    UseCaseError result = useCase.execute(manager, GROUP, Y, 1500.0);

    // Then
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveAbsoluteUseCaseTest, Move_TargetExceedsNegativeLimit_ReturnsTargetOutOfNegativeLimit) {
    // Given: 负限位 -1000，目标 -1500
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 目标超出负限位
    UseCaseError result = useCase.execute(manager, GROUP, Y, -1500.0);

    // Then
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::TargetOutOfNegativeLimit);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveAbsoluteUseCaseTest, Move_AtPositiveLimit_ReturnsAtPositiveLimit) {
    // Given: 当前已触发正限位信号
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1000.0,
        .relPos = 1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = true,        // ⚠️ 已在正限位
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 尝试正向定位
    UseCaseError result = useCase.execute(manager, GROUP, Y, 1100.0);

    // Then: 被 AtPositiveLimit 拦截
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::AtPositiveLimit);
}

TEST_F(MoveAbsoluteUseCaseTest, Move_AtNegativeLimit_ReturnsAtNegativeLimit) {
    // Given: 当前已触发负限位信号
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = -1000.0,
        .relPos = -1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = true,        // ⚠️ 已在负限位
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 尝试负向定位
    UseCaseError result = useCase.execute(manager, GROUP, Y, -1100.0);

    // Then: 被 AtNegativeLimit 拦截
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::AtNegativeLimit);
}

// ============================================================
// 第六部分：多轴操作 -- 不同轴错误互不干扰
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, X1LockedByGantry_YStillSucceeds) {
    // Given: 龙门联动中，X1 被锁定
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantryCouplingController().applyFeedback({.isCoupled = true, .errorCode = 0});

    // Y 轴正常
    Axis* y = getAxis(Y);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When & Then: X1 失败
    UseCaseError r1 = useCase.execute(manager, GROUP, X1, 300.0);
    expectError<ContextRejection>(r1);

    // When & Then: Y 仍然成功
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, 500.0);
    expectSuccess(r2);
    EXPECT_TRUE(y->hasPendingCommand());
}

// ============================================================
// 第七部分：UseCase 无状态 -- 多次调用互不影响
// ============================================================

TEST_F(MoveAbsoluteUseCaseTest, Stateless_RepeatedCallsProduceConsistentResult) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError r1 = useCase.execute(manager, GROUP, Y, 500.0);
    expectSuccess(r1);

    // 模拟反馈消费命令后，再次调用
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 500.0,
        .relPos = 500.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, 800.0);
    expectSuccess(r2);
}
