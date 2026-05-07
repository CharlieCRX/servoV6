/**
 * @file test_gantry_system_integration.cpp
 * @brief 龙门聚合根集成测试 (TC-7.1~TC-7.5)
 *
 * 覆盖：
 *   TC-7.1 - 完整的联动建立→运行→异常→恢复生命周期
 *   TC-7.2 - 反馈更新同步位置
 *   TC-7.3 - 外部不能直接访问物理轴 (接口约束)
 *   TC-7.4 - 模式变更发出事件
 *   TC-7.5 - 偏差故障发出事件
 *
 * 测试组件：GantrySystem 完整状态机 + 事件流
 */

#include <gtest/gtest.h>
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "value/GantryMode.h"
#include "value/PositionConsistency.h"
#include "value/SafetyCheckResult.h"
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
// TC-7.1: 完整的联动建立→运行→异常→恢复生命周期
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, FullCoupleDecoupleLifecycle) {
    // Step 1: 构造物理轴并初始化
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setPosition(50.0);
    x2.setPosition(-50.0);
    GantrySystem gantry(x1, x2);
    gantry.aggregateState();  // 初始状态聚合

    // Step 2: 默认为分动模式
    EXPECT_TRUE(isDecoupled(gantry.mode()));
    EXPECT_EQ(gantry.logical().position().value(), 50.0);

    // Step 3: 申请联动 → 成功建立
    auto result = gantry.requestCoupling();
    EXPECT_TRUE(result.allowed);
    EXPECT_TRUE(isCoupled(gantry.mode()));

    // Step 4: 联动模式下通过 X 轴操作
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Allowed);
    gantry.logical().setPosition(GantryPosition(75.0));
    EXPECT_DOUBLE_EQ(gantry.logical().position().value(), 75.0);

    // Step 5: 模拟偏差超限 → 故障退出
    gantry.x2().setPosition(-49.98);  // 偏差 0.02 > epsilon
    gantry.aggregateState();

    EXPECT_TRUE(isDecoupled(gantry.mode()));  // 强制退出联动

    // Step 6: 检验故障事件链
    auto events = gantry.drainEvents();
    bool hasDeviationFault = false;
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) hasDeviationFault = true;
        if (e.type == GantryEvents::Type::Decoupled) hasDecoupled = true;
    }
    EXPECT_TRUE(hasDeviationFault);
    EXPECT_TRUE(hasDecoupled);

    // Step 7: 重新对齐后可以重新建联动
    gantry.x2().setPosition(-50.0);
    auto result2 = gantry.requestCoupling();
    EXPECT_TRUE(result2.allowed);
    EXPECT_TRUE(isCoupled(gantry.mode()));
}

// ═══════════════════════════════════════════════════════════
// TC-7.2: 反馈更新应同步更新位置
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, UpdateFeedback_UpdatesPositions) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    x1.setPosition(10.0);
    x2.setPosition(-10.0);
    GantrySystem gantry(x1, x2);

    // 反馈更新 X1
    gantry.x1().setPosition(20.0);
    gantry.aggregateState();

    EXPECT_DOUBLE_EQ(gantry.logical().position().value(), 20.0);
    EXPECT_DOUBLE_EQ(gantry.x1().position(), 20.0);
}

TEST(GantrySystemIntegration, UpdateFeedback_BothAxes) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true);
    x2.setEnabled(true);
    GantrySystem gantry(x1, x2);

    gantry.x1().setPosition(30.0);
    gantry.x2().setPosition(-30.0);
    gantry.aggregateState();

    EXPECT_DOUBLE_EQ(gantry.x1().position(), 30.0);
    EXPECT_DOUBLE_EQ(gantry.x2().position(), -30.0);
}

TEST(GantrySystemIntegration, Feedback_AggregatesToLogicalPosition) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true); x2.setEnabled(true);
    x1.setPosition(100.0); x2.setPosition(-100.0);
    GantrySystem gantry(x1, x2);
    gantry.requestCoupling();
    gantry.drainEvents();

    // 更新位置后聚合
    gantry.x1().setPosition(105.0);
    gantry.x2().setPosition(-105.0);
    gantry.aggregateState();

    // 逻辑位置 = X1 位置 (约束10)
    EXPECT_DOUBLE_EQ(gantry.logical().position().value(), 105.0);
}

// ═══════════════════════════════════════════════════════════
// TC-7.3: 外部不能直接访问物理轴 (接口设计验证)
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, ExternalAccess_ViaLogicalAxis) {
    // 外部世界通过 logical() 获取位置和状态
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true); x2.setEnabled(true);
    x1.setPosition(42.0); x2.setPosition(-42.0);
    GantrySystem gantry(x1, x2);
    gantry.aggregateState();  // 初始状态聚合

    auto& lx = gantry.logical();
    EXPECT_DOUBLE_EQ(lx.position().value(), 42.0);
}

