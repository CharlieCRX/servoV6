#include <gtest/gtest.h>
#include "application/policy/JoystickControlPolicy.h"
#include "application/joystick/JoystickState.h"
#include "application/joystick/JoystickDirection.h"

// ============================================================================
// JoystickControlPolicy 单元测试
// ============================================================================
//
// 测试覆盖：
//   1. 点动模式 — 方向分支 + 电平触发（按/回/跳）
//   2. 定位模式 — 边沿触发（推-回-再推）
//   3. 跨轴跳跃保护（Cross-Axis Jump Guard）
//   4. 手柄断开保护
//   5. 模式切换与边界
//
// 方向语义：
//   Forward / Right  → JOG+（电平）/ +distance（边沿）
//   Backward / Left  → JOG-（电平）/ -distance（边沿）
//   Neutral          → Release / 解锁边沿
// ============================================================================

// ═══════════════════════════════════════════════════════════════
// 辅助：构造带有已连接状态的 JoystickState
// ═══════════════════════════════════════════════════════════════

static JoystickState MakeState(JoystickDirection dir) {
    JoystickState s;
    s.connected = true;
    s.direction = dir;
    return s;
}

static JoystickState MakeDisconnected() {
    JoystickState s;
    s.connected = false;
    s.direction = JoystickDirection::Neutral;
    return s;
}

// ═══════════════════════════════════════════════════════════════
// 辅助：构造 ActionCallbacks 带计数器
// ═══════════════════════════════════════════════════════════════

struct CallbackCounters {
    int jogPosPressed  = 0;
    int jogPosReleased = 0;
    int jogNegPressed  = 0;
    int jogNegReleased = 0;
    int moveRequested  = 0;
    double lastDistance = 0.0;
    bool moveResult    = true;  // 模拟回调返回值
    int absTriggered   = 0;
    int stopCalled     = 0;

    JoystickControlPolicy::ActionCallbacks makeCallbacks() {
        JoystickControlPolicy::ActionCallbacks cb;
        cb.onJogPositivePressed  = [this]() { jogPosPressed++; };
        cb.onJogPositiveReleased = [this]() { jogPosReleased++; };
        cb.onJogNegativePressed  = [this]() { jogNegPressed++; };
        cb.onJogNegativeReleased = [this]() { jogNegReleased++; };
        cb.onPositionMoveRequested = [this](double d) -> bool {
            moveRequested++;
            lastDistance = d;
            return moveResult;
        };
        cb.onAbsMoveTriggered = [this]() { absTriggered++; };
        cb.onStop = [this]() { stopCalled++; };
        return cb;
    }
};

// ═══════════════════════════════════════════════════════════════
// Section 1: 点动模式 — 基本电平触发
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, JogMode_NeutralToForward_PressesJogPositive) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Neutral → Forward
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);
    EXPECT_EQ(ctr.jogPosReleased, 0);
    EXPECT_EQ(ctr.jogNegPressed, 0);
    EXPECT_EQ(ctr.jogNegReleased, 0);
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Forward);
}

TEST(JoystickControlPolicy, JogMode_ForwardToNeutral_ReleasesJogPositive) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Step 1: Neutral → Forward
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);

    // Step 2: Forward → Neutral
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosReleased, 1);
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Neutral);
}

TEST(JoystickControlPolicy, JogMode_NeutralToBackward_PressesJogNegative) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);
    EXPECT_EQ(ctr.jogPosPressed, 0);
}

TEST(JoystickControlPolicy, JogMode_BackwardToNeutral_ReleasesJogNegative) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Step 1: Neutral → Backward
    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    // Step 2: Backward → Neutral
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegReleased, 1);
}

TEST(JoystickControlPolicy, JogMode_NeutralToRight_PressesJogPositive) {
    // Right 映射到 JOG+，与 Forward 语义相同
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);
    EXPECT_EQ(ctr.jogNegPressed, 0);
}

TEST(JoystickControlPolicy, JogMode_NeutralToLeft_PressesJogNegative) {
    // Left 映射到 JOG-，与 Backward 语义相同
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);
    EXPECT_EQ(ctr.jogPosPressed, 0);
}

TEST(JoystickControlPolicy, JogMode_RightToNeutral_ReleasesJogPositive) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);

    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosReleased, 1);
}

TEST(JoystickControlPolicy, JogMode_LeftToNeutral_ReleasesJogNegative) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegReleased, 1);
}

// ═══════════════════════════════════════════════════════════════
// Section 2: 点动模式 — 按住不重复触发（电平保持）
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, JogMode_HoldForward_NoRepeatPress) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // 第一次：空闲 → Forward  → 触发 Pressed
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);
    EXPECT_EQ(ctr.jogPosReleased, 0);

    // 第二次：按住不放 → 不重复触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);   // 仍是 1
    EXPECT_EQ(ctr.jogPosReleased, 0);  // 未释放

    // 第三次：再按住
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);
    EXPECT_EQ(ctr.jogPosReleased, 0);
}

TEST(JoystickControlPolicy, JogMode_HoldBackward_NoRepeatPress) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);  // 不重复
}

