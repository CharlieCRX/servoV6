/**
 * @file test_gantry_activation.cpp
 * @brief 龙门激活与操作约束单元测试 (约束18-20)
 *
 * 覆盖设计文档约束：
 *   约束18 (操作对象互斥)
 *   约束19 (运动互斥)
 *   约束20 (状态一致性 / 聚合语义)
 *
 * 测试用例：
 *   TC-6.1  ~ TC-6.10
 *
 * 被测组件：
 *   - PhysicalAxis (物理轴实体)
 *   - LogicalAxis  (逻辑轴实体)
 *   - GantrySystem  (龙门系统聚合根)
 */

#include <gtest/gtest.h>
#include "entity/Axis.h"            // AxisState
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "event/GantryEvents.h"
#include "value/GantryMode.h"
#include "value/MotionDirection.h"
#include "value/SafetyCheckResult.h"

// ═══════════════════════════════════════════════════════════
// 约束18：操作对象互斥
// ═══════════════════════════════════════════════════════════

class GantryActivationTest : public ::testing::Test {
protected:
    PhysicalAxis createEnabledX1(double pos = 0.0) {
        PhysicalAxis ax(AxisId::X1);
        PhysicalAxisState st;
        st.enabled = true;
        st.position = pos;
        ax.syncState(st);
        return ax;
    }

    PhysicalAxis createEnabledX2(double pos = 0.0) {
        PhysicalAxis ax(AxisId::X2);
        PhysicalAxisState st;
        st.enabled = true;
        st.position = pos;
        ax.syncState(st);
        return ax;
    }
};

// TC-6.1: Coupled 模式只允许操作 X
TEST_F(GantryActivationTest, CoupledMode_OnlyXIsOperable) {
    auto x1 = createEnabledX1(50.0);
    auto x2 = createEnabledX2(-50.0);
    GantrySystem system(x1, x2);

    // 先建立联动
    auto result = system.requestCoupling();
    ASSERT_TRUE(result.allowed) << "联动建立失败: " << result.failReason;
    EXPECT_EQ(system.mode(), GantryMode::Coupled);

    // Coupled 模式下：X 可操作，X1/X2 不可操作
    EXPECT_TRUE(system.isTargetOperable(AxisId::X));
    EXPECT_FALSE(system.isTargetOperable(AxisId::X1));
    EXPECT_FALSE(system.isTargetOperable(AxisId::X2));
}

// TC-6.2: Decoupled 模式只允许操作 X1 或 X2
TEST_F(GantryActivationTest, DecoupledMode_X1OrX2IsOperable) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem system(x1, x2);
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);

    // Decoupled 模式下：X1/X2 可操作，X 不可操作
    EXPECT_FALSE(system.isTargetOperable(AxisId::X));
    EXPECT_TRUE(system.isTargetOperable(AxisId::X1));
    EXPECT_TRUE(system.isTargetOperable(AxisId::X2));
}

// TC-6.3: 禁止同时操作多个对象 — Coupled 模式下对 X 下发命令后 X1 被拒绝
TEST_F(GantryActivationTest, SimultaneousOperation_ShouldBeForbidden) {
    auto x1 = createEnabledX1(50.0);
    auto x2 = createEnabledX2(-50.0);
    GantrySystem system(x1, x2);

    // 建立联动
    auto result = system.requestCoupling();
    ASSERT_TRUE(result.allowed);
    ASSERT_EQ(system.mode(), GantryMode::Coupled);

    // 对 X 下发 Jog 命令 — 应成功
    auto jogResult = system.jog(AxisId::X, MotionDirection::Forward);
    EXPECT_TRUE(jogResult.accepted) << jogResult.rejectReason;

    // 此时再对 X1 下发命令 — 因模式不匹配应被拒绝
    auto x1Result = system.jog(AxisId::X1, MotionDirection::Forward);
    EXPECT_FALSE(x1Result.accepted);
    EXPECT_EQ(x1Result.rejectReason, "Mode: target not operable in current mode");
}