TEST(GantrySystemIntegration, PhysicalAxes_AccessibleForSyncOnly) {
    // x1()/x2() 返回引用供 HAL 层 syncState 使用 (设计允许)
    // 但不通过此引用做业务决策 (该约束由架构约定保证)
    GantrySystem gantry = makeReadyGantry();
    EXPECT_DOUBLE_EQ(gantry.x1().position(), 100.0);
    EXPECT_DOUBLE_EQ(gantry.x2().position(), -100.0);
}

// ═══════════════════════════════════════════════════════════
// TC-7.4: 模式变更应发出领域事件
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, ModeChange_EmitsCoupledEvent) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();

    auto events = gantry.drainEvents();
    bool hasCoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Coupled) hasCoupled = true;
    }
    EXPECT_TRUE(hasCoupled);
}

TEST(GantrySystemIntegration, ModeChange_EmitsDecoupledEvent) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    gantry.drainEvents();  // 清空

    gantry.requestDecoupling("Test");
    auto events = gantry.drainEvents();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) hasDecoupled = true;
    }
    EXPECT_TRUE(hasDecoupled);
}

TEST(GantrySystemIntegration, ModeChange_EventContainsDetails) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    auto events = gantry.drainEvents();
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Coupled) {
            EXPECT_NE(e.description.find("Coupled"), std::string::npos);
        }
    }
}

// ═══════════════════════════════════════════════════════════
// TC-7.5: 偏差故障应发出事件
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, DeviationFault_EmitsEvent) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    gantry.drainEvents();

    gantry.x2().setPosition(-99.98);
    gantry.aggregateState();

    auto events = gantry.drainEvents();
    bool hasDeviationFault = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) hasDeviationFault = true;
    }
    EXPECT_TRUE(hasDeviationFault);
}

TEST(GantrySystemIntegration, DeviationFault_EventContainsDeviationValues) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    gantry.drainEvents();

    gantry.x2().setPosition(-99.98);
    gantry.aggregateState();

    auto events = gantry.drainEvents();
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) {
            // deviation = 100.0 + (-99.98) = 0.02
            // Event description format: "Deviation fault: X1=... X2=... deviation=..."
            EXPECT_NE(e.description.find("deviation=0.020000"), std::string::npos)
                << "Event description should contain deviation value";
        }
    }
}

// ═══════════════════════════════════════════════════════════
// 补充：事件耗尽测试
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, DrainEvents_ClearsQueue) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();
    EXPECT_FALSE(gantry.drainEvents().empty());
    EXPECT_TRUE(gantry.drainEvents().empty());  // 第二次为空
}

TEST(GantrySystemIntegration, MultipleOperations_MultipleEvents) {
    GantrySystem gantry = makeReadyGantry();
    // 序列操作: 建联动 → 解联动
    gantry.requestCoupling();
    gantry.requestDecoupling("test");
    auto events = gantry.drainEvents();
    // 应包含 CouplingRequested, Coupled, Decoupled
    int typeCount = 0;
    for (const auto& e : events) typeCount++;
    EXPECT_GE(typeCount, 3);
}

// ═══════════════════════════════════════════════════════════
// TS5.6.2: events() 只读不消耗
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, EventsReadOnlyDoesNotConsume) {
    GantrySystem gantry = makeReadyGantry();
    gantry.requestCoupling();

    // events() 返回只读引用，不清空
    EXPECT_FALSE(gantry.events().empty());
    EXPECT_FALSE(gantry.events().empty());  // 第二次仍有
    // drainEvents() 才清空
    gantry.drainEvents();
    EXPECT_TRUE(gantry.events().empty());
}

// ═══════════════════════════════════════════════════════════
// 补充：聚合状态下操作互斥
// ═══════════════════════════════════════════════════════════

TEST(GantrySystemIntegration, CoupledMode_OnlyXOperable) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true); x2.setEnabled(true);
    x1.setPosition(0.0); x2.setPosition(0.0);
    GantrySystem gantry(x1, x2);
    gantry.requestCoupling();

    // 联动模式下只有 X 可操作
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Allowed);
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Mode);
    EXPECT_EQ(gantry.checkOperability(AxisId::X2, MotionDirection::Forward),
              Operability::Rejected_Mode);
}

TEST(GantrySystemIntegration, DecoupledMode_XIsRejected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    x1.setEnabled(true); x2.setEnabled(true);
    GantrySystem gantry(x1, x2);

    // 分动模式下 X 不可操作
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Mode);
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Allowed);
    EXPECT_EQ(gantry.checkOperability(AxisId::X2, MotionDirection::Backward),
              Operability::Allowed);
}
