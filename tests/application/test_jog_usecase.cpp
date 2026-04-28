#include <gtest/gtest.h>
#include "application/axis/JogAxisUseCase.h"
#include "application/axis/AxisRepository.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class JogAxisUseCaseTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    AxisRepository repo;
    
    void SetUp() override {
        repo.registerAxis(AxisId::Y);
        repo.registerAxis(AxisId::Z);
    }
};

// 场景 1：Idle 状态下直接点动
TEST_F(JogAxisUseCaseTest, ShouldSendJogCommandWhenAxisIsIdle) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_TRUE(axisY.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<JogCommand>(axisY.getPendingCommand()));
}

// 场景 2：Error 状态下不点动
TEST_F(JogAxisUseCaseTest, ShouldReturnInvalidStateWhenAxisIsInError) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 3：限位拦截
TEST_F(JogAxisUseCaseTest, ShouldReturnLimitReasonWhenAtBoundary) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1000.0,
        .relPos = 1000.0,
        .relZeroAbsPos = 0.0,
        .posLimit = true,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::AtPositiveLimit);
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 4：停止点动
TEST_F(JogAxisUseCaseTest, ShouldSendStopCommandWhenStopJogRequested) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);

    // 启动点动
    usecase.execute(AxisId::Y, Direction::Forward);
    EXPECT_TRUE(axisY.hasPendingCommand());

    // 停止点动
    usecase.stop(AxisId::Y, Direction::Forward);

    // 验证：停止指令已挂载到轴上
    EXPECT_TRUE(axisY.hasPendingCommand());
    auto cmd = std::get<JogCommand>(axisY.getPendingCommand());
    EXPECT_FALSE(cmd.active);
    EXPECT_EQ(cmd.dir, Direction::Forward);
}

// 场景 5：Error 状态下停止仍可下发
TEST_F(JogAxisUseCaseTest, ShouldSendStopCommandEvenIfAxisIsInError) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    usecase.stop(AxisId::Y, Direction::Backward);

    // 即使 Error 状态，stop 也能产生 JogCommand(active=false)
    EXPECT_TRUE(axisY.hasPendingCommand());
    EXPECT_FALSE(std::get<JogCommand>(axisY.getPendingCommand()).active);
}

// 场景 6：Disabled 必须失败
TEST_F(JogAxisUseCaseTest, ShouldRejectWhenAxisIsDisabled) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 7：UseCase 不允许自动使能
TEST_F(JogAxisUseCaseTest, ShouldNotAutoEnableAxis) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    // 验证：绝对不能发送 EnableCommand（没有 pending command 意味着未触及驱动）
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 8：多轴隔离——正向点动只影响目标轴
TEST_F(JogAxisUseCaseTest, ShouldOnlyAffectTargetAxis) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    Axis& axisZ = repo.getAxis(AxisId::Z);

    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    axisZ.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 200.0,
        .relPos = 200.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(repo, driver);
    usecase.execute(AxisId::Y, Direction::Forward);

    EXPECT_TRUE(axisY.hasPendingCommand());  // Y 轴有命令
    EXPECT_FALSE(axisZ.hasPendingCommand()); // Z 轴不受影响
}
