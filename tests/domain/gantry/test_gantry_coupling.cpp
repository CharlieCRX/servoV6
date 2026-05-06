/**
 * @file test_gantry_coupling.cpp
 * @brief 龙门联动建立与维持约束测试 (约束12-14)
 *
 * 覆盖设计文档约束：
 *   约束12 - 联动是"状态申请"，不是强制切换
 *   约束13 - 联动建立条件
 *   约束14 - 联动期间持续约束
 *
 * 测试用例映射：
 *   TC-4.1  ~ TC-4.7  → requestCoupling() 条件测试
 *   TC-4.8  ~ TC-4.10 → checkSyncMaintenance() 持续同步测试
 *
 * 测试组件：GantrySystem (聚合根，已内聚联动逻辑)
 */

#include <gtest/gtest.h>
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "value/GantryMode.h"
#include "value/CouplingCondition.h"
#include "value/PositionConsistency.h"
#include "event/GantryEvents.h"

// ═══════════════════════════════════════════════════════════
// 辅助：构造处于就绪状态的 GantrySystem
// ═══════════════════════════════════════════════════════════

static GantrySystem makeReadyGantry(double x1Pos = 100.0, double x2Pos = -100.0) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setPosition(x1Pos);
    x2.setPosition(x2Pos);
    return GantrySystem(x1, x2);
}

// ═══════════════════════════════════════════════════════════
// TC-4.1: 联动是"状态申请"，不是强制切换 (约束12)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, DecoupledByDefault) {
    // 约束12: 默认处于分动模式，联动必须显式申请
    GantrySystem gantry = makeReadyGantry();
    EXPECT_TRUE(isDecoupled(gantry.mode()));
}

TEST(CouplingTest, CouplingRequestedEventEmitted) {
    // 约束12: requestCoupling() 发布 CouplingRequested 事件
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    auto events = gantry.drainEvents();
    bool hasRequested = false;
    bool hasCoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::CouplingRequested) hasRequested = true;
        if (e.type == GantryEvents::Type::Coupled) hasCoupled = true;
    }
    EXPECT_TRUE(hasRequested);
    EXPECT_TRUE(hasCoupled);  // 因为条件满足
}

TEST(CouplingTest, CannotForceCouplingWithoutChecking) {
    // 约束12: 无法绕过检查直接设置联动模式
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    // X1 未使能 → 不应进入 Coupled
    x2.setEnabled(true);
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_TRUE(isDecoupled(gantry.mode()));  // 模式仍为 Decoupled
}

// ═══════════════════════════════════════════════════════════
// TC-4.2: X1 未使能 → 拒绝联动 (约束13.1)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, X1NotEnabled_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x2.setEnabled(true);
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("X1"), std::string::npos);
    EXPECT_TRUE(isDecoupled(gantry.mode()));
}

// ═══════════════════════════════════════════════════════════
// TC-4.3: X2 未使能 → 拒绝联动 (约束13.1)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, X2NotEnabled_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("X2"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-4.4: 有报警 → 拒绝联动 (约束13.2)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, AlarmActive_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setAlarmed(true);  // X1 报警
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Alarm"), std::string::npos);
}

TEST(CouplingTest, X2Alarm_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x2.setAlarmed(true);  // X2 报警
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Alarm"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-4.5: 有限位 → 拒绝联动 (约束13.3)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, LimitTriggered_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setPosLimitActive(true);  // 正向限位
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Limit"), std::string::npos);
}

TEST(CouplingTest, NegativeLimit_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x2.setNegLimitActive(true);  // 负向限位
    x1.setPosition(100.0);
    x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("Limit"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-4.6: 位置不一致 → 拒绝联动 (约束13.4)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, PositionInconsistent_Rejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setPosition(100.0);
    x2.setPosition(-90.0);  // 偏差 10mm > epsilon 0.01
    GantrySystem gantry(x1, x2);
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("deviation"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-4.7: 所有条件满足 → 接受联动 (约束13)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, AllConditionsMet_Accepted) {
    GantrySystem gantry = makeReadyGantry();
    auto result = gantry.requestCoupling();
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(result.failReason.empty());
    EXPECT_TRUE(isCoupled(gantry.mode()));  // 模式已切换为 Coupled
}

TEST(CouplingTest, CoupledEventEmitted) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    auto events = gantry.drainEvents();
    bool hasCoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Coupled) hasCoupled = true;
    }
    EXPECT_TRUE(hasCoupled);
}

