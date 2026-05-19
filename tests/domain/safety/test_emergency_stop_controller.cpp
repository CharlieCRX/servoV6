#include <gtest/gtest.h>
#include "safety/EmergencyStopController.h"
#include "safety/SafetyState.h"
#include "safety/SafetyRejection.h"
#include "command/SystemCommand.h"

// ============================================================
// Startup Synchronization -- 首次 applyFeedback() 即同步
// ============================================================

// 1. 初始状态 = NotSynchronized（绝不假设系统安全）
TEST(EmergencyStopController, should_be_not_synchronized_initially)
{
    EmergencyStopController controller;

    EXPECT_EQ(controller.state(), SafetyState::NotSynchronized);
    EXPECT_TRUE(controller.isNotSynchronized());
    EXPECT_TRUE(controller.isSystemLocked());   // 真相未知，禁止一切运动
    EXPECT_FALSE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());
    EXPECT_FALSE(controller.hasPendingCommand());
}

// 2. NotSynchronized + applyFeedback(false) -> Running（首次同步，PLC 未急停）
TEST(EmergencyStopController, should_synchronize_to_running_via_first_feedback)
{
    EmergencyStopController controller;
    EXPECT_EQ(controller.state(), SafetyState::NotSynchronized);

    // 唯一的 PLC 反馈入口：首次 feedback 即完成同步
    controller.applyFeedback(false);

    EXPECT_EQ(controller.state(), SafetyState::Running);
    EXPECT_FALSE(controller.isNotSynchronized());
    EXPECT_FALSE(controller.isSystemLocked());
}

// 3. NotSynchronized + applyFeedback(true) -> EmergencyStopped（首次同步，PLC 已急停）
TEST(EmergencyStopController, should_synchronize_to_emergency_stopped_via_first_feedback)
{
    EmergencyStopController controller;
    EXPECT_EQ(controller.state(), SafetyState::NotSynchronized);

    // 唯一的 PLC 反馈入口：首次 feedback 即完成同步
    controller.applyFeedback(true);

    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);
    EXPECT_FALSE(controller.isNotSynchronized());
    EXPECT_TRUE(controller.isEmergencyStopped());
    EXPECT_TRUE(controller.isSystemLocked());
}

// 4. NotSynchronized 时 requestEmergencyStop() -> NotSynchronized 拒绝
TEST(EmergencyStopController, should_reject_emergency_stop_when_not_synchronized)
{
    EmergencyStopController controller;

    auto rejection = controller.requestEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::NotSynchronized);
    EXPECT_EQ(controller.state(), SafetyState::NotSynchronized);  // 状态不变
    EXPECT_FALSE(controller.hasPendingCommand());
}

// 5. NotSynchronized 时 requestReleaseEmergencyStop() -> NotSynchronized 拒绝
TEST(EmergencyStopController, should_reject_release_when_not_synchronized)
{
    EmergencyStopController controller;

    auto rejection = controller.requestReleaseEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::NotSynchronized);
    EXPECT_EQ(controller.state(), SafetyState::NotSynchronized);  // 状态不变
    EXPECT_FALSE(controller.hasPendingCommand());
}

// ============================================================
// 急停触发测试（从 Running 开始）
// ============================================================

// 6. Running -> 触发急停 -> EmergencyStopping + 生成 command{true}
TEST(EmergencyStopController, should_request_emergency_stop_from_running)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);  // 首次同步
    EXPECT_EQ(controller.state(), SafetyState::Running);

    auto rejection = controller.requestEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::None);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);
    EXPECT_TRUE(controller.isSystemLocked());
    EXPECT_TRUE(controller.isTransitioning());
    EXPECT_TRUE(controller.hasPendingCommand());

    auto cmd = controller.popPendingCommand();
    EXPECT_TRUE(cmd.active);
    EXPECT_FALSE(controller.hasPendingCommand());
}

// 7. EmergencyStopping + PLC 反馈 true -> EmergencyStopped
TEST(EmergencyStopController, should_transition_to_emergency_stopped_when_plc_confirms)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);
    controller.requestEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);

    controller.applyFeedback(true); // PLC 确认停机

    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(controller.isSystemLocked());
    EXPECT_TRUE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());
}

// 8. EmergencyStopping + PLC 反馈 false -> 保持 EmergencyStopping（等待）
TEST(EmergencyStopController, should_remain_emergency_stopping_when_plc_not_yet_confirmed)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);
    controller.requestEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);

    controller.applyFeedback(false); // PLC 尚未完成停机

    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);
    EXPECT_TRUE(controller.isTransitioning());
}

// 9. Running -> 完整急停链路 (request + feedback)
TEST(EmergencyStopController, should_complete_full_emergency_stop_flow)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // Step 1: 请求急停
    auto rejection = controller.requestEmergencyStop();
    EXPECT_EQ(rejection, SafetyRejection::None);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);
    EXPECT_TRUE(controller.hasPendingCommand());

    // Step 2: 消费命令
    auto cmd = controller.popPendingCommand();
    EXPECT_TRUE(cmd.active);

    // Step 3: PLC 反馈确认
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(controller.isEmergencyStopped());
}

