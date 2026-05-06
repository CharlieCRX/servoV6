/**
 * @file test_gantry_safety.cpp
 * @brief 龙门安全语义约束测试 (约束15-17)
 *
 * 覆盖设计文档约束：
 *   约束15 - 限位优先级最高
 *   约束16 - 限位后行为限制
 *   约束17 - 报警约束
 *
 * 测试用例映射：
 *   TC-5.1  ~ TC-5.2  → 限位优先级
 *   TC-5.3  ~ TC-5.8  → 限位方向行为限制
 *   TC-5.9  ~ TC-5.11 → 报警约束
 *
 * 测试组件：
 *   - GantrySystem::checkOperability() (操作安全检查)
 *   - SafetyCheckResult  + checkMotionSafety() (方向安全检查)
 *   - CouplingCondition (联动条件中包含报警/限位)
 */

#include <gtest/gtest.h>
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "value/GantryMode.h"
#include "value/SafetyCheckResult.h"
#include "value/MotionDirection.h"

// ═══════════════════════════════════════════════════════════
// 辅助：快速构造 GantrySystem
// ═══════════════════════════════════════════════════════════

static GantrySystem makeGantry() {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    return GantrySystem(x1, x2);
}

// ═══════════════════════════════════════════════════════════
// TC-5.1: 限位优先级最高 (约束15)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, LimitHasHighestPriority) {
    // 约束15: 限位触发时，所有运动非法
    // 仅报警可以覆盖限位 (报警优先级更高，在 checkOperability 中第二步就检查)
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);

    // 限位时 Jog Forward 应被拒绝
    auto op = gantry.checkOperability(AxisId::X1, MotionDirection::Forward);
    EXPECT_EQ(op, Operability::Rejected_Limit);
}

TEST(SafetyLimitTest, LimitBlocksAllX2Operation) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X2, MotionDirection::Backward);
    EXPECT_EQ(op, Operability::Rejected_Limit);
}

// ═══════════════════════════════════════════════════════════
// TC-5.2: 限位时拒绝任何运动 (约束15)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, LimitRejectsMoveAbsolute) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    auto result = gantry.moveAbsolute(AxisId::X1, 200.0);
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Limit"), std::string::npos);
}

TEST(SafetyLimitTest, LimitRejectsMoveRelative) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);
    auto result = gantry.moveRelative(AxisId::X2, -10.0);
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Limit"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-5.3: 正限位 → 允许 Backward Jog (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, PositiveLimit_AllowsBackwardJog) {
    // 约束16: 触发正向限位 → 允许远离限位 (Backward) 的 Jog
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X1, MotionDirection::Backward);
    EXPECT_EQ(op, Operability::Allowed);
}

TEST(SafetyLimitTest, PositiveLimit_JogBackward_Accepted) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    auto result = gantry.jog(AxisId::X1, MotionDirection::Backward);
    EXPECT_TRUE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.4: 正限位 → 禁止 Forward Jog (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, PositiveLimit_BlocksForwardJog) {
    // 约束16: 触发正向限位 → 禁止朝限位方向 (Forward) 的 Jog
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X1, MotionDirection::Forward);
    EXPECT_EQ(op, Operability::Rejected_Limit);
}

TEST(SafetyLimitTest, PositiveLimit_JogForward_Rejected) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    auto result = gantry.jog(AxisId::X1, MotionDirection::Forward);
    EXPECT_FALSE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.5: 负限位 → 允许 Forward Jog (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, NegativeLimit_AllowsForwardJog) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X2, MotionDirection::Forward);
    EXPECT_EQ(op, Operability::Allowed);
}

TEST(SafetyLimitTest, NegativeLimit_JogForward_Accepted) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);
    auto result = gantry.jog(AxisId::X2, MotionDirection::Forward);
    EXPECT_TRUE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.6: 负限位 → 禁止 Backward Jog (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, NegativeLimit_BlocksBackwardJog) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X2, MotionDirection::Backward);
    EXPECT_EQ(op, Operability::Rejected_Limit);
}

TEST(SafetyLimitTest, NegativeLimit_JogBackward_Rejected) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);
    auto result = gantry.jog(AxisId::X2, MotionDirection::Backward);
    EXPECT_FALSE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.7: 限位时禁止 MoveAbsolute (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, Limit_RejectsMoveAbsolute_X1) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    auto result = gantry.moveAbsolute(AxisId::X1, 300.0);
    EXPECT_FALSE(result.accepted);
}

TEST(SafetyLimitTest, Limit_RejectsMoveAbsolute_X2) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);
    auto result = gantry.moveAbsolute(AxisId::X2, -50.0);
    EXPECT_FALSE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.8: 限位时禁止 MoveRelative (约束16)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, Limit_RejectsMoveRelative_X1) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    auto result = gantry.moveRelative(AxisId::X1, 10.0);
    EXPECT_FALSE(result.accepted);
}

