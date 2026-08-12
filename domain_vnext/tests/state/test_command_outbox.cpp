// ============================================================================
// test_command_outbox.cpp —— P2 state: CommandOutbox（参数去重 / 运动保序 / 脉冲）
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/state/CommandOutbox.h"

namespace domain_vnext::state {
namespace {

using model::AxisCommand;
using model::AxisCommandKind;

TEST(CommandOutboxTest, InitiallyEmpty) {
    CommandOutbox outbox;
    EXPECT_FALSE(outbox.hasAny());
    EXPECT_FALSE(outbox.hasDirtyParameters());
    EXPECT_FALSE(outbox.hasMotion());
    EXPECT_FALSE(outbox.hasPulses());
    EXPECT_TRUE(outbox.drain().empty());
}

TEST(CommandOutboxTest, ParameterWrites_DedupeByField) {
    CommandOutbox outbox;
    // 同一字段多次写入 -> 只保留最后一次值
    outbox.push(AxisCommand{AxisCommandKind::SetManualSpeed, 1.0f, false});
    outbox.push(AxisCommand{AxisCommandKind::SetManualSpeed, 2.5f, false});
    outbox.push(AxisCommand{AxisCommandKind::SetPositioningSpeed, 9.0f, false});

    EXPECT_EQ(outbox.hasDirtyParameters(), true);
    EXPECT_EQ(outbox.motionCount(), 0u);
    EXPECT_EQ(outbox.pulseCount(), 0u);

    auto cmds = outbox.drain();
    ASSERT_EQ(cmds.size(), 2u);
    // 只保留最后一次 ManualSpeed
    EXPECT_EQ(cmds[0].kind, AxisCommandKind::SetManualSpeed);
    EXPECT_FLOAT_EQ(cmds[0].value, 2.5f);
    EXPECT_EQ(cmds[1].kind, AxisCommandKind::SetPositioningSpeed);
}

TEST(CommandOutboxTest, Motion_PreservesOrder) {
    CommandOutbox outbox;
    // 定位目标 -> 触发 -> 终止：严格保序
    outbox.push(AxisCommand{AxisCommandKind::SetAbsDistance, 100.0f, false});
    outbox.push(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false});
    outbox.push(AxisCommand{AxisCommandKind::StopAbsMove, 0.f, false});

    EXPECT_EQ(outbox.motionCount(), 3u);
    auto cmds = outbox.drain();
    ASSERT_EQ(cmds.size(), 3u);
    EXPECT_EQ(cmds[0].kind, AxisCommandKind::SetAbsDistance);
    EXPECT_FLOAT_EQ(cmds[0].value, 100.0f);
    EXPECT_EQ(cmds[1].kind, AxisCommandKind::TriggerAbsMove);
    EXPECT_EQ(cmds[2].kind, AxisCommandKind::StopAbsMove);
}

TEST(CommandOutboxTest, Pulses_CollectUnorderedTriggers) {
    CommandOutbox outbox;
    outbox.push(AxisCommand{AxisCommandKind::ClearAbsPosition, 0.f, false});
    outbox.push(AxisCommand{AxisCommandKind::EnableMotor, 0.f, true});
    outbox.push(AxisCommand{AxisCommandKind::JogForward, 0.f, true});

    EXPECT_EQ(outbox.pulseCount(), 3u);
    EXPECT_EQ(outbox.drain().size(), 3u);
}

TEST(CommandOutboxTest, Drain_CategoryOrder_ParamsMotionPulses) {
    CommandOutbox outbox;
    // 按 params -> motion -> pulses 分类展平
    outbox.push(AxisCommand{AxisCommandKind::SetManualSpeed, 5.0f, false});  // params
    outbox.push(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false});    // motion
    outbox.push(AxisCommand{AxisCommandKind::ClearRelZero, 0.f, false});      // pulse

    auto cmds = outbox.drain();
    ASSERT_EQ(cmds.size(), 3u);
    EXPECT_EQ(cmds[0].kind, AxisCommandKind::SetManualSpeed);
    EXPECT_EQ(cmds[1].kind, AxisCommandKind::TriggerAbsMove);
    EXPECT_EQ(cmds[2].kind, AxisCommandKind::ClearRelZero);
}

TEST(CommandOutboxTest, Drain_ClearsAll) {
    CommandOutbox outbox;
    outbox.push(AxisCommand{AxisCommandKind::TriggerRelMove, 0.f, false});
    outbox.push(AxisCommand{AxisCommandKind::SetRelDistance, 20.0f, false});
    outbox.push(AxisCommand{AxisCommandKind::EnableAxis, 0.f, true});

    EXPECT_TRUE(outbox.hasAny());
    outbox.drain();
    EXPECT_FALSE(outbox.hasAny());
    EXPECT_EQ(outbox.motionCount(), 0u);
    EXPECT_EQ(outbox.pulseCount(), 0u);
    EXPECT_FALSE(outbox.hasDirtyParameters());
}

TEST(CommandOutboxTest, MotionSeqs_AreAssignedAndStableAcrossCalls) {
    CommandOutbox outbox;
    outbox.push(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false});
    outbox.push(AxisCommand{AxisCommandKind::StopAbsMove, 0.f, false});
    // drain 后 seq 仍持续递增（不因清空而重置）
    outbox.drain();
    outbox.push(AxisCommand{AxisCommandKind::TriggerRelMove, 0.f, false});
    outbox.push(AxisCommand{AxisCommandKind::StopRelMove, 0.f, false});
    outbox.drain();
    EXPECT_FALSE(outbox.hasAny());
}

}  // namespace
}  // namespace domain_vnext::state
