#include <gtest/gtest.h>
#include "gantry/GantryPowerController.h"

using Status = GantryPowerController::Status;

// ============================================================
// NotSynchronized 初始态
// ============================================================

TEST(GantryPowerController, should_be_not_synchronized_by_default)
{
    GantryPowerController motor;

    EXPECT_EQ(motor.status(), Status::NotSynchronized);
    EXPECT_TRUE(motor.isNotSynchronized());
    EXPECT_FALSE(motor.isSynchronized());
    EXPECT_FALSE(motor.isEnabled());
    EXPECT_FALSE(motor.hasPendingCommand());
}

TEST(GantryPowerController, should_reject_request_enable_when_not_synchronized)
{
    GantryPowerController motor;

    GantryRejection result = motor.requestEnable(true);

    EXPECT_EQ(result, GantryRejection::NotSynchronized);
    EXPECT_EQ(motor.status(), Status::NotSynchronized);
    EXPECT_FALSE(motor.hasPendingCommand());
}

TEST(GantryPowerController, should_reject_request_disable_when_not_synchronized)
{
    GantryPowerController motor;

    GantryRejection result = motor.requestEnable(false);

    EXPECT_EQ(result, GantryRejection::NotSynchronized);
    EXPECT_EQ(motor.status(), Status::NotSynchronized);
}

// ============================================================
// NotSynchronized -> Disabled（首次 feedback）
// ============================================================

TEST(GantryPowerController, should_transition_to_disabled_on_first_feedback_disable)
{
    GantryPowerController motor;

    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Disabled);
    EXPECT_TRUE(motor.isSynchronized());
    EXPECT_FALSE(motor.isNotSynchronized());
    EXPECT_FALSE(motor.isEnabled());
}

// ============================================================
// NotSynchronized -> Enabled（首次 feedback 即已使能）
// ============================================================

TEST(GantryPowerController, should_transition_to_enabled_on_first_feedback_enable)
{
    GantryPowerController motor;

    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Enabled);
    EXPECT_TRUE(motor.isEnabled());
    EXPECT_TRUE(motor.isSynchronized());
}

// ============================================================
// Disabled -> Enabling
// ============================================================

TEST(GantryPowerController, should_transition_to_enabling_when_request_enable_from_disabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    GantryRejection result = motor.requestEnable(true);

    EXPECT_EQ(result, GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Enabling);
    EXPECT_FALSE(motor.isEnabled());
    EXPECT_TRUE(motor.hasPendingCommand());
    EXPECT_EQ(motor.popPendingCommand().enable, true);
}

// ============================================================
// Enabling -> Enabled（PLC 确认使能）
// ============================================================

TEST(GantryPowerController, should_transition_to_enabled_when_feedback_confirm_enable)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);
    motor.popPendingCommand();

    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Enabled);
    EXPECT_TRUE(motor.isEnabled());
}

// ============================================================
// Enabling -> Disabled（PLC 拒绝/未确认，反馈仍为 disabled）
// ============================================================

TEST(GantryPowerController, should_transition_to_disabled_when_feedback_still_disabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);
    motor.popPendingCommand();

    // PLC 未确认使能，反馈仍为 false
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Disabled);
    EXPECT_FALSE(motor.isEnabled());
}

// ============================================================
// Enabled -> Disabling
// ============================================================

TEST(GantryPowerController, should_transition_to_disabling_when_request_disable_from_enabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    GantryRejection result = motor.requestEnable(false);

    EXPECT_EQ(result, GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Disabling);
    EXPECT_TRUE(motor.hasPendingCommand());
    EXPECT_EQ(motor.popPendingCommand().enable, false);
}

// ============================================================
// Disabling -> Disabled（PLC 确认掉电）
// ============================================================

TEST(GantryPowerController, should_transition_to_disabled_when_feedback_confirm_disable)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(false);
    motor.popPendingCommand();

    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Disabled);
    EXPECT_FALSE(motor.isEnabled());
}

// ============================================================
// Disabling -> Enabled（PLC 拒绝掉电，反馈仍为 enabled）
// ============================================================

TEST(GantryPowerController, should_transition_to_enabled_when_feedback_still_enabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(false);
    motor.popPendingCommand();

    // PLC 未确认掉电，反馈仍为 true
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Enabled);
    EXPECT_TRUE(motor.isEnabled());
}