TEST(SafetyLimitTest, Limit_RejectsMoveRelative_X2) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setNegLimitActive(true);
    auto result = gantry.moveRelative(AxisId::X2, -10.0);
    EXPECT_FALSE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.9: 报警时禁止所有运动 (约束17)
// ═══════════════════════════════════════════════════════════

TEST(SafetyAlarmTest, Alarm_RejectsJog) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    auto result = gantry.jog(AxisId::X1, MotionDirection::Forward);
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Alarm"), std::string::npos);
}

TEST(SafetyAlarmTest, Alarm_RejectsMoveAbsolute) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setAlarmed(true);
    auto result = gantry.moveAbsolute(AxisId::X2, 100.0);
    EXPECT_FALSE(result.accepted);
}

TEST(SafetyAlarmTest, Alarm_RejectsMoveRelative) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    auto result = gantry.moveRelative(AxisId::X1, 50.0);
    EXPECT_FALSE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// TC-5.10: 报警时只允许 ResetAlarm (约束17)
// ═══════════════════════════════════════════════════════════

TEST(SafetyAlarmTest, Alarm_StopStillAccepted) {
    // 约束17: 报警时不允许 Jog/Move，但 Stop 始终可接受
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    auto result = gantry.stop(AxisId::X1);
    EXPECT_TRUE(result.accepted);  // Stop 不做安全检查
}

// 注：ResetAlarm 操作由 FeedbackPort 接口暴露，不在 GantrySystem 中直接体现。
// 报警清除后通过 syncState() 刷新状态，checkOperability() 恢复正常。

TEST(SafetyAlarmTest, AfterAlarmCleared_NormalOperationResumes) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Alarm);

    // 模拟报警清除
    gantry.x1().setAlarmed(false);
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Allowed);
}

// ═══════════════════════════════════════════════════════════
// TC-5.11: 报警覆盖限位 (约束17 优先级)
// ═══════════════════════════════════════════════════════════

TEST(SafetyAlarmTest, Alarm_OverridesLimit) {
    // 约束17: 报警优先级高于限位
    // 同时有报警和限位 → 应报告 Alarm 而非 Limit
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    gantry.x1().setPosLimitActive(true);

    auto op = gantry.checkOperability(AxisId::X1, MotionDirection::Backward);
    EXPECT_EQ(op, Operability::Rejected_Alarm);  // 先检查报警
}

TEST(SafetyAlarmTest, AlarmOverridesLimit_InBothAxes) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    gantry.x1().setPosLimitActive(true);
    gantry.x2().setAlarmed(true);
    gantry.x2().setNegLimitActive(true);

    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Alarm);
    EXPECT_EQ(gantry.checkOperability(AxisId::X2, MotionDirection::Backward),
              Operability::Rejected_Alarm);
}

// ═══════════════════════════════════════════════════════════
// 补充：Stop 命令在任何安全状态下都允许
// ═══════════════════════════════════════════════════════════

TEST(SafetyTest, StopAlwaysAllowed_EvenWithAlarm) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setAlarmed(true);
    auto result = gantry.stop(AxisId::X1);
    EXPECT_TRUE(result.accepted);
}

TEST(SafetyTest, StopAlwaysAllowed_EvenWithLimit) {
    GantrySystem gantry = makeGantry();
    gantry.x2().setPosLimitActive(true);
    auto result = gantry.stop(AxisId::X2);
    EXPECT_TRUE(result.accepted);
}

// ═══════════════════════════════════════════════════════════
// 补充：同时触发双向限位 (极端故障场景)
// ═══════════════════════════════════════════════════════════

TEST(SafetyLimitTest, BothLimits_AllDirectionsBlocked) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosLimitActive(true);
    gantry.x1().setNegLimitActive(true);

    // 双向限位 → 两个方向都应该被拒绝
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Limit);
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Backward),
              Operability::Rejected_Limit);
}

// ═══════════════════════════════════════════════════════════
// 补充：Coupled 模式下安全检查覆盖 X 逻辑轴
// ═══════════════════════════════════════════════════════════

TEST(SafetyCoupledTest, CoupledMode_X_LimitCheckViaPhysicalAxes) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosition(100.0);
    gantry.x2().setPosition(-100.0);
    gantry.requestCoupling();

    // X 轴正向限位 → Coupled 模式下 Jog X Forward 被拒
    gantry.x1().setPosLimitActive(true);
    auto op = gantry.checkOperability(AxisId::X, MotionDirection::Forward);
    EXPECT_EQ(op, Operability::Rejected_Limit);
}

TEST(SafetyCoupledTest, CoupledMode_X_AlarmBlocksAll) {
    GantrySystem gantry = makeGantry();
    gantry.x1().setPosition(100.0);
    gantry.x2().setPosition(-100.0);
    gantry.requestCoupling();

    gantry.x2().setAlarmed(true);
    auto op = gantry.checkOperability(AxisId::X, MotionDirection::Forward);
    EXPECT_EQ(op, Operability::Rejected_Alarm);
}
