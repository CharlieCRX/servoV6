/**
 * @file test_gantry_system_motion.cpp
 * @brief GantrySystem 运动命令单元测试 (T5.4)
 *
 * 覆盖设计文档约束：
 *   约束19 (命令槽互斥)
 *   约束15 (限位触发后Jog/Move方向限制)
 *
 * 测试用例：TS5.4.1 ~ TS5.4.7
 */

#include <gtest/gtest.h>
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "entity/AxisId.h"
#include "value/GantryMode.h"
#include "value/MotionDirection.h"

// ═══════════════════════════════════════════════════════════
// 测试夹具
// ═══════════════════════════════════════════════════════════

class GantrySystemMotionTest : public ::testing::Test {
protected:
    GantrySystemMotionTest() : m_x1(AxisId::X1), m_x2(AxisId::X2) {}

    void SetUp() override {
        m_x1.setEnabled(true);
        m_x2.setEnabled(true);
        m_x1.setPosition(0.0);
        m_x2.setPosition(0.0);
    }

    GantrySystem createSystem() const {
        return GantrySystem(m_x1, m_x2);
    }

    PhysicalAxis m_x1;
    PhysicalAxis m_x2;
};

// ═══════════════════════════════════════════════════════════
// TS5.4.1: Coupled 模式下 Jog X 成功
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, JogXInCoupledModeAccepted) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    auto result = sys.jog(AxisId::X, MotionDirection::Forward);

    EXPECT_TRUE(result.accepted) << result.rejectReason;
    EXPECT_EQ(sys.logical().pendingCommand().type,
              LogicalAxis::CommandType::Jog);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.2: Coupled 模式下 MoveAbsolute X 成功
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, MoveAbsoluteXInCoupledModeAccepted) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    auto result = sys.moveAbsolute(AxisId::X, 100.0);

    EXPECT_TRUE(result.accepted) << result.rejectReason;
    EXPECT_EQ(sys.logical().pendingCommand().type,
              LogicalAxis::CommandType::MoveAbsolute);
    EXPECT_EQ(sys.logical().pendingCommand().moveTarget, 100.0);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.3: Coupled 模式下 MoveRelative X 成功
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, MoveRelativeXInCoupledModeAccepted) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    auto result = sys.moveRelative(AxisId::X, -5.0);

    EXPECT_TRUE(result.accepted) << result.rejectReason;
    EXPECT_EQ(sys.logical().pendingCommand().type,
              LogicalAxis::CommandType::MoveRelative);
    EXPECT_EQ(sys.logical().pendingCommand().moveDelta, -5.0);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.4: Stop 命令在任何状态下都可接受
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, StopAlwaysAccepted) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 先占住命令槽
    sys.moveAbsolute(AxisId::X, 100.0);

    // Stop 不受命令槽限制
    auto result = sys.stop(AxisId::X);

    EXPECT_TRUE(result.accepted);
    EXPECT_EQ(sys.logical().pendingCommand().type,
              LogicalAxis::CommandType::Stop);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.5: 命令槽互斥 — 同类型命令覆盖失败
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, MoveRejectedWhenSlotBusy) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 先占住命令槽
    auto result1 = sys.moveAbsolute(AxisId::X, 100.0);
    ASSERT_TRUE(result1.accepted);

    // 第二个 Move（绝对或相对）应被拒绝
    auto result2 = sys.moveAbsolute(AxisId::X, 200.0);
    EXPECT_FALSE(result2.accepted);
    EXPECT_NE(result2.rejectReason.find("busy"), std::string::npos);

    auto result3 = sys.moveRelative(AxisId::X, 50.0);
    EXPECT_FALSE(result3.accepted);
    EXPECT_NE(result3.rejectReason.find("busy"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.6: Jog 可以覆盖正在执行的 Jog (约束19: TC-6.6)
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, JogCanOverwriteJog) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 先发一个 Jog Forward
    auto result1 = sys.jog(AxisId::X, MotionDirection::Forward);
    ASSERT_TRUE(result1.accepted);
    ASSERT_EQ(sys.logical().pendingCommand().type, LogicalAxis::CommandType::Jog);
    ASSERT_EQ(sys.logical().pendingCommand().jogDirection, MotionDirection::Forward);

    // 发第二个 Jog（不同方向），应覆盖第一个 Jog
    auto result2 = sys.jog(AxisId::X, MotionDirection::Backward);
    EXPECT_TRUE(result2.accepted) << result2.rejectReason;
    EXPECT_EQ(sys.logical().pendingCommand().type, LogicalAxis::CommandType::Jog);
    EXPECT_EQ(sys.logical().pendingCommand().jogDirection, MotionDirection::Backward);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.7: Coupled 模式限位触发展 Jog 被拒绝，反方向允许
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, JogRejectedAtForwardLimitInCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // X1 正向限位触发（通过系统内部轴直接设置）
    sys.x1().setPosLimitActive(true);

    // Forward Jog 被拒绝
    auto result = sys.jog(AxisId::X, MotionDirection::Forward);
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Limit"), std::string::npos);

    // Backward Jog 允许（远离限位）
    auto result2 = sys.jog(AxisId::X, MotionDirection::Backward);
    EXPECT_TRUE(result2.accepted) << result2.rejectReason;
}

