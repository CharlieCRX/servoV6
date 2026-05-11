#include <gtest/gtest.h>
#include "gantry/GantryGroup.h"

TEST(GantryGroup, should_hold_x1_and_x2)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    EXPECT_FALSE(gantry.isCoupled());
}

TEST(GantryGroup, should_request_and_mark_coupled)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // 1. 发起请求
    gantry.requestCouple();
    EXPECT_TRUE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.isCoupled());

    // 2. 模拟底层物理确立
    gantry.applyCoupledFeedback();
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.isCoupled());
}