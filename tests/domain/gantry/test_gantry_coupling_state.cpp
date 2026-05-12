#include <gtest/gtest.h>
#include "gantry/GantryCouplingState.h"

// 1. 验证初始默认状态
TEST(GantryCouplingState, should_be_decoupled_by_default)
{
    GantryCouplingState state;
    
    EXPECT_EQ(state.status(), GantryCouplingState::Status::Decoupled);
    EXPECT_FALSE(state.isCoupled());
    EXPECT_FALSE(state.isCouplingRequested());
    EXPECT_FALSE(state.isDecouplingRequested());
}

// 2. 验证：发起耦合请求 -> 进入 CouplingRequested 态
TEST(GantryCouplingState, should_enter_coupling_requested_mode_when_request_couple)
{
    GantryCouplingState state;
    
    state.requestCouple();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::CouplingRequested);
    EXPECT_TRUE(state.isCouplingRequested());
    EXPECT_FALSE(state.isCoupled()); // 仅为请求，物理尚未确立
}

// 3. 验证：收到 PLC 成功反馈 -> 进入 Coupled 态
TEST(GantryCouplingState, should_enter_coupled_mode_when_plc_feedback_received)
{
    GantryCouplingState state;
    
    state.requestCouple();
    state.applyCoupledFeedback(); // 模拟 PLC 物理联动确立

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Coupled);
    EXPECT_FALSE(state.isCouplingRequested());
    EXPECT_TRUE(state.isCoupled());
}

// ⭐ 4. 核心新增：发起解耦请求 -> 必须进入 DecouplingRequested 态，严禁直接变回 Decoupled
TEST(GantryCouplingState, should_enter_decoupling_requested_mode_when_request_decouple)
{
    GantryCouplingState state;
    
    // 前置：确立联动
    state.requestCouple();
    state.applyCoupledFeedback();

    // 动作：软件发起解耦意图
    state.requestDecouple();

    // 验证：必须卡在中间态，等待 PLC 物理断开
    EXPECT_EQ(state.status(), GantryCouplingState::Status::DecouplingRequested);
    EXPECT_TRUE(state.isDecouplingRequested());
    // 此时从业务逻辑看，已经不能算是安全的 Coupled 了
    EXPECT_FALSE(state.isCoupled()); 
}

// ⭐ 5. 核心新增：收到 PLC 断开反馈 -> 真正回到 Decoupled 态
TEST(GantryCouplingState, should_enter_decoupled_mode_when_plc_decoupled_feedback_received)
{
    GantryCouplingState state;
    
    state.requestCouple();
    state.applyCoupledFeedback();
    
    state.requestDecouple(); // 进入 DecouplingRequested
    
    // 动作：PLC 寄存器反馈联动状态为 OFF
    state.applyDecoupledFeedback();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Decoupled);
    EXPECT_FALSE(state.isDecouplingRequested());
    EXPECT_FALSE(state.isCoupled());
}