// ============================================================
// 约束19：运动互斥（逻辑轴命令槽）
// ============================================================

// TC-6.4: Jog 执行期间 MoveAbsolute 被拒绝
TEST_F(GantryActivationTest, MotionExclusive_JogAndMove) {
    auto x1 = createEnabledX1(0.0);
    auto x2 = createEnabledX2(0.0);
    GantrySystem system(x1, x2);

    // 建立联动
    auto couplingResult = system.requestCoupling();
    ASSERT_TRUE(couplingResult.allowed);
    ASSERT_EQ(system.mode(), GantryMode::Coupled);

    // Jog 占据命令槽
    auto jogResult = system.jog(AxisId::X, MotionDirection::Forward);
    EXPECT_TRUE(jogResult.accepted);

    // MoveAbsolute 因 Busy 被拒绝
    auto moveResult = system.moveAbsolute(AxisId::X, 100.0);
    EXPECT_FALSE(moveResult.accepted);
    EXPECT_EQ(moveResult.rejectReason, "Slot: command slot is busy");
}

// TC-6.5: MoveAbsolute 执行期间 MoveRelative 被拒绝
TEST_F(GantryActivationTest, MotionExclusive_MoveAbsoluteAndMoveRelative) {
    auto x1 = createEnabledX1(0.0);
    auto x2 = createEnabledX2(0.0);
    GantrySystem system(x1, x2);

    auto couplingResult = system.requestCoupling();
    ASSERT_TRUE(couplingResult.allowed);
    ASSERT_EQ(system.mode(), GantryMode::Coupled);

    // MoveAbsolute 占据命令槽
    auto absResult = system.moveAbsolute(AxisId::X, 200.0);
    EXPECT_TRUE(absResult.accepted);

    // MoveRelative 被拒绝
    auto relResult = system.moveRelative(AxisId::X, 50.0);
    EXPECT_FALSE(relResult.accepted);
    EXPECT_EQ(relResult.rejectReason, "Slot: command slot is busy");
}

// TC-6.6: 先清空命令槽 + 新命令覆盖（Stop 逻辑）
TEST_F(GantryActivationTest, OnlyOneMotionIntent_AtAnyTime) {
    auto x1 = createEnabledX1(0.0);
    auto x2 = createEnabledX2(0.0);
    GantrySystem system(x1, x2);

    auto couplingResult = system.requestCoupling();
    ASSERT_TRUE(couplingResult.allowed);

    // Jog Forward 占据命令槽
    auto jog1 = system.jog(AxisId::X, MotionDirection::Forward);
    EXPECT_TRUE(jog1.accepted);

    // 先 Stop 清除命令
    auto stopResult = system.stop(AxisId::X);
    EXPECT_TRUE(stopResult.accepted);
    EXPECT_TRUE(system.logical().canAcceptCommand()); // 命令槽已清空

    // 再 Jog Backward 应成功（新命令）
    auto jog2 = system.jog(AxisId::X, MotionDirection::Backward);
    EXPECT_TRUE(jog2.accepted) << jog2.rejectReason;
}

// ============================================================
// 约束20：状态聚合
// ============================================================

// TC-6.7: X1 运动 → X 聚合为 MovingAbsolute
TEST_F(GantryActivationTest, StateAggregation_X1Moving_XShouldBeMoving) {
    auto x1 = createEnabledX1(30.0);
    auto x2 = createEnabledX2(-30.0);
    GantrySystem system(x1, x2);

    // 设置 X1 运动类型
    system.setX1Motion(LogicalAxis::AggregatedMotion::MovingAbsolute);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    EXPECT_EQ(system.logical().aggregatedState(), AxisState::MovingAbsolute);
    EXPECT_EQ(system.logical().motion(), LogicalAxis::AggregatedMotion::MovingAbsolute);
    EXPECT_TRUE(system.logical().isMoving());
}

