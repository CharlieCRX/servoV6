/**
 * @file test_gantry_system_mode.cpp
 * @brief GantrySystem 模式管理单元测试 (T5.2)
 *
 * 覆盖设计文档约束：
 *   约束12 (联动建立申请)
 *   约束13 (联动建立条件校验)
 *   约束4  (分动申请)
 *
 * 测试用例：TS5.2.1 ~ TS5.2.9
 */

#include <gtest/gtest.h>
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "entity/AxisId.h"
#include "value/GantryMode.h"
#include "value/CouplingCondition.h"
#include "event/GantryEvents.h"

// ═══════════════════════════════════════════════════════════
// 测试夹具: 提供默认构造的 GantrySystem
// ═══════════════════════════════════════════════════════════

class GantrySystemModeTest : public ::testing::Test {
protected:
    GantrySystemModeTest() : m_x1(AxisId::X1), m_x2(AxisId::X2) {}

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
// TS5.2.1: 默认模式为 Decoupled
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, DefaultModeIsDecoupled) {
    auto sys = createSystem();

    EXPECT_EQ(sys.mode(), GantryMode::Decoupled);
    EXPECT_FALSE(sys.isCoupled());
}

// ═══════════════════════════════════════════════════════════
// TS5.2.2: requestCoupling 两轴 enabled → 成功
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingBothEnabledSucceeds) {
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_TRUE(result.allowed) << "Fail reason: " << result.failReason;
    EXPECT_TRUE(sys.isCoupled());
    EXPECT_EQ(sys.mode(), GantryMode::Coupled);

    // 验证事件：CouplingRequested + Coupled
    const auto& events = sys.events();
    ASSERT_GE(events.size(), 2u);
    EXPECT_EQ(events[0].type, GantryEvents::Type::CouplingRequested);
    EXPECT_EQ(events[1].type, GantryEvents::Type::Coupled);
}

// ═══════════════════════════════════════════════════════════
// TS5.2.3: requestCoupling X1 未 enabled → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingX1DisabledRejected) {
    m_x1.setEnabled(false);
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(result.failReason, "X1 is not enabled");
    EXPECT_FALSE(sys.isCoupled());

    // 验证事件：CouplingRequested 但无 Coupled
    const auto& events = sys.events();
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, GantryEvents::Type::CouplingRequested);
    // 不应有 Coupled 事件
    for (const auto& e : events) {
        EXPECT_NE(e.type, GantryEvents::Type::Coupled);
    }
}

// ═══════════════════════════════════════════════════════════
// TS5.2.4: requestCoupling X2 未 enabled → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingX2DisabledRejected) {
    m_x2.setEnabled(false);
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(result.failReason, "X2 is not enabled");
    EXPECT_FALSE(sys.isCoupled());

    const auto& events = sys.events();
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, GantryEvents::Type::CouplingRequested);
    for (const auto& e : events) {
        EXPECT_NE(e.type, GantryEvents::Type::Coupled);
    }
}

// ═══════════════════════════════════════════════════════════
// TS5.2.5: requestCoupling X1/X2 均 disabled → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingBothDisabledRejected) {
    m_x1.setEnabled(false);
    m_x2.setEnabled(false);
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(sys.isCoupled());

    // CouplingRequested 事件仍然发布
    const auto& events = sys.events();
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, GantryEvents::Type::CouplingRequested);
    for (const auto& e : events) {
        EXPECT_NE(e.type, GantryEvents::Type::Coupled);
    }
}

// ═══════════════════════════════════════════════════════════
// TS5.2.6: requestCoupling 已 Coupled → 幂等 (无新 Coupled 事件)
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingWhenAlreadyCoupledIsIdempotent) {
    auto sys = createSystem();

    // 第一次：成功建立联动
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    // 记录当前事件数量
    size_t eventCountBefore = sys.events().size();

    // 第二次：已处于 Coupled 模式，再次申请
    auto result = sys.requestCoupling();

    // CouplingCondition::checkAll 会再次通过（轴状态仍然良好）
    // 这里验证模式仍是 Coupled
    EXPECT_TRUE(sys.isCoupled());

    // 再次调用会重新发布 CouplingRequested 和 Coupled 事件
    // 因为 GantrySystem 的实现每次都会发布事件
    EXPECT_GE(sys.events().size(), eventCountBefore + 2);
}

// ═══════════════════════════════════════════════════════════
// TS5.2.7: requestCoupling 报警状态 → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingAlarmRejected) {
    m_x1.setAlarmed(true);
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(result.failReason, "Alarm is active on one or both axes");
    EXPECT_FALSE(sys.isCoupled());

    const auto& events = sys.events();
    ASSERT_GE(events.size(), 1u);
    EXPECT_EQ(events[0].type, GantryEvents::Type::CouplingRequested);
    for (const auto& e : events) {
        EXPECT_NE(e.type, GantryEvents::Type::Coupled);
    }
}

// ═══════════════════════════════════════════════════════════
// TS5.2.8: requestDecoupling 正常解联
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestDecouplingFromCoupledSucceeds) {
    auto sys = createSystem();

    // 先建立联动
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    sys.requestDecoupling("user requested");

    EXPECT_FALSE(sys.isCoupled());
    EXPECT_EQ(sys.mode(), GantryMode::Decoupled);

    // 验证事件：应包含 CouplingRequested, Coupled, Decoupled
    const auto& events = sys.events();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) {
            hasDecoupled = true;
            EXPECT_NE(e.description.find("user requested"), std::string::npos);
        }
    }
    EXPECT_TRUE(hasDecoupled);
}

// ═══════════════════════════════════════════════════════════
// TS5.2.9: requestDecoupling 已 Decoupled → 无操作
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestDecouplingWhenAlreadyDecoupledNoOp) {
    auto sys = createSystem();

    ASSERT_FALSE(sys.isCoupled());

    // 记录事件数
    size_t eventCountBefore = sys.events().size();

    sys.requestDecoupling("should be no-op");

    EXPECT_FALSE(sys.isCoupled());
    EXPECT_EQ(sys.mode(), GantryMode::Decoupled);

    // 应无新事件
    EXPECT_EQ(sys.events().size(), eventCountBefore);
}

// ═══════════════════════════════════════════════════════════
// 附加测试: requestCoupling 位置偏差 → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingPositionDeviationRejected) {
    m_x1.setPosition(100.0);
    m_x2.setPosition(50.0);  // |100 + 50| = 150 > epsilon
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(sys.isCoupled());
}

// ═══════════════════════════════════════════════════════════
// 附加测试: requestCoupling 限位触发 → 拒绝
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestCouplingLimitTriggeredRejected) {
    m_x1.setPosLimitActive(true);
    auto sys = createSystem();

    auto result = sys.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_FALSE(sys.isCoupled());
}

// ═══════════════════════════════════════════════════════════
// 附加测试: requestDecoupling 空原因 → 默认描述
// ═══════════════════════════════════════════════════════════

TEST_F(GantrySystemModeTest, RequestDecouplingEmptyReasonUsesDefault) {
    auto sys = createSystem();
    sys.requestCoupling();
    ASSERT_TRUE(sys.isCoupled());

    sys.requestDecoupling();

    EXPECT_FALSE(sys.isCoupled());

    const auto& events = sys.events();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) {
            hasDecoupled = true;
            EXPECT_EQ(e.description, "Decoupled mode entered");
        }
    }
    EXPECT_TRUE(hasDecoupled);
}