// ============================================================
// 物理急停按钮测试（PLC Feedback 驱动，非 Controller 命令）
// ============================================================

// 10. 物理急停按钮按下：Running + feedback(true) -> 直接跃迁到 EmergencyStopped
//      不经过 EmergencyStopping，因为没有通过 Controller 发出命令
TEST(EmergencyStopController, should_transition_directly_to_emergency_stopped_when_physical_button_pressed)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);
    EXPECT_EQ(controller.state(), SafetyState::Running);

    // 物理急停按钮被按下，PLC 反馈"设备急停中 = TRUE"
    controller.applyFeedback(true);

    // 直接跃迁到 EmergencyStopped，不经过 EmergencyStopping
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(controller.isEmergencyStopped());
    EXPECT_TRUE(controller.isSystemLocked());
    EXPECT_FALSE(controller.isTransitioning());  // 不是过渡态，已经直接锁定

    // 没有 pending command（因为命令不由 Controller 发出）
    EXPECT_FALSE(controller.hasPendingCommand());
}

// 11. 物理急停 -> 软件解除：EmergencyStopped(物理) -> requestRelease -> ReleasingEmergencyStop -> feedback(false) -> Running
TEST(EmergencyStopController, should_release_physical_emergency_stop_via_software)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // Step 1: 物理急停按钮按下
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // Step 2: 软件请求解除急停
    auto rejection = controller.requestReleaseEmergencyStop();
    EXPECT_EQ(rejection, SafetyRejection::None);
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);
    EXPECT_TRUE(controller.hasPendingCommand());

    auto cmd = controller.popPendingCommand();
    EXPECT_FALSE(cmd.active);

    // Step 3: PLC 反馈解除
    controller.applyFeedback(false);
    EXPECT_EQ(controller.state(), SafetyState::Running);
    EXPECT_FALSE(controller.isSystemLocked());
}

// ============================================================
// 解除急停测试
// ============================================================

// 12. EmergencyStopped -> 解除急停 -> ReleasingEmergencyStop + 生成 command{false}
TEST(EmergencyStopController, should_request_release_from_emergency_stopped)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // 先进入 EmergencyStopped
    controller.requestEmergencyStop();
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // 请求解除
    auto rejection = controller.requestReleaseEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::None);
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);
    EXPECT_TRUE(controller.isSystemLocked());
    EXPECT_TRUE(controller.isTransitioning());
    EXPECT_TRUE(controller.hasPendingCommand());

    auto cmd = controller.popPendingCommand();
    EXPECT_FALSE(cmd.active);
    EXPECT_FALSE(controller.hasPendingCommand());
}

// 13. ReleasingEmergencyStop + PLC 反馈 false -> Running（直接恢复）
TEST(EmergencyStopController, should_transition_to_running_when_plc_confirms_release)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // 先进入 EmergencyStopped
    controller.requestEmergencyStop();
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // 请求解除
    controller.requestReleaseEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);

    // PLC 反馈解除
    controller.applyFeedback(false);

    EXPECT_EQ(controller.state(), SafetyState::Running);
    EXPECT_FALSE(controller.isSystemLocked());
    EXPECT_FALSE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());
}

// 14. ReleasingEmergencyStop + PLC 反馈 true -> 保持 ReleasingEmergencyStop（等待）
TEST(EmergencyStopController, should_remain_releasing_emergency_stop_when_plc_not_yet_released)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // 先进入 EmergencyStopped
    controller.requestEmergencyStop();
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // 请求解除
    controller.requestReleaseEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);

    // PLC 尚未反馈解除
    controller.applyFeedback(true);

    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);
    EXPECT_TRUE(controller.isTransitioning());
}

// ============================================================
// 幂等 & 冲突拦截测试
// ============================================================

// 15. 幂等：EmergencyStopping 下再次请求急停 -> AlreadyInState
TEST(EmergencyStopController, should_reject_duplicate_emergency_stop_when_stopping)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    controller.requestEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);

    auto rejection = controller.requestEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::AlreadyInState);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping); // 状态不变
}

// 16. 幂等：EmergencyStopped 下再次请求急停 -> AlreadyInState
TEST(EmergencyStopController, should_reject_duplicate_emergency_stop_when_already_stopped)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    controller.requestEmergencyStop();
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    auto rejection = controller.requestEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::AlreadyInState);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped); // 状态不变
}

// 17. 冲突：ReleasingEmergencyStop 下请求急停 -> InvalidStateTransition
TEST(EmergencyStopController, should_reject_emergency_stop_when_releasing)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    // 先进入 EmergencyStopped -> ReleasingEmergencyStop
    controller.requestEmergencyStop();
    controller.applyFeedback(true);
    controller.requestReleaseEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);

    // 尝试反向操作
    auto rejection = controller.requestEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::InvalidStateTransition);
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop); // 状态不变
}