// TC-6.8: X2 报警 → X 聚合为 Error
TEST_F(GantryActivationTest, StateAggregation_X2Alarm_XShouldBeAlarm) {
    PhysicalAxis x1 = createEnabledX1(30.0);
    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState x2AlarmState;
    x2AlarmState.enabled = true;
    x2AlarmState.alarmed = true;
    x2AlarmState.position = -30.0;
    x2.syncState(x2AlarmState);

    GantrySystem system(x1, x2);

    system.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Error);
}

// TC-6.9: X1 限位 → X 聚合为 限位阻断（Idle 状态 + anyLimit=true）
TEST_F(GantryActivationTest, StateAggregation_X1Limit_XShouldBeLimit) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxisState x1LimitState;
    x1LimitState.enabled = true;
    x1LimitState.position = 150.0;
    x1LimitState.posLimitActive = true;
    x1.syncState(x1LimitState);

    PhysicalAxis x2 = createEnabledX2(-150.0);

    GantrySystem system(x1, x2);
    system.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    // 限位阻断：状态为 Idle，但 anyLimit 为 true
    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Idle);
    EXPECT_TRUE(system.logical().hasActiveLimit());

    // 应产生限位触发事件
    auto events = system.drainEvents();
    bool hasLimitEvent = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::LimitTriggered) {
            hasLimitEvent = true;
        }
    }
    EXPECT_TRUE(hasLimitEvent);
}

// TC-6.10: 双轴 Idle → X = Idle
TEST_F(GantryActivationTest, StateAggregation_BothIdle_XShouldBeIdle) {
    auto x1 = createEnabledX1(0.0);
    auto x2 = createEnabledX2(0.0);
    GantrySystem system(x1, x2);

    system.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Idle);
    EXPECT_EQ(system.logical().motion(), LogicalAxis::AggregatedMotion::Idle);
    EXPECT_FALSE(system.logical().isMoving());
}

// ============================================================
// 状态聚合优先级测试
// ============================================================

// TC-6.11: Alarm > Moving
TEST_F(GantryActivationTest, StateAggregation_AlarmOverridesMoving) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxisState x1MovingState;
    x1MovingState.enabled = true;
    x1MovingState.position = 50.0;
    x1.syncState(x1MovingState);

    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState x2AlarmState;
    x2AlarmState.enabled = true;
    x2AlarmState.alarmed = true;
    x2AlarmState.position = -50.0;
    x2.syncState(x2AlarmState);

    GantrySystem system(x1, x2);
    system.setX1Motion(LogicalAxis::AggregatedMotion::MovingAbsolute);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    // 报警覆盖运动：X 应为 Error
    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Error);
}

// TC-6.12: Limit > Moving (约束15)
TEST_F(GantryActivationTest, StateAggregation_LimitOverridesMoving) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxisState x1MovingState;
    x1MovingState.enabled = true;
    x1MovingState.position = 40.0;
    x1.syncState(x1MovingState);

    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState x2LimitState;
    x2LimitState.enabled = true;
    x2LimitState.position = -40.0;
    x2LimitState.posLimitActive = true;
    x2.syncState(x2LimitState);

    GantrySystem system(x1, x2);
    system.setX1Motion(LogicalAxis::AggregatedMotion::MovingAbsolute);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    // 限位覆盖运动：X 应为 Idle (限位阻断)
    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Idle);
    EXPECT_TRUE(system.logical().hasActiveLimit());
}

// TC-6.13: Alarm > Limit (约束17)
TEST_F(GantryActivationTest, StateAggregation_AlarmOverridesLimit) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxisState x1LimitState;
    x1LimitState.enabled = true;
    x1LimitState.position = 10.0;
    x1LimitState.posLimitActive = true;
    x1.syncState(x1LimitState);

    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState x2AlarmState;
    x2AlarmState.enabled = true;
    x2AlarmState.alarmed = true;
    x2AlarmState.position = -10.0;
    x2.syncState(x2AlarmState);

    GantrySystem system(x1, x2);
    system.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    system.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    system.aggregateState();

    // 报警覆盖限位：X 应为 Error
    EXPECT_EQ(system.logical().aggregatedState(), AxisState::Error);
}
