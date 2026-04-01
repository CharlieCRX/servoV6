#include <gtest/gtest.h>
#include "entity/Axis.h"

// 软件承认自己不知道状态
TEST(AxisTest, ShouldBeIdleInitially)
{
    Axis axis;

    EXPECT_EQ(axis.state(), AxisState::Unknown);
}