#include <gtest/gtest.h>
#include "entity/Axis.h"

TEST(AxisTest, ShouldBeIdleInitially)
{
    Axis axis;

    EXPECT_EQ(axis.state(), AxisState::Disabled);
}
