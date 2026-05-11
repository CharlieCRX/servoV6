#include <gtest/gtest.h>

#include "gantry/GantryCouplingState.h"

TEST(GantryCouplingState, should_be_decoupled_by_default)
{
    GantryCouplingState state;

    EXPECT_FALSE(state.isCoupled());
}

TEST(GantryCouplingState, should_enter_coupled_mode)
{
    GantryCouplingState state;

    state.couple();

    EXPECT_TRUE(state.isCoupled());
}

TEST(GantryCouplingState, should_exit_coupled_mode)
{
    GantryCouplingState state;

    state.couple();

    state.decouple();

    EXPECT_FALSE(state.isCoupled());
}