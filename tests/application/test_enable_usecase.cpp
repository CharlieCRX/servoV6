// tests/application/test_enable_usecase.cpp
#include <gtest/gtest.h>
#include "application/axis/EnableUseCase.h"
#include "application/axis/AxisRepository.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class EnableUseCaseTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    AxisRepository repo;
    
    void SetUp() override {
        repo.registerAxis(AxisId::Y);
        repo.registerAxis(AxisId::Z);
    }
};

// 场景 1：正常上电
TEST_F(EnableUseCaseTest, ShouldSendEnableCommandWhenAxisIsDisabled) {
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

    EnableUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, true);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_TRUE(axisY.hasPendingCommand());
    EXPECT_TRUE(std::get<EnableCommand>(axisY.getPendingCommand()).active);
}

// 场景 2：安全拦截——运动中禁止断电
TEST_F(EnableUseCaseTest, ShouldRejectDisableWhenAxisIsMoving) {
    Axis& axisY = repo.getAxis(AxisId::Y);
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

    EnableUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, false); // 尝试断电

    EXPECT_EQ(result, RejectionReason::AlreadyMoving);
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 3：多轴隔离——只使能目标轴
TEST_F(EnableUseCaseTest, ShouldOnlyAffectTargetAxis) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    Axis& axisZ = repo.getAxis(AxisId::Z);

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

    EnableUseCase usecase(repo, driver);
    usecase.execute(AxisId::Y, true);

    EXPECT_TRUE(axisY.hasPendingCommand());  // Y 轴有 EnableCommand
    EXPECT_FALSE(axisZ.hasPendingCommand()); // Z 轴不受影响
}
