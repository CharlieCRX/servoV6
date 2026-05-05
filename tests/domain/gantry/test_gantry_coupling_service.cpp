/**
 * @file test_gantry_coupling_service.cpp
 * @brief 龙门联动管理服务单元测试 (约束12-14)
 *
 * TDD Red阶段 — 测试先行于实现。
 * 被测组件: domain/service/GantryCouplingService.h (尚未创建)
 *
 * 覆盖设计文档测试用例：
 *   TC-4.1  ~ TC-4.10
 *
 * 约束覆盖：
 *   约束12 — 联动是"状态申请"，不是强制切换
 *   约束13 — 联动建立条件
 *   约束14 — 联动期间持续约束
 */

#include <gtest/gtest.h>
#include "service/GantryCouplingService.h"   // [Red] 尚未创建
#include "entity/GantrySystem.h"
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "value/GantryMode.h"
#include "value/CouplingCondition.h"
#include "value/SafetyCheckResult.h"

// ═══════════════════════════════════════════════════════════
// 辅助: 创建已使能且位置一致的物理轴对
// ═══════════════════════════════════════════════════════════

class GantryCouplingServiceTest : public ::testing::Test {
protected:
    PhysicalAxis createEnabledX1(double pos = 50.0) {
        PhysicalAxis ax(AxisId::X1);
        PhysicalAxisState st;
        st.enabled = true;
        st.position = pos;
        ax.syncState(st);
        return ax;
    }

    PhysicalAxis createEnabledX2(double pos = -50.0) {
        PhysicalAxis ax(AxisId::X2);
        PhysicalAxisState st;
        st.enabled = true;
        st.position = pos;
        ax.syncState(st);
        return ax;
    }

    GantrySystem createReadySystem() {
        auto x1 = createEnabledX1(50.0);
        auto x2 = createEnabledX2(-50.0);
        return GantrySystem(x1, x2);
    }
};

// ═══════════════════════════════════════════════════════════
// 约束12：联动是"状态申请"（TC-4.1）
// ═══════════════════════════════════════════════════════════

TEST_F(GantryCouplingServiceTest, Coupling_IsStateApplication_NotForceSwitch) {
    // 联动服务不强制切换模式，而是验证条件后申请
    auto system = createReadySystem();
    GantryCouplingService service;

    // 服务通过 GantrySystem 的聚合根接口申请联动
    // 不应绕过 GantrySystem 直接修改模式
    auto result = service.requestCoupling(system);
    EXPECT_TRUE(result.isAllowed()) << result.reason();
    EXPECT_TRUE(isCoupled(system.mode()));
}

// ═══════════════════════════════════════════════════════════
// 约束13：联动建立条件 — 逐个条件被拒绝 (TC-4.2~4.7)
// ═══════════════════════════════════════════════════════════

// TC-4.2: X1 未使能 → 拒绝
TEST_F(GantryCouplingServiceTest, Coupling_X1NotEnabled_ShouldReject) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxisState st;
    st.enabled = false;  // X1 未使能
    st.position = 50.0;
    x1.syncState(st);

    auto x2 = createEnabledX2(-50.0);
    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);  // 模式未改变
    EXPECT_NE(result.reason().find("X1"), std::string::npos);
}

// TC-4.3: X2 未使能 → 拒绝
TEST_F(GantryCouplingServiceTest, Coupling_X2NotEnabled_ShouldReject) {
    auto x1 = createEnabledX1(50.0);
    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState st;
    st.enabled = false;  // X2 未使能
    st.position = -50.0;
    x2.syncState(st);

    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);
}

// TC-4.4: 有报警 → 拒绝
TEST_F(GantryCouplingServiceTest, Coupling_Alarm_ShouldReject) {
    auto x1 = createEnabledX1(50.0);
    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState st;
    st.enabled = true;
    st.alarmed = true;  // X2 报警
    st.position = -50.0;
    x2.syncState(st);

    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);
    EXPECT_NE(result.reason().find("Alarm"), std::string::npos);
}

// TC-4.5: 有限位 → 拒绝
TEST_F(GantryCouplingServiceTest, Coupling_LimitTriggered_ShouldReject) {
    auto x1 = createEnabledX1(50.0);
    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState st;
    st.enabled = true;
    st.position = -50.0;
    st.posLimitActive = true;  // X2 正限位
    x2.syncState(st);

    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);
    EXPECT_NE(result.reason().find("Limit"), std::string::npos);
}

// TC-4.6: 位置不一致 → 拒绝
TEST_F(GantryCouplingServiceTest, Coupling_PositionInconsistent_ShouldReject) {
    auto x1 = createEnabledX1(100.0);
    auto x2 = createEnabledX2(-50.0);  // |100 + (-50)| = 50 > 0.01
    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_FALSE(result.isAllowed());
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);
    EXPECT_NE(result.reason().find("deviation"), std::string::npos);
}

// TC-4.7: 所有条件满足 → 接受
TEST_F(GantryCouplingServiceTest, Coupling_AllConditionsSatisfied_ShouldAccept) {
    auto system = createReadySystem();
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    EXPECT_TRUE(result.isAllowed()) << result.reason();
    EXPECT_TRUE(isCoupled(system.mode()));
}

// ═══════════════════════════════════════════════════════════
// 约束14：联动期间持续约束 (TC-4.8~4.10)
// ═══════════════════════════════════════════════════════════

