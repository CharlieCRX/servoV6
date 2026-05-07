/**
 * @file test_command_result.cpp
 * @brief CommandResult 结构体单元测试 (T1.3)
 *
 * 测试用例源自: domain/docs/下一阶段TDD开发任务.md
 *
 * 测试编号:
 *   TS1.3.1 - accept() 返回 Allowed
 *   TS1.3.2 - reject() 返回非 Allowed
 */

#include <gtest/gtest.h>
#include "domain/value/CommandResult.h"

using namespace GantryValue;

// ─────────────────────────────────────────
// TS1.3.1: accept() 返回 Allowed
// ─────────────────────────────────────────
TEST(CommandResultTest, AcceptReturnsAllowed) {
    CommandResult result = CommandResult::accept();

    EXPECT_TRUE(result.isAccepted());
    EXPECT_EQ(result.verdict, Operability::Allowed);
    EXPECT_FALSE(result.detail.empty());
}

// ─────────────────────────────────────────
// TS1.3.2: reject() 返回非 Allowed
// ─────────────────────────────────────────
TEST(CommandResultTest, RejectReturnsNotAllowed) {
    CommandResult alarmResult = CommandResult::reject(
        Operability::AlarmActive, "X1 is in alarm state");

    EXPECT_FALSE(alarmResult.isAccepted());
    EXPECT_EQ(alarmResult.verdict, Operability::AlarmActive);
    EXPECT_EQ(alarmResult.detail, "X1 is in alarm state");

    // 测试所有拒绝类型都返回 isAccepted() == false
    CommandResult modeResult = CommandResult::reject(
        Operability::TargetNotOperableInMode, "Coupled mode: target X1 not operable");
    EXPECT_FALSE(modeResult.isAccepted());
    EXPECT_EQ(modeResult.verdict, Operability::TargetNotOperableInMode);

    CommandResult limitResult = CommandResult::reject(
        Operability::LimitTriggered, "X1 positive limit triggered");
    EXPECT_FALSE(limitResult.isAccepted());
    EXPECT_EQ(limitResult.verdict, Operability::LimitTriggered);

    CommandResult dirLimitResult = CommandResult::reject(
        Operability::LimitBlocksDirection, "X1 forward blocked by positive limit");
    EXPECT_FALSE(dirLimitResult.isAccepted());
    EXPECT_EQ(dirLimitResult.verdict, Operability::LimitBlocksDirection);

    CommandResult notEnabledResult = CommandResult::reject(
        Operability::NotEnabled, "X2 is not enabled");
    EXPECT_FALSE(notEnabledResult.isAccepted());
    EXPECT_EQ(notEnabledResult.verdict, Operability::NotEnabled);

    CommandResult busyResult = CommandResult::reject(
        Operability::CommandSlotBusy, "Command slot is busy");
    EXPECT_FALSE(busyResult.isAccepted());
    EXPECT_EQ(busyResult.verdict, Operability::CommandSlotBusy);

    CommandResult notIdleResult = CommandResult::reject(
        Operability::NotIdle, "Axis is not idle");
    EXPECT_FALSE(notIdleResult.isAccepted());
    EXPECT_EQ(notIdleResult.verdict, Operability::NotIdle);

    CommandResult deviationResult = CommandResult::reject(
        Operability::DeviationExceeded, "Synchronization deviation exceeded");
    EXPECT_FALSE(deviationResult.isAccepted());
    EXPECT_EQ(deviationResult.verdict, Operability::DeviationExceeded);
}
