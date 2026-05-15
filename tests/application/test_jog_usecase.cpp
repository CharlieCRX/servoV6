#include <gtest/gtest.h>
#include "application/axis/JogAxisUseCase.h"
#include "application/UseCaseError.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class JogAxisUseCaseTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager mgr;

    SystemContext* group = nullptr;

    void SetUp() override {
        ContextRejection reason;
        ASSERT_TRUE(mgr.createGroup("Default", reason));
        ASSERT_TRUE(mgr.tryGetGroup("Default", group, reason));
        group->setDriver(&driver);

        // 同步急停控制器到 Running 状态（首次 PLC 反馈）
        group->emergencyStopController().applyFeedback(false);

        // 初始化所有轴的反馈
        setupAxis(AxisId::Y, AxisState::Idle, 0.0);
        setupAxis(AxisId::Z, AxisState::Idle, 200.0);
        setupAxis(AxisId::X, AxisState::Idle, 0.0);
        setupAxis(AxisId::X1, AxisState::Idle, 0.0);
        setupAxis(AxisId::X2, AxisState::Idle, 0.0);
    }

    void setupAxis(AxisId id, AxisState state, double pos) {
        Axis* ax = nullptr;
        ContextRejection r;
        if (group->tryGetAxis(id, ax, r) && ax) {
            ax->applyFeedback({
                .state = state,
                .absPos = pos,
                .relPos = pos,
                .relZeroAbsPos = 0.0,
                .posLimit = false,
                .negLimit = false,
                .posLimitValue = 1000.0,
                .negLimitValue = -1000.0
            });
        }
    }

    JogAxisUseCase usecase;
};

// ========================================================================
// ═══ 原有场景（迁移为 SystemManager + UseCaseError 格式） ═══
// ========================================================================

// 场景 1：Idle 状态下直接点动
TEST_F(JogAxisUseCaseTest, ShouldSendJogCommandWhenAxisIsIdle) {
    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));

    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ASSERT_NE(ax, nullptr);
    EXPECT_TRUE(ax->hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<JogCommand>(ax->getPendingCommand()));
}

// 场景 2：Error 状态下不点动
TEST_F(JogAxisUseCaseTest, ShouldReturnInvalidStateWhenAxisIsInError) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ax->applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<RejectionReason>(result));
    EXPECT_EQ(std::get<RejectionReason>(result), RejectionReason::InvalidState);
    EXPECT_FALSE(ax->hasPendingCommand());
}

// 场景 3：限位拦截
TEST_F(JogAxisUseCaseTest, ShouldReturnLimitReasonWhenAtBoundary) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ax->applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1000.0,
        .relPos = 1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = true,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<RejectionReason>(result));
    EXPECT_EQ(std::get<RejectionReason>(result), RejectionReason::AtPositiveLimit);
    EXPECT_FALSE(ax->hasPendingCommand());
}

// 场景 4：停止点动
TEST_F(JogAxisUseCaseTest, ShouldSendStopCommandWhenStopJogRequested) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ASSERT_NE(ax, nullptr);

    // 启动点动
    usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);
    EXPECT_TRUE(ax->hasPendingCommand());

    // 停止点动
    usecase.stop(mgr, "Default", AxisId::Y, Direction::Forward);

    // 验证：停止指令已挂载
    EXPECT_TRUE(ax->hasPendingCommand());
    auto cmd = std::get<JogCommand>(ax->getPendingCommand());
    EXPECT_FALSE(cmd.active);
    EXPECT_EQ(cmd.dir, Direction::Forward);
}

// 场景 5：Error 状态下停止仍可下发
TEST_F(JogAxisUseCaseTest, ShouldSendStopCommandEvenIfAxisIsInError) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ax->applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    usecase.stop(mgr, "Default", AxisId::Y, Direction::Backward);

    // 即使 Error 状态，stop 也能产生 JogCommand(active=false)
    EXPECT_TRUE(ax->hasPendingCommand());
    EXPECT_FALSE(std::get<JogCommand>(ax->getPendingCommand()).active);
}

// 场景 6：Disabled 必须失败
TEST_F(JogAxisUseCaseTest, ShouldRejectWhenAxisIsDisabled) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ax->applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<RejectionReason>(result));
    EXPECT_EQ(std::get<RejectionReason>(result), RejectionReason::InvalidState);
    EXPECT_FALSE(ax->hasPendingCommand());
}

