/**
 * @file test_gantry_system_operability.cpp
 * @brief GantrySystem 操作可行性检查单元测试 (T5.3)
 *
 * 覆盖设计文档约束：
 *   约束15 (限位后Jog方向限制)
 *   约束18 (操作目标互斥)
 *   约束19 (命令槽互斥)
 *
 * checkOperability 检查顺序:
 *   1. 模式检查 (约束18)
 *   2. 报警检查 (约束17)
 *   3. 限位检查 (约束15-16)
 *   4. 命令槽检查 (约束19)
 *
 * 测试用例：TS5.3.1 ~ TS5.3.6
 */

#include <gtest/gtest.h>
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "entity/AxisId.h"
#include "value/GantryMode.h"
#include "value/MotionDirection.h"
#include "value/SafetyCheckResult.h"

// ═══════════════════════════════════════════════════════════
// 测试夹具
// ═══════════════════════════════════════════════════════════

class GantrySystemOperabilityTest : public ::testing::Test {
protected:
    GantrySystemOperabilityTest() : m_x1(AxisId::X1), m_x2(AxisId::X2) {}

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
// TS5.3.1: Decoupled 模式下 X1/X2 可操作，X 不可操作
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, DecoupledModeX1X2OperableXRejected) {
    auto sys = createSystem();
    ASSERT_EQ(sys.mode(), GantryMode::Decoupled);

    // X1 可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Allowed);

    // X2 可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Backward),
              Operability::Allowed);

    // X 不可操作（未被模式 target）
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Mode);
}

// ═══════════════════════════════════════════════════════════
// TS5.3.2: Coupled 模式下 X 可操作，X1/X2 不可操作
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, CoupledModeXOperableX1X2Rejected) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // X 可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Allowed);

    // X1 不可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Mode);

    // X2 不可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Forward),
              Operability::Rejected_Mode);
}

// ═══════════════════════════════════════════════════════════
// TS5.3.3: 报警状态拒绝所有操作
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, AlarmRejectsAll) {
    m_x1.setAlarmed(true);
    auto sys = createSystem();
    ASSERT_FALSE(sys.isCoupled());

    // X1 被报警拒绝
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Alarm);

    // X2 被报警拒绝
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Forward),
              Operability::Rejected_Alarm);
}

TEST_F(GantrySystemOperabilityTest, AlarmRejectsInCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // m_x1 报警; 但 sys 持有的是 copy，需要直接操作 sys 内部的轴
    // 通过注入耦合建立后设置报警来测试
    // 由于 GantrySystem 持有 PhysicalAxis 副本，这里我们改为不复制构造
    // 直接在 sys 上操作
    sys.x1().setAlarmed(true);

    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Alarm);
}

// ═══════════════════════════════════════════════════════════
// TS5.3.4: 限位方向检查 — Forward 限位只允许 Backward
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, ForwardLimitAllowsOnlyBackward) {
    m_x1.setPosLimitActive(true);
    auto sys = createSystem();

    // Forward 方向被拒绝
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Limit);

    // Backward 方向允许（远离限位）
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Backward),
              Operability::Allowed);
}

// ═══════════════════════════════════════════════════════════
// TS5.3.5: 限位方向检查 — Backward 限位只允许 Forward
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, BackwardLimitAllowsOnlyForward) {
    m_x1.setNegLimitActive(true);
    auto sys = createSystem();

    // Backward 方向被拒绝
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Backward),
              Operability::Rejected_Limit);

    // Forward 方向允许（远离限位）
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Allowed);
}

// ═══════════════════════════════════════════════════════════
// TS5.3.6: 命令槽繁忙拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, BusySlotRejectedInCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 先占满命令槽
    std::string err = sys.logical().tryAcceptMoveAbsolute(100.0);
    ASSERT_TRUE(err.empty()) << err;

    // 此时 X 命令槽已被占用
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Busy);
}

TEST_F(GantrySystemOperabilityTest, BusySlotRejectedForJogAfterMove) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 占满命令槽
    std::string err = sys.logical().tryAcceptMoveAbsolute(200.0);
    ASSERT_TRUE(err.empty());

    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Busy);
}

// ═══════════════════════════════════════════════════════════
// 附加测试: decoupled 模式下 X2 限位方向检查
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, DecoupledX2ForwardLimit) {
    m_x2.setPosLimitActive(true);
    auto sys = createSystem();

    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Forward),
              Operability::Rejected_Limit);
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Backward),
              Operability::Allowed);
}

// ═══════════════════════════════════════════════════════════
// 附加测试: Coupled 模式下 X 的限位传播（任一物理轴限位影响 X）
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, CoupledXLimitPropagation) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // X2 正向限位
    sys.x2().setPosLimitActive(true);

    // X 的 Forward 被拒绝
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Limit);

    // X 的 Backward 仍允许
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Backward),
              Operability::Allowed);
}

// ═══════════════════════════════════════════════════════════
// 附加测试: 正常状态下所有目标均可操作
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemOperabilityTest, AllNormalDecoupled) {
    auto sys = createSystem();
    ASSERT_FALSE(sys.isCoupled());

    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Forward), Operability::Allowed);
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Forward), Operability::Allowed);
    EXPECT_EQ(sys.checkOperability(AxisId::X1, MotionDirection::Backward), Operability::Allowed);
    EXPECT_EQ(sys.checkOperability(AxisId::X2, MotionDirection::Backward), Operability::Allowed);
    // X 在 decoupled 下不可操作
    EXPECT_EQ(sys.checkOperability(AxisId::X, MotionDirection::Forward), Operability::Rejected_Mode);
}
