#include <gtest/gtest.h>
#include "gantry/GantryCouplingController.h"

// ============================================================
// NotSynchronized 前置拦截测试
// ============================================================

// 1. 验证初始状态为 NotSynchronized
TEST(GantryCouplingController, should_be_not_synchronized_initially)
{
    GantryCouplingController gantry;

    EXPECT_TRUE(gantry.isNotSynchronized());
    EXPECT_FALSE(gantry.isCoupled());
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.hasError());
}

// 2. NotSynchronized 下 requestCouple(true) 被拒绝
TEST(GantryCouplingController, should_reject_couple_when_not_synchronized)
{
    GantryCouplingController gantry;

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::NotSynchronized);
    EXPECT_TRUE(gantry.isNotSynchronized()); // 状态不变
    EXPECT_FALSE(gantry.hasPendingCommand()); // 无命令产生
}

// 3. NotSynchronized 下 requestCouple(false) 被拒绝
TEST(GantryCouplingController, should_reject_decouple_when_not_synchronized)
{
    GantryCouplingController gantry;

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::NotSynchronized);
    EXPECT_TRUE(gantry.isNotSynchronized()); // 状态不变
    EXPECT_FALSE(gantry.hasPendingCommand()); // 无命令产生
}

// 4. applyFeedback 是唯一退出 NotSynchronized 的途径 -- 进入 Coupled
TEST(GantryCouplingController, should_exit_not_synchronized_to_coupled_via_feedback)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });

    EXPECT_FALSE(gantry.isNotSynchronized());
    EXPECT_TRUE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());

    // 同步后，意图操作正常放行
    auto reason = gantry.requestCouple(false);
    EXPECT_EQ(reason, GantryRejection::None);
}

// 5. applyFeedback 是唯一退出 NotSynchronized 的途径 -- 进入 Decoupled
TEST(GantryCouplingController, should_exit_not_synchronized_to_decoupled_via_feedback)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

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
TEST(GantryCouplingController, should_produce_and_pop_coupling_intent_when_requested)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling);
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 7. 非对称解耦的四态闭环 (DecouplingRequested)
TEST(GantryCouplingController, should_produce_decoupling_intent_and_enter_decoupling_requested_state)
{
    GantryCouplingController gantry;

    // 先同步到 Coupled
    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.isCoupled());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_FALSE(cmd.enableCoupling); 
}

// 8. PLC 负责安全校验：上位机不再因 Axis Error 状态拦截联动请求
//    （X1/X2 是否使能/静止/超差由 PLC 通过 Gantry_Error_Code 反馈）
TEST(GantryCouplingController, should_not_reject_coupling_when_physical_axes_may_be_in_error)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    // 即使物理轴可能在 Error 状态，上位机不拦截，允许下发联动命令
    // PLC 会通过 Gantry_Error_Code 反馈实际结果
    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.hasPendingCommand());
}

// 9. 验证统一的 GantryFeedback 成功路径处理
TEST(GantryCouplingController, should_update_state_based_on_unified_feedback_success)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });

    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_TRUE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());
}

// 10. 验证统一的 GantryFeedback 错误码映射与状态重置
//     PLC 拒绝联动 -> 回退到 Decoupled（未联动状态）
TEST(GantryCouplingController, should_update_state_and_record_error_based_on_unified_feedback)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 1 });

    EXPECT_TRUE(gantry.hasError());
    EXPECT_EQ(gantry.getLastError(), GantryRejection::PositionToleranceExceeded);
    EXPECT_FALSE(gantry.isCouplingRequested());
    EXPECT_FALSE(gantry.isCoupled()); // PLC 拒绝联动 -> 回退到未联动状态
}

// 10b. 解耦请求中收到 errorCode != 0 时，不应回退到 Coupled 状态
//      因为 PLC 解耦操作不会返回错误码（始终为 None），
//      解耦中间态仅根据 isCoupled 标志决定是否完成
TEST(GantryCouplingController, should_not_revert_to_coupled_on_error_during_decoupling)
{
    GantryCouplingController gantry;

    // 先同步到 Coupled
    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    // 发起解耦
    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.isDecouplingRequested());

    // 解耦过程中收到带错误码的反馈（模拟异常场景）
    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 2 });

    // 错误码被记录
    EXPECT_TRUE(gantry.hasError());
    EXPECT_EQ(gantry.getLastError(), GantryRejection::X1NotEnabled);

    // 但不应回退到 Coupled（解耦不依赖 errorCode 做状态回退）
    EXPECT_TRUE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.isCoupled());
}

// 10c. 解耦请求中 PLC 反馈 isCoupled=false 正常完成解耦
TEST(GantryCouplingController, should_complete_decoupling_when_feedback_is_uncoupled)
{
    GantryCouplingController gantry;

    // 先同步到 Coupled
    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantry.isCoupled());

    // 发起解耦
    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.isDecouplingRequested());

    // PLC 反馈解耦成功
    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    EXPECT_FALSE(gantry.isDecouplingRequested());
    EXPECT_FALSE(gantry.isCoupled());
    EXPECT_FALSE(gantry.hasError());
}

// ============================================================
// 幂等 & 冲突拦截测试 (已适配)
// ============================================================

// 11. 幂等：已联动时再次请求联动，不产生新命令
TEST(GantryCouplingController, should_not_produce_command_when_already_coupled)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCoupled());
}

// 12. 幂等：已解耦时再次请求解耦，不产生新命令
TEST(GantryCouplingController, should_not_produce_command_when_already_decoupled)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized 到 Decoupled
    EXPECT_FALSE(gantry.isCoupled());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_FALSE(gantry.hasPendingCommand());
}

// 13. 幂等：联动请求进行中，再次请求联动不重复产生命令
TEST(GantryCouplingController, should_not_duplicate_command_when_coupling_already_requested)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());
}

// 14. 幂等：解耦请求进行中，再次请求解耦不重复产生命令
TEST(GantryCouplingController, should_not_duplicate_command_when_decoupling_already_requested)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isDecouplingRequested());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
}

// 15. 冲突：联动请求进行中，不允许发起解耦
TEST(GantryCouplingController, should_reject_decouple_when_coupling_in_progress)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(false);

    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isCouplingRequested());
}

// 16. 冲突：解耦请求进行中，不允许发起联动
TEST(GantryCouplingController, should_reject_couple_when_decoupling_in_progress)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 }); // 快捷进入 Coupled
    EXPECT_TRUE(gantry.isCoupled());

    gantry.requestCouple(false);
    EXPECT_TRUE(gantry.isDecouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::StateConflict);
    EXPECT_TRUE(gantry.isDecouplingRequested());
}

// 17. 正交：幂等联动不消耗已存在的待发送命令
TEST(GantryCouplingController, idempotent_couple_does_not_clear_existing_pending_command)
{
    GantryCouplingController gantry;

    gantry.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized

    gantry.requestCouple(true);
    EXPECT_TRUE(gantry.hasPendingCommand());
    EXPECT_TRUE(gantry.isCouplingRequested());

    auto reason = gantry.requestCouple(true);

    EXPECT_EQ(reason, GantryRejection::None);
    EXPECT_TRUE(gantry.hasPendingCommand());
    
    auto cmd = gantry.popPendingCommand();
    EXPECT_TRUE(cmd.enableCoupling);
}
