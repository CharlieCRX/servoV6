/**
 * @file test_gantry_system_aggregation.cpp
 * @brief GantrySystem 状态聚合单元测试 (T5.5)
 *
 * 覆盖设计文档约束：
 *   约束20 (状态聚合：X1/X2 → X)
 *   约束14 (联动维持检查)
 *
 * 聚合规则 (约束20, 优先级: Alarm > Limit > Moving > Idle):
 *   1. 任一轴报警 → X = Error
 *   2. 任一轴限位 → X = Idle (限位阻断)
 *   3. 任一轴运动中 → X = 对应运动状态
 *   4. 双轴 Idle → X = Idle
 *
 * 逻辑位置 (约束10): X.position = X1.pos
 *
 * 测试用例：TS5.5.1 ~ TS5.5.8
 */

#include <gtest/gtest.h>
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "entity/AxisId.h"
#include "value/GantryMode.h"

// ═══════════════════════════════════════════════════════════
// 测试夹具
// ═══════════════════════════════════════════════════════════

class GantrySystemAggregationTest : public ::testing::Test {
protected:
    GantrySystemAggregationTest() : m_x1(AxisId::X1), m_x2(AxisId::X2) {}

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
// TS5.5.1: 双轴 Idle → X = Idle
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, BothIdleAggregatesToIdle) {
    auto sys = createSystem();

    // 无运动、无限位、无报警
    sys.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::Idle);
    EXPECT_FALSE(sys.logical().isMoving());
}

// ═══════════════════════════════════════════════════════════
// TS5.5.2: X1 运动中，X2 Idle → X = 对应运动状态
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, X1JoggingAggregatesToJogging) {
    auto sys = createSystem();

    sys.setX1Motion(LogicalAxis::AggregatedMotion::Jogging);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::Jogging);
    EXPECT_TRUE(sys.logical().isMoving());
}

TEST_F(GantrySystemAggregationTest, X1MovingAbsoluteAggregatesToMovingAbsolute) {
    auto sys = createSystem();

    sys.setX1Motion(LogicalAxis::AggregatedMotion::MovingAbsolute);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::Idle);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::MovingAbsolute);
}

TEST_F(GantrySystemAggregationTest, X2MovingRelativeAggregatesToMovingRelative) {
    auto sys = createSystem();

    sys.setX1Motion(LogicalAxis::AggregatedMotion::Idle);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::MovingRelative);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::MovingRelative);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.3: 报警优先级 > 运动 — X1 Alarm + X2 Jogging → X = Error
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, AlarmOverridesMotion) {
    auto sys = createSystem();

    // X1 报警 + X2 运动中
    sys.x1().setAlarmed(true);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::Jogging);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::Error);
    EXPECT_TRUE(sys.logical().isError());
}

// ═══════════════════════════════════════════════════════════
// TS5.5.4: 限位优先级 > 运动 — X1 Limit + X2 Jogging → X = Idle
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, LimitOverridesMotion) {
    auto sys = createSystem();

    // X1 限位 + X2 运动中
    sys.x1().setPosLimitActive(true);
    sys.setX2Motion(LogicalAxis::AggregatedMotion::Jogging);

    sys.aggregateState();

    EXPECT_EQ(sys.aggregatedState(), AxisState::Idle);
    EXPECT_TRUE(sys.logical().hasActiveLimit());
    EXPECT_FALSE(sys.logical().isMoving());  // 限位阻断运动
}

// ═══════════════════════════════════════════════════════════
// TS5.5.5: 逻辑位置 X = X1.pos (约束10)
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, LogicalPositionEqualsX1Position) {
    auto sys = createSystem();

    sys.x1().setPosition(123.45);
    sys.x2().setPosition(123.45);

    sys.aggregateState();

    EXPECT_DOUBLE_EQ(sys.logical().position().value(), 123.45);
    EXPECT_EQ(sys.position(), 123.45);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.6: Coupled 模式下位置偏差触发 DeviationFault
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, DeviationFaultInCoupledMode) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 制造位置偏差 > epsilon (1000 mm)
    sys.x1().setPosition(0.0);
    sys.x2().setPosition(1000.0);

    sys.aggregateState();

    // 偏差应触发退出 Coupled
    EXPECT_FALSE(sys.isCoupled());
    EXPECT_EQ(sys.mode(), GantryMode::Decoupled);

    // 应产生 DeviationFault 事件
    auto events = sys.drainEvents();
    bool foundDeviation = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) {
            foundDeviation = true;
            break;
        }
    }
    EXPECT_TRUE(foundDeviation);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.7: Coupled 模式下位置无偏差时维持联动
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, SyncMaintainedWhenNoDeviation) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 镜像位置 (X1 ≈ -X2)，偏差 = |50 + (-50)| = 0 ≤ epsilon
    sys.x1().setPosition(50.0);
    sys.x2().setPosition(-50.0);

    sys.aggregateState();

    // 无偏差，应保持 Coupled
    EXPECT_TRUE(sys.isCoupled());
    EXPECT_EQ(sys.mode(), GantryMode::Coupled);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.8: 限位边沿触发事件
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, LimitTriggeredEventOnEdge) {
    auto sys = createSystem();

    // 第一次 aggregateState: 无限位
    sys.aggregateState();
    auto events = sys.drainEvents();  // 清空

    // 触发 X1 正向限位
    sys.x1().setPosLimitActive(true);
    sys.aggregateState();

    events = sys.drainEvents();
    bool foundLimitEvent = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::LimitTriggered) {
            foundLimitEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundLimitEvent);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.9: 报警边沿触发事件
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, AlarmRaisedEventOnEdge) {
    auto sys = createSystem();

    // 第一次 aggregateState: 无报警
    sys.aggregateState();
    auto events = sys.drainEvents();  // 清空

    // 触发 X2 报警
    sys.x2().setAlarmed(true);
    sys.aggregateState();

    events = sys.drainEvents();
    bool foundAlarmEvent = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::AlarmRaised) {
            foundAlarmEvent = true;
            break;
        }
    }
    EXPECT_TRUE(foundAlarmEvent);
}

// ═══════════════════════════════════════════════════════════
// TS5.5.10: 限位持续激活不重复触发事件
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, LimitNotRetriggeredOnSubsequentCycles) {
    auto sys = createSystem();

    // 第一次触发限位
    sys.x1().setPosLimitActive(true);
    sys.aggregateState();
    auto events1 = sys.drainEvents();
    bool firstHadLimit = false;
    for (const auto& e : events1) {
        if (e.type == GantryEvents::Type::LimitTriggered) {
            firstHadLimit = true;
            break;
        }
    }
    EXPECT_TRUE(firstHadLimit);

    // 第二次（限位持续），不应再次触发
    sys.aggregateState();
    auto events2 = sys.drainEvents();
    for (const auto& e : events2) {
        EXPECT_NE(e.type, GantryEvents::Type::LimitTriggered);
    }
}

// ═══════════════════════════════════════════════════════════
// TS5.5.11: Decoupled 模式下不执行联动维持检查
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemAggregationTest, NoSyncCheckInDecoupledMode) {
    auto sys = createSystem();
    // 未联动
    ASSERT_FALSE(sys.isCoupled());

    // 制造大偏差（但不应触发 DeviationFault，因为未耦合）
    sys.x1().setPosition(0.0);
    sys.x2().setPosition(9999.0);

    sys.aggregateState();

    auto events = sys.drainEvents();
    for (const auto& e : events) {
        EXPECT_NE(e.type, GantryEvents::Type::DeviationFault);
    }
}
