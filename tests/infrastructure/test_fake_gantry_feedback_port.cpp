#include <gtest/gtest.h>
#include "infrastructure/FakeGantryFeedbackPort.h"
#include "infrastructure/FakePLC.h"

/**
 * @brief TS3.1 FakeGantryFeedbackPort 测试套件
 *
 * 测试用例序号映射：
 *   TS3.1.1 ~ TS3.1.7 来自 domain_layer_tdd_design.md 表格
 *
 * 测试结构：
 *   1. getX1Feedback / getX2Feedback 状态映射正确性
 *   2. resetAlarm 行为
 *   3. isAnyAlarm / isAnyLimit 聚合查询
 */
class FakeGantryFeedbackPortTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeGantryFeedbackPort port{plc};
};

// ═══════════════════════════════════════════════════════════
// TS3.1.1: X1 空闲状态反馈映射
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, X1IdleMapsToEnabledNotAlarmed) {
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.setPosition(AxisId::X1, 42.5);

    PhysicalAxisState state = port.getX1Feedback();

    EXPECT_TRUE(state.enabled);
    EXPECT_FALSE(state.alarmed);
    EXPECT_DOUBLE_EQ(state.position, 42.5);
}

// ═══════════════════════════════════════════════════════════
// TS3.1.2: X2 禁用状态反馈映射
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, X2DisabledMapsToNotEnabled) {
    // 默认即为 Disabled
    PhysicalAxisState state = port.getX2Feedback();

    EXPECT_FALSE(state.enabled);
    EXPECT_FALSE(state.alarmed);
}

// ═══════════════════════════════════════════════════════════
// TS3.1.3: 报警状态映射
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, AlarmStateMapsToAlarmed) {
    plc.forceState(AxisId::X1, AxisState::Error);

    PhysicalAxisState state = port.getX1Feedback();

    EXPECT_TRUE(state.alarmed);
    EXPECT_FALSE(state.enabled); // Error 状态不算 enabled
}

// ═══════════════════════════════════════════════════════════
// TS3.1.4: 限位状态映射
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, LimitStateMapping) {
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setLimits(AxisId::X2, 100.0, -100.0);
    plc.setPosition(AxisId::X2, 100.0); // 压在正限位上
    plc.tick(10); // 触发 FakePLC 限位检测

    PhysicalAxisState state = port.getX2Feedback();

    EXPECT_TRUE(state.posLimitActive);
    EXPECT_FALSE(state.negLimitActive);
}

// ═══════════════════════════════════════════════════════════
// TS3.1.5: resetAlarm 清除报警
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, ResetAlarmClearsErrorState) {
    plc.forceState(AxisId::X1, AxisState::Error);
    plc.forceState(AxisId::X2, AxisState::Error);

    ASSERT_TRUE(port.isAnyAlarm());

    port.resetAlarm();

    EXPECT_FALSE(port.isAnyAlarm());
    PhysicalAxisState x1State = port.getX1Feedback();
    PhysicalAxisState x2State = port.getX2Feedback();
    EXPECT_FALSE(x1State.alarmed);
    EXPECT_FALSE(x2State.alarmed);
    EXPECT_TRUE(x1State.enabled); // 复位后回到 Idle，enabled=true
    EXPECT_TRUE(x2State.enabled);
}

// ═══════════════════════════════════════════════════════════
// TS3.1.6: isAnyAlarm 聚合判断
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, IsAnyAlarmAggregatesBothAxes) {
    // 初始无报警
    EXPECT_FALSE(port.isAnyAlarm());

    // 仅 X1 报警
    plc.forceState(AxisId::X1, AxisState::Error);
    EXPECT_TRUE(port.isAnyAlarm());

    // 清除 X1，仅 X2 报警
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Error);
    EXPECT_TRUE(port.isAnyAlarm());

    // 全部清除
    plc.forceState(AxisId::X2, AxisState::Idle);
    EXPECT_FALSE(port.isAnyAlarm());
}

// ═══════════════════════════════════════════════════════════
// TS3.1.7: isAnyLimit 聚合判断
// ═══════════════════════════════════════════════════════════
TEST_F(FakeGantryFeedbackPortTest, IsAnyLimitAggregatesBothAxes) {
    // 初始无限位
    EXPECT_FALSE(port.isAnyLimit());

    // 仅 X1 正限位
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.setLimits(AxisId::X1, 100.0, -100.0);
    plc.setPosition(AxisId::X1, 100.0);
    plc.tick(10);
    EXPECT_TRUE(port.isAnyLimit());

    // 重置 X1，仅 X2 负限位
    plc.setPosition(AxisId::X1, 0.0);
    plc.tick(10);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setLimits(AxisId::X2, 100.0, -100.0);
    plc.setPosition(AxisId::X2, -100.0);
    plc.tick(10);
    EXPECT_TRUE(port.isAnyLimit());

    // 全部清除
    plc.setPosition(AxisId::X2, 0.0);
    plc.tick(10);
    EXPECT_FALSE(port.isAnyLimit());
}