// TC-4.8: 运行中偏差超阈值 → 触发 DeviationFault
TEST_F(GantryCouplingServiceTest, CoupledMode_DeviationExceeded_ShouldTriggerFault) {
    auto system = createReadySystem();
    GantryCouplingService service;

    // 先建立联动
    auto result = service.requestCoupling(system);
    ASSERT_TRUE(result.isAllowed());
    ASSERT_TRUE(isCoupled(system.mode()));

    // 人为制造位置偏差（模拟 X2 滑移）
    PhysicalAxisState driftedState;
    driftedState.enabled = true;
    driftedState.position = -40.0;  // 原来 -50, 现在 -40, 偏差 = |50 + (-40)| = 10
    PhysicalAxis& x2 = const_cast<PhysicalAxis&>(system.x2());
    x2.syncState(driftedState);

    // 检查联动维持
    auto maintenanceResult = service.checkSyncMaintenance(system);
    EXPECT_FALSE(maintenanceResult.allowed);
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);  // 应自动退出联动
}

// TC-4.9: DeviationFault → 强制退出联动 + 发布事件
TEST_F(GantryCouplingServiceTest, DeviationFault_ShouldForceDecoupling) {
    auto system = createReadySystem();
    GantryCouplingService service;

    // 建立联动
    auto result = service.requestCoupling(system);
    ASSERT_TRUE(result.isAllowed());
    ASSERT_TRUE(isCoupled(system.mode()));

    // 制造偏差
    PhysicalAxisState driftedState;
    driftedState.enabled = true;
    driftedState.position = -30.0;  // 偏差 20
    PhysicalAxis& x2 = const_cast<PhysicalAxis&>(system.x2());
    x2.syncState(driftedState);

    // 检查联动维持 — 应强制解联
    auto maintenanceResult = service.checkSyncMaintenance(system);
    EXPECT_FALSE(maintenanceResult.allowed);
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);

    // 验证发布了 DeviationFault 事件
    auto events = system.drainEvents();
    bool hasDeviationFault = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::DeviationFault) {
            hasDeviationFault = true;
            break;
        }
    }
    EXPECT_TRUE(hasDeviationFault);
}

// TC-4.10: 偏差在阈值内 → 正常
TEST_F(GantryCouplingServiceTest, CoupledMode_DeviationWithinRange_ShouldNotFault) {
    auto system = createReadySystem();
    GantryCouplingService service;

    auto result = service.requestCoupling(system);
    ASSERT_TRUE(result.isAllowed());
    ASSERT_TRUE(isCoupled(system.mode()));

    // 微小位置变化（在允许范围内）
    PhysicalAxisState newState;
    newState.enabled = true;
    newState.position = -49.995;  // 偏差 0.005 < 0.01
    PhysicalAxis& x2 = const_cast<PhysicalAxis&>(system.x2());
    x2.syncState(newState);

    // 检查联动维持 — 应通过
    auto maintenanceResult = service.checkSyncMaintenance(system);
    EXPECT_TRUE(maintenanceResult.allowed);
    EXPECT_TRUE(isCoupled(system.mode()));  // 不应退出联动
}

// ═══════════════════════════════════════════════════════════
// 解联操作测试
// ═══════════════════════════════════════════════════════════

TEST_F(GantryCouplingServiceTest, Decoupling_FromCoupledState) {
    auto system = createReadySystem();
    GantryCouplingService service;

    service.requestCoupling(system);
    ASSERT_TRUE(isCoupled(system.mode()));

    service.requestDecoupling(system, "User request");
    EXPECT_TRUE(isDecoupled(system.mode()));

    // 验证发布了解联事件
    auto events = system.drainEvents();
    bool hasDecoupled = false;
    for (const auto& e : events) {
        if (e.type == GantryEvents::Type::Decoupled) {
            hasDecoupled = true;
            break;
        }
    }
    EXPECT_TRUE(hasDecoupled);
}

TEST_F(GantryCouplingServiceTest, Decoupling_FromAlreadyDecoupled_IsNoop) {
    auto system = createReadySystem();
    GantryCouplingService service;

    ASSERT_TRUE(isDecoupled(system.mode()));
    service.requestDecoupling(system, "Redundant");
    EXPECT_TRUE(isDecoupled(system.mode()));
}

// ═══════════════════════════════════════════════════════════
// 条件验证独立测试
// ═══════════════════════════════════════════════════════════

TEST_F(GantryCouplingServiceTest, ValidateConditions_FromReadySystem) {
    auto system = createReadySystem();
    GantryCouplingService service;

    auto conditions = service.validateConditions(system);
    EXPECT_TRUE(conditions.x1Enabled);
    EXPECT_TRUE(conditions.x2Enabled);
    EXPECT_TRUE(conditions.noAlarm);
    EXPECT_TRUE(conditions.noLimit);
    EXPECT_TRUE(conditions.positionConsistent);
    EXPECT_TRUE(conditions.allSatisfied());
}

TEST_F(GantryCouplingServiceTest, ValidateConditions_WithIssues) {
    auto x1 = createEnabledX1(50.0);
    PhysicalAxis x2(AxisId::X2);
    PhysicalAxisState st;
    st.enabled = true;
    st.alarmed = true;
    st.position = -100.0;  // 位置也不一致
    x2.syncState(st);

    GantrySystem system(x1, x2);
    GantryCouplingService service;

    auto conditions = service.validateConditions(system);
    EXPECT_TRUE(conditions.x1Enabled);
    EXPECT_TRUE(conditions.x2Enabled);
    EXPECT_FALSE(conditions.noAlarm);
    EXPECT_TRUE(conditions.noLimit);
    EXPECT_FALSE(conditions.positionConsistent);
    EXPECT_FALSE(conditions.allSatisfied());

    // 诊断信息应包含未满足的原因
    std::string reasons = conditions.unsatisfiedReasons();
    EXPECT_FALSE(reasons.empty());
    EXPECT_NE(reasons.find("Alarm"), std::string::npos);
}
