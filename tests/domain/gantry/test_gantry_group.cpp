#include <gtest/gtest.h>
#include "gantry/GantryGroup.h"

// ============================================================
// NotSynchronized 前置拦截测试 (新增)
// ============================================================

// 1. 验证初始状态为 NotSynchronized
TEST(GantryGroup, should_be_not_synchronized_initially)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    EXPECT_TRUE(gantry.isNotSynchronized());
    EXPECT_FALSE(gantry.isCoupled());
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.hasError());
}

// 2. NotSynchronized 下 requestCouple(true) 被拒绝
TEST(GantryGroup, should_reject_couple_when_not_synchronized)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::NotSynchronized);
    EXPECT_TRUE(gantry.isNotSynchronized()); // 状态不变
    EXPECT_FALSE(gantry.hasPendingCommand()); // 无命令产生
}

// 3. NotSynchronized 下 requestCouple(false) 被拒绝
TEST(GantryGroup, should_reject_decouple_when_not_synchronized)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::NotSynchronized);
    EXPECT_TRUE(gantry.isNotSynchronized()); // 状态不变
    EXPECT_FALSE(gantry.hasPendingCommand()); // 无命令产生
}

// 4. applyFeedback 是唯一退出 NotSynchronized 的途径 — 进入 Coupled
TEST(GantryGroup, should_exit_not_synchronized_to_coupled_via_feedback)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });

    EXPECT_FALSE(gantry.isNotSynchronized());
    EXPECT_TRUE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());

    // 同步后，意图操作正常放行
    auto reason = gantry.requestCouple(false);
    EXPECT_EQ(reason, GantryRejection::None);
}

// 5. applyFeedback 是唯一退出 NotSynchronized 的途径 — 进入 Decoupled
TEST(GantryGroup, should_exit_not_synchronized_to_decoupled_via_feedback)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 });

    EXPECT_FALSE(gantry.isNotSynchronized());
    EXPECT_FALSE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());

    // 同步后，意图操作正常放行
    auto reason = gantry.requestCouple(true);
    EXPECT_EQ(reason, GantryRejection::None);
}

// ============================================================
// 原有意图控制测试 (已适配：先退出 NotSynchronized)
// ============================================================

// 6. 意图生成与弹出消费 (Produce & Pop Intent)
TEST(GantryGroup, should_produce_and_pop_coupling_intent_when_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling);
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 7. 非对称解耦的四态闭环 (DecouplingRequested)
TEST(GantryGroup, should_produce_decoupling_intent_and_enter_decoupling_requested_state)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    // 先同步到 Coupled
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.isCoupled());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_FALSE(cmd.enableCoupling); 
}

// 8. 验证领域安全防线：轴 Error 状态拒绝联动
TEST(GantryGroup, should_reject_coupling_intent_when_axis_is_in_error_state)
{
    Axis x1;
    Axis x2;
    x1.applyFeedback({ .state = AxisState::Error }); 
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::AxisStateError);
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 9. 验证统一的 GantryFeedback 成功路径处理
TEST(GantryGroup, should_update_state_based_on_unified_feedback_success)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 });

    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());
}

// 10. 验证统一的 GantryFeedback 错误码映射与状态重置
TEST(GantryGroup, should_update_state_and_record_error_based_on_unified_feedback)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    gantry.applyFeedback({ .isCoupled = false, .errorCode = 1 });

    EXPECT_TRUE(gantry.hasError());
    EXPECT_EQ(gantry.getLastError(), GantryRejection::PositionToleranceExceeded);
    EXPECT_FALSE(gantry.isCouplingRequested());
}

// ============================================================
// 幂等 & 冲突拦截测试 (已适配)
// ============================================================

// 11. 幂等：已联动时再次请求联动，不产生新命令
TEST(GantryGroup, should_not_produce_command_when_already_coupled)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCoupled());
}

// 12. 幂等：已解耦时再次请求解耦，不产生新命令
TEST(GantryGroup, should_not_produce_command_when_already_decoupled)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized 到 Decoupled
    EXPECT_FALSE(gantry.isCoupled());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 13. 幂等：联动请求进行中，再次请求联动不重复产生命令
TEST(GantryGroup, should_not_duplicate_command_when_coupling_already_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());
}

// 14. 幂等：解耦请求进行中，再次请求解耦不重复产生命令
TEST(GantryGroup, should_not_duplicate_command_when_decoupling_already_requested)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isDecouplingRequested());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
}

// 15. 冲突：联动请求进行中，不允许发起解耦
TEST(GantryGroup, should_reject_decouple_when_coupling_in_progress)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isCouplingRequested());
}

// 16. 冲突：解耦请求进行中，不允许发起联动
TEST(GantryGroup, should_reject_couple_when_decoupling_in_progress)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.isDecouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isDecouplingRequested());
}

// 17. 正交：幂等联动不消耗已存在的待发送命令
TEST(GantryGroup, idempotent_couple_does_not_clear_existing_pending_command)
{
    Axis x1;
    Axis x2;
    GantryGroup gantry(x1, x2);

    gantry.applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling);
}
