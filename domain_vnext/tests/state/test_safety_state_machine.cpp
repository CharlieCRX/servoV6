// ============================================================================
// test_safety_state_machine.cpp —— P2 state: 急停五态状态机
// ============================================================================
// 移植旧 EmergencyStopController 语义（启动同步 / 五态 / 锁存），实现重写于 domain_vnext。
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/state/SafetyStateMachine.h"

namespace domain_vnext::state {
namespace {

using model::SafetyState;

TEST(SafetyStateMachineTest, InitiallyNotSynchronized_AndLocked) {
    SafetyStateMachine sm;
    EXPECT_EQ(sm.state(), SafetyState::NotSynchronized);
    EXPECT_TRUE(sm.isNotSynchronized());
    EXPECT_TRUE(sm.isSystemLocked());
    EXPECT_FALSE(sm.isEmergencyStopped());
    EXPECT_FALSE(sm.isTransitioning());
    EXPECT_FALSE(sm.hasPendingCommand());
}

TEST(SafetyStateMachineTest, FirstFeedbackFalse_SyncsToRunning) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    EXPECT_EQ(sm.state(), SafetyState::Running);
    EXPECT_FALSE(sm.isNotSynchronized());
    EXPECT_FALSE(sm.isSystemLocked());
}

TEST(SafetyStateMachineTest, FirstFeedbackTrue_SyncsToEmergencyStopped) {
    SafetyStateMachine sm;
    sm.applyFeedback(true);
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(sm.isEmergencyStopped());
    EXPECT_TRUE(sm.isSystemLocked());
}

TEST(SafetyStateMachineTest, RejectIntents_WhenNotSynchronized) {
    SafetyStateMachine sm;
    EXPECT_EQ(sm.requestEmergencyStop(), SafetyRejection::NotSynchronized);
    EXPECT_EQ(sm.requestReleaseEmergencyStop(), SafetyRejection::NotSynchronized);
    EXPECT_FALSE(sm.hasPendingCommand());
    EXPECT_EQ(sm.state(), SafetyState::NotSynchronized);
}

TEST(SafetyStateMachineTest, TriggerEmergencyStop_FromRunning) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);  // -> Running

    EXPECT_EQ(sm.requestEmergencyStop(), SafetyRejection::None);
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopping);
    EXPECT_TRUE(sm.isSystemLocked());
    EXPECT_TRUE(sm.isTransitioning());

    ASSERT_TRUE(sm.hasPendingCommand());
    EXPECT_EQ(sm.popPendingCommand().active, true);
    EXPECT_FALSE(sm.hasPendingCommand());
}

TEST(SafetyStateMachineTest, ConfirmEmergencyStop_ViaFeedback) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    sm.requestEmergencyStop();
    sm.popPendingCommand();

    // 等待 PLC 确认急停完成
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopping);
    sm.applyFeedback(true);
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(sm.isEmergencyStopped());
    EXPECT_FALSE(sm.isTransitioning());
}

TEST(SafetyStateMachineTest, Idempotent_TriggerTwice_IsAlreadyInState) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    EXPECT_EQ(sm.requestEmergencyStop(), SafetyRejection::None);
    EXPECT_EQ(sm.requestEmergencyStop(), SafetyRejection::AlreadyInState);
}

TEST(SafetyStateMachineTest, Release_OnlyFromEmergencyStopped) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);  // Running
    EXPECT_EQ(sm.requestReleaseEmergencyStop(), SafetyRejection::NotEmergencyStopped);

    sm.requestEmergencyStop();
    sm.popPendingCommand();
    sm.applyFeedback(true);  // EmergencyStopped
    EXPECT_EQ(sm.requestReleaseEmergencyStop(), SafetyRejection::None);
    EXPECT_EQ(sm.state(), SafetyState::ReleasingEmergencyStop);
    EXPECT_TRUE(sm.isTransitioning());

    ASSERT_TRUE(sm.hasPendingCommand());
    EXPECT_EQ(sm.popPendingCommand().active, false);
}

TEST(SafetyStateMachineTest, ConfirmRelease_ViaFeedback) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    sm.requestEmergencyStop();
    sm.popPendingCommand();
    sm.applyFeedback(true);
    sm.requestReleaseEmergencyStop();
    sm.popPendingCommand();

    EXPECT_EQ(sm.state(), SafetyState::ReleasingEmergencyStop);
    sm.applyFeedback(false);
    EXPECT_EQ(sm.state(), SafetyState::Running);
    EXPECT_FALSE(sm.isSystemLocked());
}

TEST(SafetyStateMachineTest, ReleaseDuringRelease_IsInvalidTransition) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    sm.requestEmergencyStop();
    sm.popPendingCommand();
    sm.applyFeedback(true);
    sm.requestReleaseEmergencyStop();

    // 正在解除中再次触发急停 -> InvalidStateTransition
    EXPECT_EQ(sm.requestEmergencyStop(), SafetyRejection::InvalidStateTransition);
}

TEST(SafetyStateMachineTest, EmergencyStopped_IsLatched) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);
    sm.requestEmergencyStop();
    sm.popPendingCommand();
    sm.applyFeedback(true);   // EmergencyStopped

    // PLC 瞬态恢复 false 也不允许自动恢复
    sm.applyFeedback(false);
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(sm.isEmergencyStopped());
    EXPECT_TRUE(sm.isSystemLocked());
}

TEST(SafetyStateMachineTest, PhysicalEStop_RunningToStopped) {
    SafetyStateMachine sm;
    sm.applyFeedback(false);  // Running
    // 物理急停按钮 -> PLC 直接反馈 true（Controller 永远相信 PLC）
    sm.applyFeedback(true);
    EXPECT_EQ(sm.state(), SafetyState::EmergencyStopped);
}

TEST(SafetyStateMachineTest, IsSystemLocked_CoversAllLockedStates) {
    SafetyStateMachine sm;
    EXPECT_TRUE(sm.isSystemLocked());          // NotSynchronized
    sm.applyFeedback(false);
    EXPECT_FALSE(sm.isSystemLocked());         // Running
    sm.requestEmergencyStop();
    EXPECT_TRUE(sm.isSystemLocked());          // EmergencyStopping
    sm.applyFeedback(true);
    EXPECT_TRUE(sm.isSystemLocked());          // EmergencyStopped
    sm.requestReleaseEmergencyStop();
    EXPECT_TRUE(sm.isSystemLocked());          // ReleasingEmergencyStop
    sm.applyFeedback(false);
    EXPECT_FALSE(sm.isSystemLocked());         // Running
}

}  // namespace
}  // namespace domain_vnext::state