// ═══════════════════════════════════════════════════════════════
// Section 3: 跨轴跳跃保护（Cross-Axis Jump Guard）
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, CrossAxisGuard_ForwardToBackward_ReleasesForwardFirst) {
    // 场景：摇杆从 Forward 直接跳到 Backward（不经过 Neutral）
    // 期望：先补发 ForwardReleased，再按 BackwardPressed
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Step 1: Forward
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);
    EXPECT_EQ(ctr.jogNegPressed, 0);

    // Step 2: 直接跳到 Backward（跨越 Neutral）
    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogPosReleased, 1); // ★ 跨轴保护：必须先释放 Forward
    EXPECT_EQ(ctr.jogNegPressed, 1);  // ★ 然后触发 Backward Pressed
}

TEST(JoystickControlPolicy, CrossAxisGuard_BackwardToForward_ReleasesBackwardFirst) {
    // 对称场景
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Step 1: Backward
    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    // Step 2: 直接跳到 Forward
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogNegReleased, 1); // ★ 跨轴保护
    EXPECT_EQ(ctr.jogPosPressed, 1);
}

TEST(JoystickControlPolicy, CrossAxisGuard_RightToLeft_ReleasesRightFirst) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogPosReleased, 1); // Right Released
    EXPECT_EQ(ctr.jogNegPressed, 1);  // Left Pressed
}

TEST(JoystickControlPolicy, CrossAxisGuard_LeftToRight_ReleasesLeftFirst) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogNegReleased, 1);
    EXPECT_EQ(ctr.jogPosPressed, 1);
}

TEST(JoystickControlPolicy, CrossAxisGuard_ForwardToRight_NoRedundantRelease) {
    // Forward → Right：两者都是 JOG+，不应触发 Released
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosReleased, 0); // ★ 不应该释放（同方向语义）
    EXPECT_EQ(ctr.jogPosPressed, 1);  // ★ 不应该重复按下
}

TEST(JoystickControlPolicy, CrossAxisGuard_BackwardToLeft_NoRedundantRelease) {
    // Backward → Left：两者都是 JOG-，不应触发 Released
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegReleased, 0); // ★ 不应该释放
    EXPECT_EQ(ctr.jogNegPressed, 1);  // ★ 不应该重复按下
}

// ═══════════════════════════════════════════════════════════════
// Section 4: 定位模式 — 边沿触发
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, PositionMode_ForwardTriggersMoveOnce) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(2.0);
    CallbackCounters ctr;

    // 第一次推前 → 触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);
    EXPECT_EQ(ctr.lastDistance, 2.0);  // +stepDistance

    // 按住不放 → 不触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);  // 仍是 1

    // 回中
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());

    // 再推前 → 第二次触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 2);
    EXPECT_EQ(ctr.lastDistance, 2.0);
}

TEST(JoystickControlPolicy, PositionMode_BackwardTriggersNegativeDistance) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(5.0);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);
    EXPECT_EQ(ctr.lastDistance, -5.0);  // 后退 → 负距离
}

TEST(JoystickControlPolicy, PositionMode_RightTriggersPositiveDistance) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(3.0);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);
    EXPECT_EQ(ctr.lastDistance, 3.0);  // Right 映射到 JOG+ 方向
}

TEST(JoystickControlPolicy, PositionMode_LeftTriggersNegativeDistance) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(3.0);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Left), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);
    EXPECT_EQ(ctr.lastDistance, -3.0);
}

TEST(JoystickControlPolicy, PositionMode_HoldDoesNotRepeat) {
    // 边沿触发：按住不放 = 只触发一次
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(1.0);
    CallbackCounters ctr;

    for (int i = 0; i < 10; i++) {
        policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    }
    EXPECT_EQ(ctr.moveRequested, 1);  // 10 帧中只触发 1 次
}

TEST(JoystickControlPolicy, PositionMode_DiagonalDirectionIgnored) {
    // 对角方向（ForwardLeft 等）不应该触发定位移动
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(1.0);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::ForwardLeft), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 0);

    policy.tick(MakeState(JoystickDirection::BackwardRight), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 0);
}

TEST(JoystickControlPolicy, PositionMode_ArmsAfterNeutral) {
    // 验证 m_positionTriggerArmed 在回中后重置
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    CallbackCounters ctr;

    // 初始状态：已解锁
    EXPECT_TRUE(policy.positionTriggerArmed());

    // 推前 → 锁定（内部会调用 onPositionMoveRequested，需提供有效回调）
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_FALSE(policy.positionTriggerArmed());

    // 回中 → 解锁
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_TRUE(policy.positionTriggerArmed());
}

// ═══════════════════════════════════════════════════════════════
// Section 5: 手柄断开保护
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, DisconnectDuringJog_ReleasesActiveDirection) {
    // 场景：点动模式下推住 Forward，手柄突然断开 → 应自动释放
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    // Step 1: 推 Forward
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogPosPressed, 1);

    // Step 2: 手柄断开
    policy.tick(MakeDisconnected(), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogPosReleased, 1);     // ★ 自动释放 Forward
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Neutral);
}