// ============================================================
// 幂等
// ============================================================

TEST(GantryPowerController, should_be_idempotent_when_already_enabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    GantryRejection result = motor.requestEnable(true);

    EXPECT_EQ(result, GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Enabled);
    EXPECT_FALSE(motor.hasPendingCommand());
}

TEST(GantryPowerController, should_be_idempotent_when_already_disabled)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });

    GantryRejection result = motor.requestEnable(false);

    EXPECT_EQ(result, GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Disabled);
    EXPECT_FALSE(motor.hasPendingCommand());
}

// ============================================================
// StateConflict：中间态拒绝操作
// ============================================================

TEST(GantryPowerController, should_reject_request_enable_when_enabling)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);  // -> Enabling

    // Enabling 中再次请求使能
    GantryRejection result = motor.requestEnable(true);

    EXPECT_EQ(result, GantryRejection::StateConflict);
    EXPECT_EQ(motor.status(), Status::Enabling);  // 状态不变
}

TEST(GantryPowerController, should_reject_request_disable_when_disabling)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(false);  // -> Disabling

    GantryRejection result = motor.requestEnable(false);

    EXPECT_EQ(result, GantryRejection::StateConflict);
    EXPECT_EQ(motor.status(), Status::Disabling);
}

TEST(GantryPowerController, should_reject_request_enable_when_disabling)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(false);  // -> Disabling

    GantryRejection result = motor.requestEnable(true);

    EXPECT_EQ(result, GantryRejection::StateConflict);
}

TEST(GantryPowerController, should_reject_request_disable_when_enabling)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);  // -> Enabling

    GantryRejection result = motor.requestEnable(false);

    EXPECT_EQ(result, GantryRejection::StateConflict);
}

// ============================================================
// 中间态下 applyFeedback 可改变状态（PLC 反馈覆盖进程状态）
// ============================================================

TEST(GantryPowerController, should_update_status_on_feedback_even_when_in_enabling)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);  // Enabling，pending 未 pop

    // 直接 applyFeedback 覆盖状态（PLC 主动报告）
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });

    EXPECT_EQ(motor.status(), Status::Enabled);
}

// ============================================================
// popPendingCommand 行为
// ============================================================

TEST(GantryPowerController, should_clear_pending_after_pop)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    motor.requestEnable(true);

    EXPECT_TRUE(motor.hasPendingCommand());
    motor.popPendingCommand();
    EXPECT_FALSE(motor.hasPendingCommand());
}

// ============================================================
// 同步态粘滞
// ============================================================

TEST(GantryPowerController, should_stay_synchronized_after_multiple_feedbacks)
{
    GantryPowerController motor;

    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    EXPECT_TRUE(motor.isSynchronized());

    motor.applyFeedback({ .enable = false, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(motor.isSynchronized());
    EXPECT_EQ(motor.status(), Status::Disabled);
}

// ============================================================
// 全链路：false -> true -> false -> true
// ============================================================

TEST(GantryPowerController, full_enable_disable_cycle)
{
    GantryPowerController motor;
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    EXPECT_EQ(motor.status(), Status::Disabled);

    // Disabled -> Enabling
    EXPECT_EQ(motor.requestEnable(true), GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Enabling);
    EXPECT_TRUE(motor.hasPendingCommand());
    EXPECT_EQ(motor.popPendingCommand().enable, true);

    // Enabling -> Enabled
    motor.applyFeedback({ .enable = true, .isCoupled = false, .errorCode = 0 });
    EXPECT_EQ(motor.status(), Status::Enabled);

    // Enabled -> Disabling
    EXPECT_EQ(motor.requestEnable(false), GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Disabling);
    EXPECT_TRUE(motor.hasPendingCommand());
    EXPECT_EQ(motor.popPendingCommand().enable, false);

    // Disabling -> Disabled
    motor.applyFeedback({ .enable = false, .isCoupled = false, .errorCode = 0 });
    EXPECT_EQ(motor.status(), Status::Disabled);

    // Disabled -> Enabling（再次）
    EXPECT_EQ(motor.requestEnable(true), GantryRejection::None);
    EXPECT_EQ(motor.status(), Status::Enabling);
}
