#include <gtest/gtest.h>
#include "entity/Axis.h"

TEST(AxisTest, ShouldBeIdleInitially)
{
    Axis axis;

    EXPECT_EQ(axis.state(), AxisState::Disabled);
}

TEST(AxisTest, ShouldBeIdleWhenEnabled) {
    Axis axis; 
    // 假设默认构造函数让轴处于 Disabled 状态
    ASSERT_EQ(axis.state(), AxisState::Disabled);

    axis.enable();
    ASSERT_EQ(axis.state(), AxisState::Idle);
}

TEST(AxisTest, ShouldRemainIdleWhenEnabledMultipleTimes) {
    Axis axis; 
    ASSERT_EQ(axis.state(), AxisState::Disabled);

    axis.enable();
    ASSERT_EQ(axis.state(), AxisState::Idle);

    // 再次调用 enable()，状态应该保持不变
    axis.enable();
    ASSERT_EQ(axis.state(), AxisState::Idle);
}

TEST(AxisTest, ShouldStoreManualSpeed) {
    Axis axis;
    double expectedSpeed = 50.0;

    // 动作：设置点动速度
    axis.setManualSpeed(expectedSpeed);

    // 断言：读取到的速度应与设置的一致
    // 注意：在 C++ 中比较浮点数建议使用 ASSERT_NEAR 避免精度误差
    ASSERT_NEAR(axis.manualSpeed(), expectedSpeed, 1e-5);
}