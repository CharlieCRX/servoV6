#include <gtest/gtest.h>
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/AxisRepository.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class MoveAbsoluteUseCaseTest : public ::testing::Test {
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

TEST_F(MoveAbsoluteUseCaseTest, ShouldRouteCommandToTargetAxis) {
    // 准备：让 Y 轴处于允许运动的 Idle 状态
    Axis& axisY = repo.getAxis(AxisId::Y);
    // 将 24 行改为：补全必需的核心字段，避免警告
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

    // 实例化 UseCase，注意现在要注入 Repo 了
    MoveAbsoluteUseCase usecase(repo, driver);
    
    RejectionReason result = usecase.execute(AxisId::Y, 500.0);

    // 断言 1：业务层不应拒绝
    EXPECT_EQ(result, RejectionReason::None);
    
    // 断言 2：Y轴的“物理状态”依然是 Idle（因为还在等 PLC 反馈），但它内部必须已经挂载了“移动意图”
    EXPECT_EQ(axisY.state(), AxisState::Idle); 
    EXPECT_TRUE(axisY.hasPendingCommand()); // 这是 Axis.h 里提供的方法
    
    // 断言 3：Z 轴完全不受影响，既没有状态变化，也没有意图
    Axis& axisZ = repo.getAxis(AxisId::Z);
    EXPECT_EQ(axisZ.state(), AxisState::Unknown);
    EXPECT_FALSE(axisZ.hasPendingCommand());
}