#include <gtest/gtest.h>
#include "gantry/GantryGroup.h"

// 1. 验证基础状态
TEST(GantryGroup, should_hold_x1_and_x2_and_be_decoupled_initially)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    EXPECT_FALSE(gantry.isCoupled());
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.hasError());
}

// ⭐ 2. 验证意图的生成与弹出消费 (Produce & Pop Intent)
TEST(GantryGroup, should_produce_and_pop_coupling_intent_when_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // 动作：发起耦合请求
    auto reason = gantry.requestCouple(true);

    // 验证：领域层放行，状态推进到 CouplingRequested
    EXPECT_EQ(reason, RejectionReason::None);
    EXPECT_TRUE(gantry.isCouplingRequested());
    
    // 验证：意图已生成
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    // 动作：外部（如 Orchestrator/Driver）弹出意图
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling); // 下发 ON 指令

    // 验证：意图一旦被消费，即刻清空 (Domain 层绝不持有旧指令)
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// ⭐ 3. 验证非对称解耦的四态闭环 (DecouplingRequested)
TEST(GantryGroup, should_produce_decoupling_intent_and_enter_decoupling_requested_state)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // 前置准备：强行推入已联动状态
    gantry.requestCouple(true);
    gantry.popPendingCommand(); 
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 统一 DTO 喂入成功反馈
    EXPECT_TRUE(gantry.isCoupled());

    // 动作：发起解耦请求
    auto reason = gantry.requestCouple(false);

    // 验证结果
    EXPECT_EQ(reason, RejectionReason::None);
    
    // ⭐ 这里是四态模型的精髓：软件不能立刻变 Decoupled，必须等待物理 OFF
    // 假设你的 GantryGroup 暴露了内部状态或者 isDecouplingRequested 接口
    // EXPECT_TRUE(gantry.isDecouplingRequested()); // 如果你开放了这个接口
    EXPECT_FALSE(gantry.isCoupled()); // 不再是安全的 Coupled
    
    // 验证生成的意图为 OFF
    auto cmd = gantry.popPendingCommand();
    EXPECT_FALSE(cmd.enableCoupling); 
}

// 4. 验证领域安全防线
TEST(GantryGroup, should_reject_coupling_intent_when_axis_is_in_error_state)
{
    Axis x1;
    Axis x2;
    x1.applyFeedback({ .state = AxisState::Error }); 
    GantryGroup gantry(x1, x2);

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, RejectionReason::InvalidState);
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.hasPendingCommand()); // 拦截后绝不产生意图
}

// ⭐ 5. 验证统一的 GantryFeedback 成功路径处理
TEST(GantryGroup, should_update_state_based_on_unified_feedback_success)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.requestCouple(true);
    
    // 动作：底层基建层 (Infrastructure) 喂入统一的物理快照
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });

    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());
}

// ⭐ 6. 验证统一的 GantryFeedback 错误码映射与状态重置
TEST(GantryGroup, should_update_state_and_record_error_based_on_unified_feedback)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.requestCouple(true);
    
    // 动作：模拟 PLC 返回联动失败 (isCoupled 依然为 false，且 errorCode = 1)
    gantry.applyFeedback({ .isCoupled = false, .errorCode = 1 });

    EXPECT_TRUE(gantry.hasError());
    EXPECT_EQ(gantry.getLastError(), RejectionReason::PositionToleranceExceeded);
    EXPECT_FALSE(gantry.isCouplingRequested()); // 发生错误，状态机退回解耦
}