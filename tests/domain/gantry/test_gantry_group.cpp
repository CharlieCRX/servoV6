#include <gtest/gtest.h>

#include "gantry/GantryGroup.h"

TEST(GantryGroup,
     should_hold_x1_and_x2)
{
    Axis x1;
    Axis x2;

    GantryGroup gantry(x1, x2);

    EXPECT_FALSE(gantry.isCoupled());
}