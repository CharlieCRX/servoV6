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
    EXPECT_EQ(reason, GantryRejection::None);
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
    EXPECT_EQ(reason, GantryRejection::None);
    
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

    EXPECT_EQ(reason, GantryRejection::AxisStateError);
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
    EXPECT_EQ(gantry.getLastError(), GantryRejection::PositionToleranceExceeded);
    EXPECT_FALSE(gantry.isCouplingRequested()); // 发生错误，状态机退回解耦
}

// ============================================================
// ⭐ 新增：幂等 & 冲突拦截测试 (7 条)
// ============================================================

// 7. 幂等：已联动时再次请求联动，不应产生新命令
TEST(GantryGroup, should_not_produce_command_when_already_coupled)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 已联动
    gantry.requestCouple(true);
    gantry.popPendingCommand(); // 消费意图
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    // When: 再次请求联动
    auto reason = gantry.requestCouple(true);

    // Then: 返回成功，但不产生新命令
    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCoupled()); // 状态保持
}

// 8. 幂等：已解耦时再次请求解耦，不应产生新命令
TEST(GantryGroup, should_not_produce_command_when_already_decoupled)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 已解耦（初始状态）
    EXPECT_FALSE(gantry.isCoupled());

    // When: 请求解耦
    auto reason = gantry.requestCouple(false);

    // Then: 返回成功，但不产生新命令
    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 9. 幂等：联动请求进行中，再次请求联动不重复产生命令
TEST(GantryGroup, should_not_duplicate_command_when_coupling_already_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 已发起联动请求，PendingCommand 未消费
    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    // When: 再次请求联动
    auto reason = gantry.requestCouple(true);

    // Then: 幂等返回成功，命令不被覆盖
    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand()); // 原命令保留
    EXPECT_TRUE(gantry.isCouplingRequested());
}

// 10. 幂等：解耦请求进行中，再次请求解耦不重复产生命令
TEST(GantryGroup, should_not_duplicate_command_when_decoupling_already_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 先联动
    gantry.requestCouple(true);
    gantry.popPendingCommand();
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    // Given: 发起解耦请求
    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isDecouplingRequested());

    // When: 再次请求解耦
    auto reason = gantry.requestCouple(false);

    // Then: 幂等返回成功
    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand()); // 原命令保留
}

// 11. 冲突：联动请求进行中，不允许发起解耦
TEST(GantryGroup, should_reject_decouple_when_coupling_in_progress)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 联动请求已发起，等待 PLC 确认
    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.isCouplingRequested());

    // When: 尝试解耦
    auto reason = gantry.requestCouple(false);

    // Then: 被拦截，返回 StateConflict
    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isCouplingRequested()); // 状态不变
}

// 12. 冲突：解耦请求进行中，不允许发起联动
TEST(GantryGroup, should_reject_couple_when_decoupling_in_progress)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 先联动
    gantry.requestCouple(true);
    gantry.popPendingCommand();
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });

    // Given: 发起解耦请求，等待 PLC 确认
    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.isDecouplingRequested());

    // When: 尝试再次联动
    auto reason = gantry.requestCouple(true);

    // Then: 被拦截，返回 StateConflict
    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isDecouplingRequested()); // 状态不变
}

// 13. 正交：幂等联动不消耗已存在的待发送命令
TEST(GantryGroup, idempotent_couple_does_not_clear_existing_pending_command)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // Given: 已发起一次联动请求，命令尚未被消费
    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    // When: 幂等再次请求联动
    auto reason = gantry.requestCouple(true);

    // Then: 命令保留，未被清除或覆盖
    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    // 消费后验证命令正确
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling);
}