// 场景 7：UseCase 不允许自动使能
TEST_F(JogAxisUseCaseTest, ShouldNotAutoEnableAxis) {
    Axis* ax = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, ax, r);
    ax->applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<RejectionReason>(result));
    EXPECT_EQ(std::get<RejectionReason>(result), RejectionReason::InvalidState);
    // 绝对不能发送 EnableCommand
    EXPECT_FALSE(ax->hasPendingCommand());
}

// 场景 8：多轴隔离——正向点动只影响目标轴
TEST_F(JogAxisUseCaseTest, ShouldOnlyAffectTargetAxis) {
    UseCaseError result = usecase.execute(mgr, "Default", AxisId::Y, Direction::Forward);

    EXPECT_TRUE(std::holds_alternative<std::monostate>(result));

    Axis* axY = nullptr;
    Axis* axZ = nullptr;
    ContextRejection r;
    group->tryGetAxis(AxisId::Y, axY, r);
    group->tryGetAxis(AxisId::Z, axZ, r);
    ASSERT_NE(axY, nullptr);
    ASSERT_NE(axZ, nullptr);

    EXPECT_TRUE(axY->hasPendingCommand());   // Y 轴有命令
    EXPECT_FALSE(axZ->hasPendingCommand());  // Z 轴不受影响
}

// ========================================================================
// ═══ 新增场景：分组层错误（SystemManager 层） ═══
// ========================================================================

// 场景 9：分组不存在
TEST_F(JogAxisUseCaseTest, ShouldReturnGroupNotFoundWhenGroupMissing) {
    UseCaseError result = usecase.execute(mgr, "NonExistent", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::GroupNotFound);
}

// 场景 10：分组名称为空
TEST_F(JogAxisUseCaseTest, ShouldReturnGroupNameInvalidWhenNameEmpty) {
    UseCaseError result = usecase.execute(mgr, "", AxisId::Y, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::GroupNameInvalid);
}

// ========================================================================
// ═══ 新增场景：龙门联动锁定（SystemContext 层） ═══
// ========================================================================

// 场景 11：龙门联动时，禁止直接点动物理轴 X1
TEST_F(JogAxisUseCaseTest, ShouldRejectPhysicalAxisWhenGantryCoupled) {
    // 默认 SetUp 中龙门处于联动状态（isGantryCoupled = true）
    group->gantryCouplingController().applyFeedback({.isCoupled = true, .errorCode = 0});  // 明确反馈龙门联动状态
    UseCaseError result = usecase.execute(mgr, "Default", AxisId::X1, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::PhysicalAxisLockedByGantry);
}

// 场景 12：龙门联动时，禁止直接点动物理轴 X2
TEST_F(JogAxisUseCaseTest, ShouldRejectX2WhenGantryCoupled) {
    group->gantryCouplingController().applyFeedback({.isCoupled = true, .errorCode = 0});  // 明确反馈龙门联动状态
    UseCaseError result = usecase.execute(mgr, "Default", AxisId::X2, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::PhysicalAxisLockedByGantry);
}

// 场景 13：龙门解耦时，禁止点动逻辑轴 X
TEST_F(JogAxisUseCaseTest, ShouldRejectLogicalAxisWhenGantryDecoupled) {
    group->gantryCouplingController().applyFeedback({.isCoupled = false, .errorCode = 0});

    UseCaseError result = usecase.execute(mgr, "Default", AxisId::X, Direction::Forward);

    ASSERT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::LogicalAxisUnavailableWhenDecoupled);
}

// ========================================================================
// ═══ 新增场景：stop 的分组感知 ═══
// ========================================================================

// 场景 14：stop 对不存在的分组静默无操作
TEST_F(JogAxisUseCaseTest, StopShouldSilentlyIgnoreMissingGroup) {
    // 不应崩溃或抛异常
    EXPECT_NO_FATAL_FAILURE({
        usecase.stop(mgr, "NonExistent", AxisId::Y, Direction::Forward);
    });
}

// 场景 15：stop 在龙门锁定轴上静默返回（不崩溃、不产生命令）
TEST_F(JogAxisUseCaseTest, StopShouldSilentlyReturnOnGantryLockedAxis) {
    // 龙门联动时 X1 被 tryGetAxis 拦截，stop() 静默返回
    // 不崩溃，不产生任何驱动命令
    driver.history.clear();

    EXPECT_NO_FATAL_FAILURE({
        usecase.stop(mgr, "Default", AxisId::X1, Direction::Forward);
    });

    EXPECT_TRUE(driver.history.empty());
}
