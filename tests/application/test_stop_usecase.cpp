#include <gtest/gtest.h>
#include <variant>
#include "application/axis/StopAxisUseCase.h"
#include "application/SystemManager.h"
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================
// StopAxisUseCase TDD 测试套件
//
// 验证完整调用链：
//   UI -> StopAxisUseCase.execute(manager, groupName, axisId)
//      -> 阶段 0: SystemManager::tryGetGroup
//      -> 阶段 1: SystemContext::tryGetAxis (含龙门校验)
//      -> 阶段 2: Axis::stop (领域层不可拒绝)
//      -> 返回 UseCaseError
//
// 设计要点：停止是安全指令，在领域层被设计为不可拒绝，
// 即使 Error / Disabled 状态也必须能下发停止命令。
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

class StopAxisUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    StopAxisUseCase useCase;
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
// 第一部分：成功路径 -- 语义为不可拒绝的安全指令
// ============================================================

TEST_F(StopAxisUseCaseTest, ShouldSendStopCommand_WhenIdle) {
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

    UseCaseError result = useCase.execute(manager, GROUP, Y);

    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(y->getPendingCommand()));
}

TEST_F(StopAxisUseCaseTest, ShouldSendStopCommand_WhenMovingAbsolute) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, Y);

    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(y->getPendingCommand()));
}

// ============================================================
// 第二部分：安全穿透 -- Error / Disabled 状态下也必须能停止
// ============================================================

TEST_F(StopAxisUseCaseTest, ShouldSendStopCommand_EvenInErrorState) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Error,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, Y);

    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(y->getPendingCommand()));
}

TEST_F(StopAxisUseCaseTest, ShouldSendStopCommand_EvenWhenDisabled) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, Y);

    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(y->getPendingCommand()));
}

// ============================================================
// 第三部分：SystemManager 层错误 -- 分组不存在
// ============================================================

TEST_F(StopAxisUseCaseTest, NonExistentGroup_ReturnsGroupNotFound) {
    UseCaseError result = useCase.execute(manager, "GhostGroup", Y);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::GroupNotFound);
}

// ============================================================
// 第四部分：SystemContext 层错误 -- 龙门联动锁定
// ============================================================

TEST_F(StopAxisUseCaseTest, StopX1_WhenGantryCoupled_ReturnsPhysicalAxisLocked) {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantryCouplingController().applyFeedback({.isCoupled = true, .errorCode = 0});

    UseCaseError result = useCase.execute(manager, GROUP, X1);

    ContextRejection err = expectError<ContextRejection>(result);
    EXPECT_EQ(err, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(StopAxisUseCaseTest, StopX1_WhenGantryDecoupled_PassesThrough) {
    SystemContext* ctx = nullptr;
    ContextRejection reason;
    manager.tryGetGroup(GROUP, ctx, reason);
    ctx->gantryCouplingController().applyFeedback({.isCoupled = false, .errorCode = 0});

    Axis* x1 = getAxis(X1);
    ASSERT_NE(x1, nullptr);
    x1->applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 50.0,
        .relPos = 50.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = useCase.execute(manager, GROUP, X1);

    expectSuccess(result);
    EXPECT_TRUE(x1->hasPendingCommand());
}

// ============================================================
// 第五部分：多轴隔离 -- 只停止目标轴
// ============================================================

TEST_F(StopAxisUseCaseTest, ShouldOnlyStopTargetAxis) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    // Given: 龙门联动中 X2 被锁定不可直接访问，改用不受龙门约束的 Z 轴验证隔离性
    // SystemContext 默认包含 6 个轴：X, X1, X2, Y, Z, R
    // 这里验证 Z 轴不受 Y 的 stop 影响
    Axis* z = getAxis(AxisId::Z);
    ASSERT_NE(z, nullptr);

    UseCaseError result = useCase.execute(manager, GROUP, Y);

    expectSuccess(result);
    EXPECT_TRUE(y->hasPendingCommand());     // Y 轴有 StopCommand
    EXPECT_FALSE(z->hasPendingCommand());   // Z 轴无变化
}

// ============================================================
// 第六部分：UseCase 无状态
// ============================================================

TEST_F(StopAxisUseCaseTest, Stateless_RepeatedStopsSucceed) {
    Axis* y = getAxis(Y);
    ASSERT_NE(y, nullptr);
    y->applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError r1 = useCase.execute(manager, GROUP, Y);
    expectSuccess(r1);

    // 模拟反馈消费命令后，再次停止
    y->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 200.0,
        .relPos = 200.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    UseCaseError r2 = useCase.execute(manager, GROUP, Y);
    expectSuccess(r2);
}
