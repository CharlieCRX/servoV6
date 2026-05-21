#include <gtest/gtest.h>
#include "gantry/GantryCouplingState.h"

// ============================================================
// NotSynchronized 初始态测试
// ============================================================

// 1. 验证默认初始态为 NotSynchronized，而非预设 Decoupled 或 Coupled
TEST(GantryCouplingState, should_be_not_synchronized_by_default)
{
    GantryCouplingState state;
    
    EXPECT_EQ(state.status(), GantryCouplingState::Status::NotSynchronized);
    EXPECT_TRUE(state.isNotSynchronized());
    EXPECT_FALSE(state.isCoupled());
    EXPECT_FALSE(state.isCouplingRequested());
    EXPECT_FALSE(state.isDecouplingRequested());
}

// 2. NotSynchronized 下 requestCouple() 被拒绝，状态不变
TEST(GantryCouplingState, should_reject_request_couple_when_not_synchronized)
{
    GantryCouplingState state;
    
    state.requestCouple();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::NotSynchronized);
    EXPECT_TRUE(state.isNotSynchronized());
}

// 3. NotSynchronized 下 requestDecouple() 被拒绝，状态不变
TEST(GantryCouplingState, should_reject_request_decouple_when_not_synchronized)
{
    GantryCouplingState state;
    
    state.requestDecouple();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::NotSynchronized);
    EXPECT_TRUE(state.isNotSynchronized());
}

// 4. applyCoupledFeedback() 从 NotSynchronized 退出到 Coupled
TEST(GantryCouplingState, should_exit_not_synchronized_to_coupled_via_apply_coupled_feedback)
{
    GantryCouplingState state;
    
    state.applyCoupledFeedback();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Coupled);
    EXPECT_TRUE(state.isCoupled());
    EXPECT_FALSE(state.isNotSynchronized());
}

// 5. applyDecoupledFeedback() 从 NotSynchronized 退出到 Decoupled
TEST(GantryCouplingState, should_exit_not_synchronized_to_decoupled_via_apply_decoupled_feedback)
{
    GantryCouplingState state;
    
    state.applyDecoupledFeedback();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Decoupled);
    EXPECT_FALSE(state.isCoupled());
    EXPECT_FALSE(state.isNotSynchronized());
}

// ============================================================
// 原有意图控制测试（从 Decoupled 出发）
// ============================================================

// 6. 验证：从 Decoupled 发起耦合请求 -> 进入 CouplingRequested 态
TEST(GantryCouplingState, should_enter_coupling_requested_mode_when_request_couple)
{
    GantryCouplingState state;
    state.applyDecoupledFeedback(); // 先退出 NotSynchronized

    state.requestCouple();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::CouplingRequested);
    EXPECT_TRUE(state.isCouplingRequested());
    EXPECT_FALSE(state.isCoupled());
}

// 7. 验证：收到 PLC 成功反馈 -> 进入 Coupled 态
TEST(GantryCouplingState, should_enter_coupled_mode_when_plc_feedback_received)
{
    GantryCouplingState state;
    state.applyDecoupledFeedback();
    
    state.requestCouple();
    state.applyCoupledFeedback();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Coupled);
    EXPECT_FALSE(state.isCouplingRequested());
    EXPECT_TRUE(state.isCoupled());
}

// 8. 核心：发起解耦请求 -> 必须进入 DecouplingRequested 态，严禁直接变回 Decoupled
TEST(GantryCouplingState, should_enter_decoupling_requested_mode_when_request_decouple)
{
    GantryCouplingState state;
    state.applyCoupledFeedback(); // 快捷进入 Coupled
    
    state.requestDecouple();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::DecouplingRequested);
    EXPECT_TRUE(state.isDecouplingRequested());
    EXPECT_FALSE(state.isCoupled()); 
}

// 9. 核心：收到 PLC 断开反馈 -> 真正回到 Decoupled 态
TEST(GantryCouplingState, should_enter_decoupled_mode_when_plc_decoupled_feedback_received)
{
    GantryCouplingState state;
    state.applyCoupledFeedback();
    
    state.requestDecouple();
    state.applyDecoupledFeedback();

    EXPECT_EQ(state.status(), GantryCouplingState::Status::Decoupled);
    EXPECT_FALSE(state.isDecouplingRequested());
    EXPECT_FALSE(state.isCoupled());
}
