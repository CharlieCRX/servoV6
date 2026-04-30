#include <gtest/gtest.h>
#include "application/axis/MoveRelativeUseCase.h"
#include "application/axis/AxisRepository.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class MoveRelativeUseCaseTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    AxisRepository repo;
    
    void SetUp() override {
        // 注册两个轴
        repo.registerAxis(AxisId::Y);
        repo.registerAxis(AxisId::Z);
    }
};

// 场景 1：正常相对位移下发
TEST_F(MoveRelativeUseCaseTest, ShouldSendMoveCommandWhenDistanceIsLegal) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    // 环境：当前位置 100.0，限位 [-1000, 1000]
    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 100.0,
        .relPos = 100.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    MoveRelativeUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, 50.0); // 预期终点 150.0

    EXPECT_EQ(result, RejectionReason::None);
    
    // 验证命令已挂载到 Axis 上
    EXPECT_TRUE(axisY.hasPendingCommand());
    auto cmd = std::get<MoveCommand>(axisY.getPendingCommand());
    EXPECT_EQ(cmd.type, MoveType::Relative);
    EXPECT_DOUBLE_EQ(cmd.target, 50.0);
}

// 场景 2：未使能拦截（不执行自愈）
TEST_F(MoveRelativeUseCaseTest, ShouldReturnInvalidStateWhenDisabled) {
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

    MoveRelativeUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, 10.0);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_FALSE(axisY.hasPendingCommand());
}

// 场景 3：增量导致终点越界拦截
TEST_F(MoveRelativeUseCaseTest, ShouldRejectWhenRelativeDistanceExceedsLimit) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    // 环境：当前位置 900.0，正限位 1000.0
    axisY.applyFeedback({
        .state = AxisState::Idle,
        .absPos = 900.0,
        .relPos = 900.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    MoveRelativeUseCase usecase(repo, driver);
    // 动作：移动 +200.0 (预期终点 1100.0，超限)
    RejectionReason result = usecase.execute(AxisId::Y, 200.0);

    EXPECT_EQ(result, RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_FALSE(axisY.hasPendingCommand()); // 不应挂载命令
}

// 场景 4：多轴隔离——只影响目标轴
TEST_F(MoveRelativeUseCaseTest, ShouldOnlyAffectTargetAxis) {
    Axis& axisY = repo.getAxis(AxisId::Y);
    Axis& axisZ = repo.getAxis(AxisId::Z);
    
    axisY.applyFeedback({
        .state = AxisState::Idle,
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

    MoveRelativeUseCase usecase(repo, driver);
    RejectionReason result = usecase.execute(AxisId::Y, 50.0);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_TRUE(axisY.hasPendingCommand());  // Y 轴有命令
    EXPECT_FALSE(axisZ.hasPendingCommand()); // Z 轴不受影响
}
