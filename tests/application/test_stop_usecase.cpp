#include <gtest/gtest.h>
#include "application/axis/StopAxisUseCase.h"
#include "application/axis/AxisRepository.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class StopAxisUseCaseTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    AxisRepository repo;
    
    void SetUp() override {
        repo.registerAxis(AxisId::Y);
        repo.registerAxis(AxisId::Z);
    }
};

// 场景 1：验证停止指令的下发与意图覆盖
TEST_F(StopAxisUseCaseTest, ShouldSendStopCommandAndClearMovingIntent) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    // 准备环境：轴正在绝对定位运动中
    axisY.applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    StopAxisUseCase usecase(repo, driver);
    
    // 动作：执行停止
    usecase.execute(AxisId::Y);

    // 验证：轴上挂载了 StopCommand
    EXPECT_TRUE(axisY.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(axisY.getPendingCommand()));
}

// 场景 2：安全穿透——即便在 Error 状态也必须能发停止指令
TEST_F(StopAxisUseCaseTest, ShouldSendStopCommandEvenInErrorState) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    axisY.applyFeedback({
        .state = AxisState::Error,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    StopAxisUseCase usecase(repo, driver);
    usecase.execute(AxisId::Y);

    // 验证：即便故障，停止信号也要发给 PLC
    EXPECT_TRUE(axisY.hasPendingCommand());
    EXPECT_TRUE(std::holds_alternative<StopCommand>(axisY.getPendingCommand()));
}

// 场景 3：多轴隔离——只停止目标轴
TEST_F(StopAxisUseCaseTest, ShouldOnlyStopTargetAxis) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    Axis& axisZ = repo.getAxis(AxisId::Z);

    axisY.applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 100.0,
        .relPos = 100.0,
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

    StopAxisUseCase usecase(repo, driver);
    usecase.execute(AxisId::Y);

    EXPECT_TRUE(axisY.hasPendingCommand());  // Y 轴有 StopCommand
    EXPECT_FALSE(axisZ.hasPendingCommand()); // Z 轴无变化
}
