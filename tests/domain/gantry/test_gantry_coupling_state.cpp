#include <gtest/gtest.h>

#include "gantry/GantryCouplingState.h"

TEST(GantryCouplingState, should_be_decoupled_by_default)
{
    GantryCouplingState state;

    EXPECT_FALSE(state.isCoupled());
}