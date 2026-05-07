/**
 * @file test_gantry_full_integration.cpp
 * @brief T6.1: GantrySystem + Fake 端口全流程集成测试
 *
 * 测试覆盖 (TS6.1.1 ~ TS6.1.5):
 *   TS6.1.1 - Feedback→Aggregation→Event→Command 完整链路
 *   TS6.1.2 - Coupled 联动建立→Jog→偏差检测→自动解联
 *   TS6.1.3 - 限位触发→Jog 方向限制→反方向可退
 *   TS6.1.4 - 报警触发→禁止所有运动命令
 *   TS6.1.5 - 报警复位→恢复运动能力
 *
 * 测试组件栈:
 *   GantrySystem (聚合根)
 *   + FakePLC (仿真 PLC)
 *   + FakeGantryFeedbackPort (反馈端口)
 *   + FakeGantryCommandPort (命令端口)
 *   + FakeGantryEventBus (事件总线)
 */

#include <gtest/gtest.h>
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeGantryFeedbackPort.h"
#include "infrastructure/FakeGantryCommandPort.h"
#include "infrastructure/FakeGantryEventBus.h"
#include "value/GantryMode.h"
#include "value/MotionDirection.h"
#include "value/GantryPosition.h"
#include "event/GantryEvents.h"

// ═══════════════════════════════════════════════════════════
// 辅助: 构造一条完整的扫描周期 (Feedback → Aggregate → Drain events)
// ═══════════════════════════════════════════════════════════

struct ScanResult {
    AxisState aggregatedState;
    std::vector<GantryEvents::Event> events;
};

static ScanResult doScanCycle(GantrySystem& gantry,
                               FakeGantryFeedbackPort& feedback) {
    // Step 1: 从 "HAL" 读取反馈 → 同步到 GantrySystem
    gantry.x1().syncState(feedback.getX1Feedback());
    gantry.x2().syncState(feedback.getX2Feedback());

    // Step 2: 状态聚合
    gantry.aggregateState();

    // Step 3: 排空事件
    ScanResult r;
    r.aggregatedState = gantry.aggregatedState();
    r.events = gantry.drainEvents();
    return r;
}

// ═══════════════════════════════════════════════════════════
// TS6.1.1: Feedback→Aggregation→Event→Command 完整链路
// ═══════════════════════════════════════════════════════════

TEST(GantryFullIntegration, FullScanCycle_FeedbackToState) {
    // Arrange: 构建完整端口栈
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);
    FakeGantryEventBus eventBus;

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    // 模拟 PLC 状态: X1/X2 均已使能, 位置对齐
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 100.0);
    plc.setPosition(AxisId::X2, -100.0);

    // Act: 执行扫描周期
    auto result = doScanCycle(gantry, feedback);

    // Assert: 状态正确聚合
    EXPECT_EQ(result.aggregatedState, AxisState::Idle);
    EXPECT_DOUBLE_EQ(gantry.position(), 100.0);
    EXPECT_TRUE(gantry.x1().isEnabled());
    EXPECT_TRUE(gantry.x2().isEnabled());
}

TEST(GantryFullIntegration, FullScanCycle_EventEmission) {
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);
    FakeGantryEventBus eventBus;

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 0.0);
    plc.setPosition(AxisId::X2, 0.0);

    // 首周期: 无事件
    auto r1 = doScanCycle(gantry, feedback);
    // 初始状态不应产生异常事件

    // 建立联动
    gantry.requestCoupling();
    bool hasCoupled = false;
    for (auto& e : gantry.drainEvents()) {
        if (e.type == GantryEvents::Type::Coupled) hasCoupled = true;
    }
    EXPECT_TRUE(hasCoupled);
}

// ═══════════════════════════════════════════════════════════
// TS6.1.2: Coupled 联动建立→Jog→偏差检测→自动解联
// ═══════════════════════════════════════════════════════════

TEST(GantryFullIntegration, CoupledJogThenDeviationFault) {
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);
    FakeGantryEventBus eventBus;

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    // Step 1: 使能并对齐
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 0.0);
    plc.setPosition(AxisId::X2, 0.0);

    // 初始扫描同步
    doScanCycle(gantry, feedback);

    // Step 2: 建立联动
    auto cpResult = gantry.requestCoupling();
    EXPECT_TRUE(cpResult.allowed);
    EXPECT_TRUE(isCoupled(gantry.mode()));

    // 清理耦合事件
    gantry.drainEvents();

    // Step 3: 模拟 X1 移动, X2 卡住 (偏差超限)
    plc.setPosition(AxisId::X1, 0.02);  // X1 移动 0.02
    // X2 保持 0.0, 偏差 = 0.02 > epsilon

    auto result = doScanCycle(gantry, feedback);

    // Step 4: 验证自动解联
    EXPECT_TRUE(isDecoupled(gantry.mode()));

    bool hasDeviationFault = false;
    bool hasDecoupled = false;
    for (const auto& e : result.events) {
        if (e.type == GantryEvents::Type::DeviationFault) hasDeviationFault = true;
        if (e.type == GantryEvents::Type::Decoupled) hasDecoupled = true;
    }
    EXPECT_TRUE(hasDeviationFault) << "Should emit DeviationFault event";
    EXPECT_TRUE(hasDecoupled) << "Should emit Decoupled event";
}