TEST(CouplingTest, AtZeroPosition_CanCouple) {
    // 双轴在零点时也应该能建联动
    GantrySystem gantry = makeReadyGantry(0.0, 0.0);
    auto result = gantry.requestCoupling();
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(isCoupled(gantry.mode()));
}

// ═══════════════════════════════════════════════════════════
// TC-4.8: 运行中偏差超阈值 → 触发 DeviationFault (约束14)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, DeviationExceeded_TriggersFault) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    EXPECT_TRUE(isCoupled(gantry.mode()));

    // 模拟偏差超限：X2 偏移 0.02mm (epsilon = 0.01)
    gantry.x2().setPosition(-99.98);
    // aggregateState 会自动调用 checkSyncMaintenance
    gantry.aggregateState();

    auto events = gantry.drainEvents();
    bool hasDeviationFault = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) hasDeviationFault = true;
    }
    EXPECT_TRUE(hasDeviationFault);
}

// ═══════════════════════════════════════════════════════════
// TC-4.9: DeviationFault → 强制退出联动 (约束14)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, DeviationFault_ForcesDecoupling) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    EXPECT_TRUE(isCoupled(gantry.mode()));

    // 偏差超限
    gantry.x2().setPosition(-99.98);
    gantry.aggregateState();

    EXPECT_TRUE(isDecoupled(gantry.mode()));  // 已被强制退出

    // 验证 Decoupled 事件
    auto events = gantry.drainEvents();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) hasDecoupled = true;
    }
    EXPECT_TRUE(hasDecoupled);
}

TEST(CouplingTest, DeviationFault_CannotReCoupleWithoutRealign) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    gantry.x2().setPosition(-99.98);
    gantry.aggregateState();  // 触发 DeviationFault → Decoupled

    // 偏差仍存在 → 再次申请联动应被拒绝
    auto result = gantry.requestCoupling();
    EXPECT_FALSE(result.allowed);
    EXPECT_NE(result.failReason.find("deviation"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════
// TC-4.10: 偏差在阈值内 → 正常 (约束14)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, DeviationWithinRange_NoFault) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    EXPECT_TRUE(isCoupled(gantry.mode()));

    // 微小偏差 (0.005mm < epsilon 0.01)
    gantry.x2().setPosition(-99.995);
    gantry.aggregateState();

    auto events = gantry.drainEvents();
    bool hasFault = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) hasFault = true;
    }
    EXPECT_FALSE(hasFault);
    EXPECT_TRUE(isCoupled(gantry.mode()));  // 仍保持联动
}

TEST(CouplingTest, DeviationExactlyAtEpsilon_NoFault) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();

    // 偏差精确等于 epsilon 0.01 (使用 <= 判断)
    gantry.x2().setPosition(-99.99);  // 偏差 = 100 + (-99.99) = 0.01
    gantry.aggregateState();

    EXPECT_TRUE(isCoupled(gantry.mode()));  // 边界值上应保持
}

// ═══════════════════════════════════════════════════════════
// 联动恢复测试 (补充)
// ═══════════════════════════════════════════════════════════

TEST(CouplingTest, CanReCoupleAfterRealign) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    gantry.x2().setPosition(-99.98);
    gantry.aggregateState();  // DeviationFault → Decoupled

    // 重新对齐
    gantry.x2().setPosition(-100.0);
    auto result = gantry.requestCoupling();
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(isCoupled(gantry.mode()));
}

TEST(CouplingTest, ExplicitDecoupling_ReturnsDecoupled) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    EXPECT_TRUE(isCoupled(gantry.mode()));

    gantry.requestDecoupling("User request");
    EXPECT_TRUE(isDecoupled(gantry.mode()));

    auto events = gantry.drainEvents();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) {
            hasDecoupled = true;
            EXPECT_NE(e.description.find("User request"), std::string::npos);
        }
    }
    EXPECT_TRUE(hasDecoupled);
}

TEST(CouplingTest, DecoupledMode_DecouplingIsNoOp) {
    // 已经在分动模式，再 Request Decoupling 不产生事件
    GantrySystem gantry = makeReadyGantry();
    gantry.requestDecoupling();
    EXPECT_TRUE(isDecoupled(gantry.mode()));
    auto events = gantry.drainEvents();
    EXPECT_TRUE(events.empty());
}
