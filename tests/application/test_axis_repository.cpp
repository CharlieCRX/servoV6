#include <gtest/gtest.h>
#include "application/axis/AxisRepository.h"

TEST(AxisRepositoryTest, ShouldReturnIndependentAxisInstances) {
    AxisRepository repo;
    
    // 1. 注册两个独立的轴
    repo.registerAxis(AxisId::Y);
    repo.registerAxis(AxisId::Z);

    Axis& axisY = repo.getAxis(AxisId::Y);
    Axis& axisZ = repo.getAxis(AxisId::Z);

    // 2. 改变 Y 轴的状态
    axisY.applyFeedback({.state = AxisState::Idle});
    
    // 3. 断言：Z 轴绝对不能受到 Y 轴的影响
    EXPECT_EQ(axisY.state(), AxisState::Idle);
    EXPECT_EQ(axisZ.state(), AxisState::Unknown); // Z 轴应保持初始态
}

TEST(AxisRepositoryTest, ShouldThrowWhenAccessingUnregisteredAxis) {
    AxisRepository repo;
    repo.registerAxis(AxisId::Y);

    // 尝试获取未注册的 R 轴，必须抛出异常
    EXPECT_THROW(repo.getAxis(AxisId::R), std::out_of_range);
}