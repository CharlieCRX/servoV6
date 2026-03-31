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
