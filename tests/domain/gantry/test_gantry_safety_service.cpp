/**
 * @file test_gantry_safety_service.cpp
 * @brief 龙门安全约束服务单元测试 (约束15-17)
 *
 * TDD Red阶段 — 测试先行于实现。
 * 被测组件: domain/service/GantrySafetyService.h (尚未创建)
 *
 * 覆盖设计文档测试用例：
 *   TC-5.1  ~ TC-5.11
 *
 * 约束覆盖：
 *   约束15 — 限位优先级最高
 *   约束16 — 限位后行为限制（可Jog远离不可Move/Jog靠近）
 *   约束17 — 报警状态下禁止所有运动，只允许ResetAlarm
 */

#include <gtest/gtest.h>
#include "service/GantrySafetyService.h"   // [Red] 尚未创建
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "value/GantryMode.h"
#include "value/MotionDirection.h"
#include "value/SafetyCheckResult.h"

// ═══════════════════════════════════════════════════════════
// 辅助: 创建不同状态的龙门系统
// ═══════════════════════════════════════════════════════════

class GantrySafetyServiceTest : public ::testing::Test {
protected:
    // 正常状态系统
    GantrySystem createNormalSystem() {
        PhysicalAxis x1(AxisId::X1);
        PhysicalAxisState s1;
        s1.enabled = true;
        s1.position = 0.0;
        x1.syncState(s1);

        PhysicalAxis x2(AxisId::X2);
        PhysicalAxisState s2;
        s2.enabled = true;
        s2.position = 0.0;
        x2.syncState(s2);

        return GantrySystem(x1, x2);
    }

    // X1 报警状态
    GantrySystem createAlarmSystem() {
        PhysicalAxis x1(AxisId::X1);
        PhysicalAxisState s1;
        s1.enabled = true;
        s1.alarmed = true;
        s1.position = 0.0;
        x1.syncState(s1);

        PhysicalAxis x2(AxisId::X2);
        PhysicalAxisState s2;
        s2.enabled = true;
        s2.position = 0.0;
        x2.syncState(s2);

        return GantrySystem(x1, x2);
    }

    // X1 正向限位
    GantrySystem createForwardLimitSystem() {
        PhysicalAxis x1(AxisId::X1);
        PhysicalAxisState s1;
        s1.enabled = true;
        s1.position = 200.0;
        s1.posLimitActive = true;
        x1.syncState(s1);

        PhysicalAxis x2(AxisId::X2);
        PhysicalAxisState s2;
        s2.enabled = true;
        s2.position = -200.0;
        x2.syncState(s2);

        return GantrySystem(x1, x2);
    }

    // X2 负向限位
    GantrySystem createBackwardLimitSystem() {
        PhysicalAxis x1(AxisId::X1);
        PhysicalAxisState s1;
        s1.enabled = true;
        s1.position = -200.0;
        x1.syncState(s1);

        PhysicalAxis x2(AxisId::X2);
        PhysicalAxisState s2;
        s2.enabled = true;
        s2.position = 200.0;
        s2.negLimitActive = true;
        x2.syncState(s2);

        return GantrySystem(x1, x2);
    }
};

// ═══════════════════════════════════════════════════════════
// 约束15：限位优先级最高 (TC-5.1, 5.2)
// ═══════════════════════════════════════════════════════════

// TC-5.1: 限位优先级最高 — 一旦触发，所有运动操作被拒绝
TEST_F(GantrySafetyServiceTest, Limit_HasHighestPriority) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;

    // MoveAbsolute
    EXPECT_FALSE(safetyService.isMotionAllowed(system, MotionDirection::Forward).isAllowed());
    // MoveRelative
    EXPECT_FALSE(safetyService.isMotionAllowed(system, MotionDirection::Backward).isAllowed());
    // Forward Jog 被拒绝
    EXPECT_FALSE(safetyService.isJogAllowed(system, MotionDirection::Forward));
}

// TC-5.2: 限位时拒绝任何运动的语义
TEST_F(GantrySafetyServiceTest, Limit_AnyMotionShouldBeRejected) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;

    auto result = safetyService.isMotionAllowed(system, MotionDirection::Forward);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_NE(result.reason().find("limit"), std::string::npos);

    // 报警应覆盖限位
    auto alarmSystem = createAlarmSystem();
    auto alarmResult = safetyService.isMotionAllowed(alarmSystem, MotionDirection::Forward);
    EXPECT_FALSE(alarmResult.isAllowed());
    EXPECT_NE(alarmResult.reason().find("alarm"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// 约束16：限位后行为限制 (TC-5.3~5.8)
// ═══════════════════════════════════════════════════════════

// TC-5.3: 正向限位 → 只允许 Backward Jog
TEST_F(GantrySafetyServiceTest, Limit_PositiveAllowedJogBackward) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;

    EXPECT_TRUE(safetyService.isJogAllowed(system, MotionDirection::Backward));
}

// TC-5.4: 正向限位 → 禁止 Forward Jog
TEST_F(GantrySafetyServiceTest, Limit_PositiveForbiddenJogForward) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;

    EXPECT_FALSE(safetyService.isJogAllowed(system, MotionDirection::Forward));
}