// ═══════════════════════════════════════════════════════════
// TS5.4.8: Coupled 模式下 Move 在限位触发后一律拒绝（约束15）
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, MoveRejectedAtForwardLimitInCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    sys.x1().setPosLimitActive(true);

    // MoveAbsolute 被拒绝（约束15：限位触发 → 所有 Move 非法）
    auto result = sys.moveAbsolute(AxisId::X, -100.0);  // 反方向也拒绝
    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Limit"), std::string::npos);

    // MoveRelative 也被拒绝
    auto result2 = sys.moveRelative(AxisId::X, -50.0);
    EXPECT_FALSE(result2.accepted);
    EXPECT_NE(result2.rejectReason.find("Limit"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.9: 报警状态下所有运动命令被拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, AllMotionsRejectedInAlarmCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    sys.x1().setAlarmed(true);

    EXPECT_FALSE(sys.jog(AxisId::X, MotionDirection::Forward).accepted);
    EXPECT_FALSE(sys.moveAbsolute(AxisId::X, 100.0).accepted);
    EXPECT_FALSE(sys.moveRelative(AxisId::X, 50.0).accepted);

    // Stop 仍应成功
    EXPECT_TRUE(sys.stop(AxisId::X).accepted);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.10: Decoupled 模式下 Jog X1/X2
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, JogX1InDecoupledModeAccepted) {
    auto sys = createSystem();
    ASSERT_FALSE(sys.isCoupled());

    auto result = sys.jog(AxisId::X1, MotionDirection::Forward);

    // Decoupled 模式 Jog 物理轴应被接受
    // Jog 不写入 LogicalAxis 命令槽，直接标记物理轴
    EXPECT_TRUE(result.accepted) << result.rejectReason;
}

TEST_F(GantrySystemMotionTest, JogXRejectedInDecoupledMode) {
    auto sys = createSystem();
    ASSERT_FALSE(sys.isCoupled());

    auto result = sys.jog(AxisId::X, MotionDirection::Forward);

    EXPECT_FALSE(result.accepted);
    EXPECT_NE(result.rejectReason.find("Mode"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TS5.4.11: Coupled 模式下 Move 写入 command slot 后 Stop 清除
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemMotionTest, StopAfterMoveSetsStopSlot) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 先发 MoveAbsolute
    auto result1 = sys.moveAbsolute(AxisId::X, 100.0);
    ASSERT_TRUE(result1.accepted);

    // Stop → 覆盖命令槽
    auto result2 = sys.stop(AxisId::X);
    EXPECT_TRUE(result2.accepted);
    EXPECT_EQ(sys.logical().pendingCommand().type,
              LogicalAxis::CommandType::Stop);
}
