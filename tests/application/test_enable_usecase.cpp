// tests/application/test_enable_usecase.cpp
#include <gtest/gtest.h>
#include "application/axis/EnableUseCase.h"
#include "application/SystemManager.h"
#include "infrastructure/FakeAxisDriver.h" // 假设已实现 ISystemDriver

class EnableUseCaseTest : public ::testing::Test {
protected:
    SystemManager manager;
    EnableUseCase useCase;
    
    void SetUp() override {
        // 初始化一个标准机台分组
        manager.createGroup("Machine_A");
        // 绑定模拟驱动
        // manager.getGroup("Machine_A")->setDriver(new FakeSystemDriver()); 
    }
};

// 场景 1：常规轴 Y 在正常状态下使能成功
TEST_F(EnableUseCaseTest, ShouldEnableStandardAxisSuccessfully) {
    SystemContext* group = manager.getGroup("Machine_A");
    
    // 模拟 Y 轴当前处于 Disabled 状态
    Axis* axisY = nullptr;
    RejectionReason r;
    group->tryGetAxis(AxisId::Y, axisY, r);
    axisY->applyFeedback({.state = AxisState::Disabled});

    // 执行 UseCase
    RejectionReason result = useCase.execute(*group, AxisId::Y, true);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_TRUE(axisY->hasPendingCommand());
}

// 场景 2：联动模式下拦截物理轴 X1 的使能 (核心重构点)
TEST_F(EnableUseCaseTest, ShouldRejectX1Enable_WhenGantryIsCoupled) {
    SystemContext* group = manager.getGroup("Machine_A");
    group->setCoupledState(true); // 设为联动模式

    // 执行 UseCase，尝试操作物理轴 X1
    RejectionReason result = useCase.execute(*group, AxisId::X1, true);

    // 预期结果：被 SystemContext 拦截，返回 InvalidState
    EXPECT_EQ(result, RejectionReason::InvalidState);
}

// 场景 3：解耦模式下允许物理轴 X1 的使能
TEST_F(EnableUseCaseTest, ShouldAllowX1Enable_WhenGantryIsDecoupled) {
    SystemContext* group = manager.getGroup("Machine_A");
    group->setCoupledState(false); // 设为解耦模式

    // 模拟 X1 轴反馈
    Axis* axisX1 = nullptr;
    RejectionReason r;
    group->tryGetAxis(AxisId::X1, axisX1, r);
    axisX1->applyFeedback({.state = AxisState::Disabled});

    // 执行 UseCase
    RejectionReason result = useCase.execute(*group, AxisId::X1, true);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_TRUE(axisX1->hasPendingCommand());
}