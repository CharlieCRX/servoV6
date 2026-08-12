// ============================================================================
// test_axis_state_machine.cpp —— P2 state: 单轴意图校验状态机
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/state/AxisStateMachine.h"

namespace domain_vnext::state {
namespace {

using model::AxisCommand;
using model::AxisCommandKind;
using model::GantryCouplingState;

TEST(AxisStateMachineTest, AcceptSimpleCommand_WhenIdle) {
    AxisStateMachine sm;
    CommandOutbox outbox;
    AxisCommand enable{AxisCommandKind::EnableAxis, 0.f, true};

    auto r = sm.submit(enable, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_TRUE(outbox.hasAny());
    EXPECT_EQ(outbox.drain().size(), 1u);
}

TEST(AxisStateMachineTest, RejectMotion_WhenSystemLocked) {
    AxisStateMachine sm;
    sm.setSystemLocked(true);
    CommandOutbox outbox;

    auto r = sm.submit(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::RejectedSystemLocked);
    EXPECT_FALSE(outbox.hasAny());  // 拒绝的命令不入 Outbox
}

TEST(AxisStateMachineTest, RejectMotion_WhenGantryUnsync_AndRequiresSync) {
    AxisStateMachine sm(true);  // X1/X2/X 逻辑轴依赖龙门同步
    CommandOutbox outbox;
    // 龙门未同步（默认 Unconfigured）

    auto r = sm.submit(AxisCommand{AxisCommandKind::TriggerRelMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::RejectedGantryLocked);
    EXPECT_FALSE(outbox.hasAny());
}

TEST(AxisStateMachineTest, AllowMotion_AfterGantrySynced) {
    AxisStateMachine sm(true);
    sm.setGantryState(GantryCouplingState::Decoupled);
    CommandOutbox outbox;

    auto r = sm.submit(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_TRUE(outbox.hasAny());
}

TEST(AxisStateMachineTest, IndependentAxis_IgnoresGantrySync) {
    AxisStateMachine sm(false);  // Y/Z/R 独立轴
    CommandOutbox outbox;
    // 龙门未同步也不影响独立轴
    auto r = sm.submit(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::Accepted);
}

TEST(AxisStateMachineTest, RejectNewMotion_WhenBusy) {
    AxisStateMachine sm;
    CommandOutbox outbox;
    // 触发定位 -> busy
    sm.submit(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false}, outbox);
    EXPECT_TRUE(sm.isBusy());
    outbox.drain();

    // busy 中再触发 -> RejectedAxisBusy
    auto r = sm.submit(AxisCommand{AxisCommandKind::TriggerAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::RejectedAxisBusy);
    EXPECT_FALSE(outbox.hasAny());

    // StopAbsMove 放行并解除 busy
    auto stop = sm.submit(AxisCommand{AxisCommandKind::StopAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(stop, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_FALSE(sm.isBusy());
}

TEST(AxisStateMachineTest, JogOnOff_DrivesBusy) {
    AxisStateMachine sm;
    CommandOutbox outbox;

    // 点动 ON -> 忙
    auto on = sm.submit(AxisCommand{AxisCommandKind::JogForward, 0.f, true}, outbox);
    EXPECT_EQ(on, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_TRUE(sm.isBusy());
    outbox.drain();

    // 忙中再次点动 ON -> 拒绝
    auto again = sm.submit(AxisCommand{AxisCommandKind::JogForward, 0.f, true}, outbox);
    EXPECT_EQ(again, AxisStateMachine::SubmitResult::RejectedAxisBusy);

    // 点动 OFF -> 解除忙
    auto off = sm.submit(AxisCommand{AxisCommandKind::JogForward, 0.f, false}, outbox);
    EXPECT_EQ(off, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_FALSE(sm.isBusy());
}

TEST(AxisStateMachineTest, ParameterWrite_AllowedWhenLocked) {
    AxisStateMachine sm;
    sm.setSystemLocked(true);
    CommandOutbox outbox;

    // 普通参数写不受急停锁定约束（四接口解耦，保证写入失败不会阻塞后续触发）
    auto r = sm.submit(AxisCommand{AxisCommandKind::SetManualSpeed, 3.0f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_TRUE(outbox.hasAny());
}

TEST(AxisStateMachineTest, StopCommand_AllowedWhenLocked) {
    AxisStateMachine sm;
    sm.setSystemLocked(true);
    CommandOutbox outbox;

    auto r = sm.submit(AxisCommand{AxisCommandKind::StopAbsMove, 0.f, false}, outbox);
    EXPECT_EQ(r, AxisStateMachine::SubmitResult::Accepted);
    EXPECT_FALSE(sm.isBusy());
}

}  // namespace
}  // namespace domain_vnext::state
