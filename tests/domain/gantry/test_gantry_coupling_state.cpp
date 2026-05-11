#include <gtest/gtest.h>

#include "gantry/GantryCouplingState.h"

TEST(GantryCouplingState, should_be_decoupled_by_default)
{
    GantryCouplingState state;
    EXPECT_FALSE(state.isCoupled());
    EXPECT_FALSE(state.isCouplingRequested());
}


TEST(GantryCouplingState, should_enter_requested_mode_when_request_couple)
{
    GantryCouplingState state;
    
    state.requestCouple();

    EXPECT_TRUE(state.isCouplingRequested());
    EXPECT_FALSE(state.isCoupled()); // 请求了但不代表真正联动
}


// ⭐ 修改：测试真正联动建立
TEST(GantryCouplingState, should_enter_coupled_mode_when_marked_coupled)
{
    GantryCouplingState state;
    
    state.requestCouple();
    state.applyCoupledFeedback(); // 模拟 PLC 返回联动成功

    EXPECT_FALSE(state.isCouplingRequested());
    EXPECT_TRUE(state.isCoupled());
}

TEST(GantryCouplingState, should_exit_coupled_mode)
{
    GantryCouplingState state;
    
    state.requestCouple();
    state.applyCoupledFeedback();
    state.applyDecoupledFeedback();

    EXPECT_FALSE(state.isCoupled());
    EXPECT_FALSE(state.isCouplingRequested());
}