// 18. 前置条件：Running 下解除急停 -> NotEmergencyStopped
TEST(EmergencyStopController, should_reject_release_when_running)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);
    EXPECT_EQ(controller.state(), SafetyState::Running);

    auto rejection = controller.requestReleaseEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::NotEmergencyStopped);
    EXPECT_EQ(controller.state(), SafetyState::Running); // 状态不变
}

// 19. 前置条件：EmergencyStopping 下解除急停 -> NotEmergencyStopped
TEST(EmergencyStopController, should_reject_release_when_emergency_stopping)
{
    EmergencyStopController controller;
    controller.applyFeedback(false);

    controller.requestEmergencyStop();
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping);

    auto rejection = controller.requestReleaseEmergencyStop();

    EXPECT_EQ(rejection, SafetyRejection::NotEmergencyStopped);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopping); // 状态不变
}

// ============================================================
// 状态查询测试
// ============================================================

// 20. 验证 isSystemLocked() 对全部 5 态的覆盖（含 NotSynchronized）
TEST(EmergencyStopController, is_system_locked_should_cover_all_locked_states)
{
    EmergencyStopController controller;

    // NotSynchronized: 锁定（真相未知，保守策略）
    EXPECT_TRUE(controller.isSystemLocked());

    // 首次同步 -> Running: 不锁定
    controller.applyFeedback(false);
    EXPECT_FALSE(controller.isSystemLocked());

    // EmergencyStopping: 锁定
    controller.requestEmergencyStop();
    EXPECT_TRUE(controller.isSystemLocked());

    // EmergencyStopped: 锁定
    controller.applyFeedback(true);
    EXPECT_TRUE(controller.isSystemLocked());

    // ReleasingEmergencyStop: 锁定
    controller.requestReleaseEmergencyStop();
    EXPECT_TRUE(controller.isSystemLocked());

    // 恢复 Running: 不锁定
    controller.applyFeedback(false);
    EXPECT_FALSE(controller.isSystemLocked());
}

// 21. 验证 isEmergencyStopped() 和 isTransitioning() 的精确语义
TEST(EmergencyStopController, state_query_methods_should_be_precise)
{
    EmergencyStopController controller;

    // NotSynchronized
    EXPECT_FALSE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());

    // 首次同步 -> Running
    controller.applyFeedback(false);
    EXPECT_FALSE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());

    // EmergencyStopping
    controller.requestEmergencyStop();
    EXPECT_FALSE(controller.isEmergencyStopped());  // 还未确认
    EXPECT_TRUE(controller.isTransitioning());

    // EmergencyStopped
    controller.applyFeedback(true);
    EXPECT_TRUE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());     // 已确认，不再过渡

    // ReleasingEmergencyStop
    controller.requestReleaseEmergencyStop();
    EXPECT_FALSE(controller.isEmergencyStopped());  // 已发起解除
    EXPECT_TRUE(controller.isTransitioning());

    // 恢复 Running
    controller.applyFeedback(false);
    EXPECT_FALSE(controller.isEmergencyStopped());
    EXPECT_FALSE(controller.isTransitioning());
}

// 22. applyFeedback(true) 首次同步 -> EmergencyStopped 后可以走完整软件解除链路
TEST(EmergencyStopController, should_release_after_synced_to_emergency_stopped_via_feedback)
{
    EmergencyStopController controller;

    // PLC 已在急停中：首次 feedback 即同步到 EmergencyStopped
    controller.applyFeedback(true);
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // 软件请求解除
    auto rejection = controller.requestReleaseEmergencyStop();
    EXPECT_EQ(rejection, SafetyRejection::None);
    EXPECT_EQ(controller.state(), SafetyState::ReleasingEmergencyStop);
    EXPECT_TRUE(controller.hasPendingCommand());

    auto cmd = controller.popPendingCommand();
    EXPECT_FALSE(cmd.active);

    // PLC 反馈解除
    controller.applyFeedback(false);
    EXPECT_EQ(controller.state(), SafetyState::Running);
}


// 23. EmergencyStopped 是锁存态：即使 PLC 瞬态恢复 false 也不允许自动恢复，必须走解除流程
TEST(EmergencyStopController, should_not_auto_recover_from_emergency_stopped_when_feedback_turns_false)
{
    EmergencyStopController controller;

    controller.applyFeedback(false);

    controller.requestEmergencyStop();
    controller.applyFeedback(true);

    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);

    // PLC 瞬态恢复 false
    controller.applyFeedback(false);

    // 不允许自动恢复
    EXPECT_EQ(controller.state(), SafetyState::EmergencyStopped);
    EXPECT_TRUE(controller.isEmergencyStopped());
    EXPECT_TRUE(controller.isSystemLocked());
}