TEST(JoystickControlPolicy, DisconnectDuringJogBackward_ReleasesActiveDirection) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.jogNegPressed, 1);

    policy.tick(MakeDisconnected(), ctr.makeCallbacks());
    EXPECT_GE(ctr.jogNegReleased, 1);     // ★ 自动释放 Backward
}

TEST(JoystickControlPolicy, DisconnectInPositionMode_ResetsTriggerArmed) {
    // 定位模式下断开 → 应重置边沿触发状态
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_FALSE(policy.positionTriggerArmed());  // 已锁定

    policy.tick(MakeDisconnected(), ctr.makeCallbacks());
    EXPECT_TRUE(policy.positionTriggerArmed());   // ★ 断开后重置
}

TEST(JoystickControlPolicy, DisconnectThenReconnect_ResetsDirection) {
    // 断开后重连：lastDirection 应重置为 Neutral
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Forward);

    policy.tick(MakeDisconnected(), ctr.makeCallbacks());
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Neutral);
}

// ═══════════════════════════════════════════════════════════════
// Section 6: 模式切换
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, ModeSwitch_JogToPosition_ResetsTriggerArmed) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    EXPECT_EQ(policy.uiMode(), JoystickControlPolicy::UIMode::Jog);

    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    EXPECT_EQ(policy.uiMode(), JoystickControlPolicy::UIMode::Position);

    // 切换到定位模式后，边沿触发应已解锁
    EXPECT_TRUE(policy.positionTriggerArmed());
}

TEST(JoystickControlPolicy, ModeSwitch_PositionToJog_StopsPositionDispatch) {
    // 从定位模式切回点动模式后，摇杆不应再触发定位移动
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(1.0);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);

    // 切换到点动模式
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);

    // 回中再推 → 不应触发定位移动
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);  // ★ 不增加（已切换到点动模式）
    EXPECT_GT(ctr.jogPosPressed, 0);  // ★ 应触发点动而不是定位
}

// ═══════════════════════════════════════════════════════════════
// Section 7: 步进距离设置
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, StepDistance_DefaultIsOne) {
    JoystickControlPolicy policy;
    EXPECT_EQ(policy.stepDistance(), 1.0);
}

TEST(JoystickControlPolicy, StepDistance_CanBeChanged) {
    JoystickControlPolicy policy;
    policy.setStepDistance(0.1);
    EXPECT_EQ(policy.stepDistance(), 0.1);

    policy.setStepDistance(100.0);
    EXPECT_EQ(policy.stepDistance(), 100.0);
}

TEST(JoystickControlPolicy, StepDistance_AffectsPositionMoveDistance) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    CallbackCounters ctr;

    // 默认步进 1.0
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.lastDistance, 1.0);

    // 修改步进为 0.5
    policy.setStepDistance(0.5);
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    policy.tick(MakeState(JoystickDirection::Right), ctr.makeCallbacks());
    EXPECT_EQ(ctr.lastDistance, 0.5);
}

// ═══════════════════════════════════════════════════════════════
// Section 8: 回调返回值不影响边沿锁定
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, PositionMode_CallbackReturnsFalse_StillLocksEdge) {
    // 即使回调返回 false（操作被拒绝），边沿也应锁定，避免连续重试
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Position);
    policy.setStepDistance(1.0);
    CallbackCounters ctr;
    ctr.moveResult = false;  // 模拟回调返回 false（被拒绝）

    // 第一次推 → 触发但被拒绝
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);
    EXPECT_FALSE(policy.positionTriggerArmed());  // ★ 仍然锁定

    // 按住不放 → 不触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 1);  // ★ 不重复

    // 回中
    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_TRUE(policy.positionTriggerArmed());

    // 再推 → 可以再次触发
    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(ctr.moveRequested, 2);  // ★ 再次尝试
}

// ═══════════════════════════════════════════════════════════════
// Section 9: 空闲状态（Neutral 连续帧无副作用）
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, NeutralContinuous_NoCallbacks) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    for (int i = 0; i < 5; i++) {
        policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    }

    EXPECT_EQ(ctr.jogPosPressed, 0);
    EXPECT_EQ(ctr.jogPosReleased, 0);
    EXPECT_EQ(ctr.jogNegPressed, 0);
    EXPECT_EQ(ctr.jogNegReleased, 0);
    EXPECT_EQ(ctr.moveRequested, 0);
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Neutral);
}

// ═══════════════════════════════════════════════════════════════
// Section 10: lastDirection 状态追踪
// ═══════════════════════════════════════════════════════════════

TEST(JoystickControlPolicy, LastDirection_TracksCurrentDirection) {
    JoystickControlPolicy policy;
    policy.setUIMode(JoystickControlPolicy::UIMode::Jog);
    CallbackCounters ctr;

    policy.tick(MakeState(JoystickDirection::Forward), ctr.makeCallbacks());
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Forward);

    policy.tick(MakeState(JoystickDirection::Backward), ctr.makeCallbacks());
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Backward);

    policy.tick(MakeState(JoystickDirection::Neutral), ctr.makeCallbacks());
    EXPECT_EQ(policy.lastDirection(), JoystickDirection::Neutral);
}