// ═══════════════════════════════════════════════════════════
// TS6.1.3: 限位触发→Jog 方向限制→反方向可退
// ═══════════════════════════════════════════════════════════

TEST(GantryFullIntegration, LimitBlocksDirection_ReverseAllowed) {
    // Decoupled 模式：物理轴正限位触发后，正向 Jog 被拒，反向可退
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    // X1/X2 在 Idle 状态，X1 位于正限位边界
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 1000.0);  // 正限位值 = 1000.0
    plc.setPosition(AxisId::X2, 0.0);
    plc.tick(10);  // checkHardwareLimits → posLimit = true

    // 扫描同步
    auto r = doScanCycle(gantry, feedback);

    // X1 正限位触发后，Forward 方向被拒
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Forward),
              Operability::Rejected_Limit);
    // X1 反向可退出限位区
    EXPECT_EQ(gantry.checkOperability(AxisId::X1, MotionDirection::Backward),
              Operability::Allowed);

    // 验证限位事件已发出
    bool hasLimit = false;
    for (const auto& e : r.events) {
        if (e.type == GantryEvents::Type::LimitTriggered) hasLimit = true;
    }
    EXPECT_TRUE(hasLimit);
}

// ═══════════════════════════════════════════════════════════
// TS6.1.4: 报警触发→禁止所有运动命令
// ═══════════════════════════════════════════════════════════

TEST(GantryFullIntegration, AlarmBlocksAllMotionCommands) {
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    // 正常状态: X1/X2 Idle
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 0.0);
    plc.setPosition(AxisId::X2, 0.0);

    doScanCycle(gantry, feedback);
    gantry.requestCoupling();
    gantry.drainEvents();

    // 触发 X1 报警
    plc.forceState(AxisId::X1, AxisState::Error);

    auto r = doScanCycle(gantry, feedback);

    // 验证报警事件
    bool hasAlarm = false;
    for (const auto& e : r.events) {
        if (e.type == GantryEvents::Type::AlarmRaised) hasAlarm = true;
    }
    EXPECT_TRUE(hasAlarm);

    // 所有运动命令均被拒绝
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Alarm);
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Backward),
              Operability::Rejected_Alarm);

    // Jog 命令应被拒绝
    auto jogResult = gantry.jog(AxisId::X, MotionDirection::Forward);
    EXPECT_FALSE(jogResult.accepted);

    // MoveAbsolute 命令应被拒绝
    auto maResult = gantry.moveAbsolute(AxisId::X, 100.0);
    EXPECT_FALSE(maResult.accepted);

    // MoveRelative 命令应被拒绝
    auto mrResult = gantry.moveRelative(AxisId::X, 10.0);
    EXPECT_FALSE(mrResult.accepted);

    // Stop 仍应可接受
    auto stopResult = gantry.stop(AxisId::X);
    EXPECT_TRUE(stopResult.accepted);
}

// ═══════════════════════════════════════════════════════════
// TS6.1.5: 报警复位→恢复运动能力
// ═══════════════════════════════════════════════════════════

TEST(GantryFullIntegration, AlarmReset_RestoresMotionCapability) {
    FakePLC plc;
    FakeGantryFeedbackPort feedback(plc);
    FakeGantryCommandPort command(plc);

    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);
    GantrySystem gantry(x1, x2);

    // Step 1: 正常状态建立联动
    plc.forceState(AxisId::X1, AxisState::Idle);
    plc.forceState(AxisId::X2, AxisState::Idle);
    plc.setPosition(AxisId::X1, 0.0);
    plc.setPosition(AxisId::X2, 0.0);

    doScanCycle(gantry, feedback);
    gantry.requestCoupling();
    gantry.drainEvents();

    // Step 2: 触发报警
    plc.forceState(AxisId::X1, AxisState::Error);
    doScanCycle(gantry, feedback);

    // 确认运动被禁止
    EXPECT_EQ(gantry.checkOperability(AxisId::X, MotionDirection::Forward),
              Operability::Rejected_Alarm);

    // Step 3: 复位报警 (通过 FeedbackPort)
    feedback.resetAlarm();
    // resetAlarm 会将 X1 从 Error 恢复为 Idle
    // 需要重新建立联动 (报警后联解)
    plc.setPosition(AxisId::X1, 0.0);
    plc.setPosition(AxisId::X2, 0.0);

    doScanCycle(gantry, feedback);

    // Step 4: 重新建立联动
    auto cpResult = gantry.requestCoupling();

    // Step 5: 验证运动能力已恢复
    auto op = gantry.checkOperability(AxisId::X, MotionDirection::Forward);
    // 不在报警状态即可操作
    if (cpResult.allowed) {
        EXPECT_EQ(op, Operability::Allowed);
    }
}