// TC-5.5: 负向限位 → 只允许 Forward Jog
TEST_F(GantrySafetyServiceTest, Limit_NegativeAllowedJogForward) {
    auto system = createBackwardLimitSystem();
    GantrySafetyService safetyService;

    EXPECT_TRUE(safetyService.isJogAllowed(system, MotionDirection::Forward));
}

// TC-5.6: 负向限位 → 禁止 Backward Jog
TEST_F(GantrySafetyServiceTest, Limit_NegativeForbiddenJogBackward) {
    auto system = createBackwardLimitSystem();
    GantrySafetyService safetyService;

    EXPECT_FALSE(safetyService.isJogAllowed(system, MotionDirection::Backward));
}

// TC-5.7: 限位时禁止 MoveAbsolute
TEST_F(GantrySafetyServiceTest, Limit_RejectMoveAbsolute) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;

    auto result = safetyService.isMoveAllowed(system, MotionDirection::Forward);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_NE(result.reason().find("limit"), std::string::npos);
}

// TC-5.8: 限位时禁止 MoveRelative
TEST_F(GantrySafetyServiceTest, Limit_RejectMoveRelative) {
    auto system = createBackwardLimitSystem();
    GantrySafetyService safetyService;

    auto result = safetyService.isMoveAllowed(system, MotionDirection::Backward);
    EXPECT_FALSE(result.isAllowed());
}

// ═══════════════════════════════════════════════════════════
// 约束17：报警约束 (TC-5.9~5.11)
// ═══════════════════════════════════════════════════════════

// TC-5.9: 报警时禁止所有运动
TEST_F(GantrySafetyServiceTest, Alarm_RejectAllMotion) {
    auto system = createAlarmSystem();
    GantrySafetyService safetyService;

    EXPECT_FALSE(safetyService.isJogAllowed(system, MotionDirection::Forward));
    EXPECT_FALSE(safetyService.isJogAllowed(system, MotionDirection::Backward));
    EXPECT_FALSE(safetyService.isMoveAllowed(system, MotionDirection::Forward).isAllowed());
    EXPECT_FALSE(safetyService.isMoveAllowed(system, MotionDirection::Backward).isAllowed());
}

// TC-5.10: 报警时只允许 ResetAlarm
TEST_F(GantrySafetyServiceTest, Alarm_OnlyAllowResetAlarm) {
    auto system = createAlarmSystem();
    GantrySafetyService safetyService;

    // 所有运动类型都被禁止
    EXPECT_FALSE(safetyService.isMotionAllowed(system, MotionDirection::Forward).isAllowed());

    // 但 ResetAlarm 应该被允许（这通常通过 isResetAllowed 检查）
    EXPECT_TRUE(safetyService.isResetAllowed(system));
}

// TC-5.11: 报警时禁止 Jog/MoveAbsolute/MoveRelative
TEST_F(GantrySafetyServiceTest, Alarm_RejectJogAndMove) {
    auto system = createAlarmSystem();
    GantrySafetyService safetyService;

    // 验证所有具体的运动类型都被拒绝
    auto jogResult = safetyService.isMotionAllowed(system, MotionDirection::Forward);
    EXPECT_FALSE(jogResult.isAllowed());
    EXPECT_NE(jogResult.reason().find("alarm"), std::string::npos);

    // 双向检查
    auto jogBackResult = safetyService.isMotionAllowed(system, MotionDirection::Backward);
    EXPECT_FALSE(jogBackResult.isAllowed());
    EXPECT_NE(jogBackResult.reason().find("alarm"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// 综合安全检查矩阵
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySafetyServiceTest, NormalSystem_AllOperationsAllowed) {
    auto system = createNormalSystem();
    GantrySafetyService safetyService;

    EXPECT_TRUE(safetyService.isJogAllowed(system, MotionDirection::Forward));
    EXPECT_TRUE(safetyService.isJogAllowed(system, MotionDirection::Backward));
    EXPECT_TRUE(safetyService.isMoveAllowed(system, MotionDirection::Forward).isAllowed());
    EXPECT_TRUE(safetyService.isMoveAllowed(system, MotionDirection::Backward).isAllowed());
}

TEST_F(GantrySafetyServiceTest, IsAnyAlarm_FalseForNormalSystem) {
    auto system = createNormalSystem();
    GantrySafetyService safetyService;
    EXPECT_FALSE(safetyService.isAnyAlarm(system));
}

TEST_F(GantrySafetyServiceTest, IsAnyAlarm_TrueForAlarmSystem) {
    auto system = createAlarmSystem();
    GantrySafetyService safetyService;
    EXPECT_TRUE(safetyService.isAnyAlarm(system));
}

TEST_F(GantrySafetyServiceTest, IsAnyLimit_TrueForLimitSystem) {
    auto system = createForwardLimitSystem();
    GantrySafetyService safetyService;
    EXPECT_TRUE(safetyService.isAnyLimit(system));
}

TEST_F(GantrySafetyServiceTest, IsAnyLimit_FalseForNormalSystem) {
    auto system = createNormalSystem();
    GantrySafetyService safetyService;
    EXPECT_FALSE(safetyService.isAnyLimit(system));
}
