#include <gtest/gtest.h>
#include <variant>
#include "application/axis/MoveRelativeUseCase.h"
#include "application/SystemManager.h"
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================
// MoveRelativeUseCase TDD 测试套件
//
// 验证完整调用链：
//   UI → MoveRelativeUseCase.execute(manager, groupName, axisId, distance)
//      → 阶段 0: SystemManager::tryGetGroup
//      → 阶段 1: SystemContext::tryGetAxis (含龙门校验)
//      → 阶段 2: Axis::moveRelative (领域状态判定 + 限位预检)
//      → 阶段 3: 驱动下发
//      → 返回 UseCaseError
// ============================================================

// ── 辅助 ─────────────────────────────────────────────────────

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

class MoveRelativeUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    MoveRelativeUseCase useCase;
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

TEST_F(MoveRelativeUseCaseTest, MoveRelative_StandardAxisY_Success) {
    // Given: Y 轴 Idle，限位 [-1000, 1000]，当前位置 100
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 相对移动 +50
    UseCaseError result = useCase.execute(manager, GROUP, Y, 50.0);

    // Then
    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
}

// ============================================================
// 第二部分：SystemManager 层错误
// ============================================================

TEST_F(MoveRelativeUseCaseTest, NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = useCase.execute(manager, "GhostGroup", Y, 50.0);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

// ============================================================
// 第三部分：SystemContext 层错误 — 龙门联动锁定
// ============================================================

TEST_F(MoveRelativeUseCaseTest, MoveX1_WhenGantryCoupled_ReturnsPhysicalAxisLocked) {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantry().applyFeedback({.isCoupled = true, .errorCode = 0});

    UseCaseError result = useCase.execute(manager, GROUP, X1, 50.0);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(MoveRelativeUseCaseTest, MoveX1_WhenGantryDecoupled_PassesThrough) {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantry().applyFeedback({.isCoupled = false, .errorCode = 0});

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

    UseCaseError result = useCase.execute(manager, GROUP, X1, 50.0);

    expectSuccess(result);
    EXPECT_TRUE(x1->hasPendingCommand());
}

// ============================================================
// 第四部分：Axis 领域层错误 — 状态非法
// ============================================================

TEST_F(MoveRelativeUseCaseTest, Move_WhenAxisDisabled_ReturnsInvalidState) {
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

    UseCaseError result = useCase.execute(manager, GROUP, Y, 50.0);

    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::InvalidState);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveRelativeUseCaseTest, Move_WhenAxisInError_ReturnsInvalidState) {
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

    UseCaseError result = useCase.execute(manager, GROUP, Y, 50.0);

    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::InvalidState);
}

// ============================================================
// 第五部分：Axis 领域层错误 — 限位预检
// ============================================================

TEST_F(MoveRelativeUseCaseTest, Move_TargetExceedsPositiveLimit_ReturnsTargetOutOfPositiveLimit) {
    // Given: 当前位置 900，正限位 1000，delta +200 → 终点 1100 超限
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 900.0,
        .relPos = 900.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, Y, 200.0);

    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveRelativeUseCaseTest, Move_TargetExceedsNegativeLimit_ReturnsTargetOutOfNegativeLimit) {
    // Given: 当前位置 -900，负限位 -1000，delta -200 → 终点 -1100 超限
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = -900.0,
        .relPos = -900.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, Y, -200.0);

    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::TargetOutOfNegativeLimit);
    EXPECT_FALSE(y->hasPendingCommand());
}

TEST_F(MoveRelativeUseCaseTest, Move_AtPositiveLimit_ReturnsAtPositiveLimit) {
    // Given: 当前已触发正限位
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1000.0,
        .relPos = 1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = true,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 尝试正向前进
    UseCaseError result = useCase.execute(manager, GROUP, Y, 10.0);

    // Then
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::AtPositiveLimit);
}

TEST_F(MoveRelativeUseCaseTest, Move_AtNegativeLimit_ReturnsAtNegativeLimit) {
    // Given: 当前已触发负限位
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = -1000.0,
        .relPos = -1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = true,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // When: 尝试负向前进
    UseCaseError result = useCase.execute(manager, GROUP, Y, -10.0);

    // Then
    RejectionReason err = expectError<RejectionReason>(result);
    EXPECT_EQ(err, RejectionReason::AtNegativeLimit);
}

// ============================================================
// 第六部分：多轴隔离
// ============================================================

TEST_F(MoveRelativeUseCaseTest, X1LockedByGantry_YStillSucceeds) {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantry().applyFeedback({.isCoupled = true, .errorCode = 0});

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

    UseCaseError r1 = useCase.execute(manager, GROUP, X1, 50.0);
    expectError<ContextRejection>(r1);

    UseCaseError r2 = useCase.execute(manager, GROUP, Y, 50.0);
    expectSuccess(r2);
    EXPECT_TRUE(y->hasPendingCommand());
}

// ============================================================
// 第七部分：UseCase 无状态
// ============================================================

TEST_F(MoveRelativeUseCaseTest, Stateless_RepeatedCallsProduceConsistentResult) {
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

    UseCaseError r1 = useCase.execute(manager, GROUP, Y, 50.0);
    expectSuccess(r1);

    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 50.0,
        .relPos = 50.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    UseCaseError r2 = useCase.execute(manager, GROUP, Y, 100.0);
    expectSuccess(r2);
}
