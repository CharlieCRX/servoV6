// ============================================================================
// test_physical_axis_commissioning_service.cpp —— 阶段3：单轴调试服务单元测试
// ============================================================================
// 覆盖 Step 3.0 写入闸门（slot 白名单 / 断开 / 急停 / 报警 / Revision 变化）与
// Step 3.2/3.3/3.8 的服务级流程：
//   - slot 0、1 通过，slot 2、13、15 拒绝（SlotNotAllowed）；
//   - 断开 / 急停 / 报警 / Revision 变化分别返回对应原因；
//   - 使能写入落在物理 slot（绝不改写到 slot13，避开龙门逻辑轴路由）；
//   - 参数写入/恢复做读回确认（成功与失败两路径）；
//   - 急停触发 M224 / 解除 M225 记录正确。
// 使用 FakePlcRuntimeGateway（纯内存高层假实现）离线测试，不接触真实 PLC。
// ============================================================================
#include <cstdint>
#include <vector>

#include "application_vnext/commissioning/PhysicalAxisCommissioningService.h"
#include "gtest/gtest.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace {

using application_vnext::commissioning::CommissioningGateReason;
using application_vnext::commissioning::CommissioningOperation;
using application_vnext::commissioning::PhysicalAxisCommissioningService;
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeAxisSnapshot;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

// 构造“闸门可通过”的默认假网关：已连接 + 有效拓扑 + 可信运行 + 无急停。
void makeReady(FakePlcRuntimeGateway& g) {
    g.setConnected(true);
    g.setTopologySnapshot(makeValidTopologySnapshot());
    g.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
    plc_vnext::contracts::SafetySnapshot s;
    s.trusted = true;
    s.emergencyStop = false;
    s.releaseRequest = false;
    g.setSafetySnapshot(s);
}

// 让指定槽位出现报警的运行快照。
plc_vnext::contracts::RuntimeSnapshot makeRuntimeWithAlarm(int slot) {
    auto r = makeTrustedRuntimeSnapshot();
    auto ax = r.axes[static_cast<std::size_t>(slot)];
    ax.alarmWord = 0x0001;
    r.axes[static_cast<std::size_t>(slot)] = ax;
    return r;
}

PlcAxisSlot slot(int v) { return PlcAxisSlot::tryCreate(v).value(); }

// ---- Step 3.0 写入闸门 ----
TEST(CommissioningServiceGate, Slot23AllowedSlotOthersRejected) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    EXPECT_TRUE(svc.evaluateGate(slot(2), CommissioningOperation::Enable).allow());
    EXPECT_TRUE(svc.evaluateGate(slot(3), CommissioningOperation::Enable).allow());
    // slot 0/1 为 X1/X2 龙门成员，本阶段禁用。
    EXPECT_EQ(svc.evaluateGate(slot(0), CommissioningOperation::Enable).reason,
              CommissioningGateReason::SlotNotAllowed);
    EXPECT_EQ(svc.evaluateGate(slot(1), CommissioningOperation::Enable).reason,
              CommissioningGateReason::SlotNotAllowed);
    EXPECT_EQ(svc.evaluateGate(slot(13), CommissioningOperation::Enable).reason,
              CommissioningGateReason::SlotNotAllowed);
    EXPECT_EQ(svc.evaluateGate(slot(15), CommissioningOperation::Enable).reason,
              CommissioningGateReason::SlotNotAllowed);
}

TEST(CommissioningServiceGate, DisconnectedRejects) {
    FakePlcRuntimeGateway g; makeReady(g);
    g.setConnected(false);
    PhysicalAxisCommissioningService svc(g);
    EXPECT_EQ(svc.evaluateGate(slot(2), CommissioningOperation::Enable).reason,
              CommissioningGateReason::Disconnected);
}

TEST(CommissioningServiceGate, EmergencyStopRejectsOrdinaryOps) {
    FakePlcRuntimeGateway g; makeReady(g);
    plc_vnext::contracts::SafetySnapshot s;
    s.trusted = true;
    s.emergencyStop = true;
    g.setSafetySnapshot(s);
    PhysicalAxisCommissioningService svc(g);
    EXPECT_EQ(svc.evaluateGate(slot(2), CommissioningOperation::Enable).reason,
              CommissioningGateReason::EmergencyStop);
    EXPECT_EQ(svc.evaluateGate(slot(2), CommissioningOperation::Jog).reason,
              CommissioningGateReason::EmergencyStop);
}

TEST(CommissioningServiceGate, AxisAlarmRejects) {
    FakePlcRuntimeGateway g; makeReady(g);
    g.setRuntimeSnapshot(makeRuntimeWithAlarm(2));
    PhysicalAxisCommissioningService svc(g);
    EXPECT_EQ(svc.evaluateGate(slot(2), CommissioningOperation::Enable).reason,
              CommissioningGateReason::AxisAlarm);
}


// ---- Step 3.2 参数写入/恢复（读回确认）----
TEST(CommissioningServiceParameter, VerifyRoundTripSuccess) {
    FakePlcRuntimeGateway g; makeReady(g);
    // 脚本原值 = 测试值，读回一致 → 成功路径。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2] = makeAxisSnapshot(0, /*manualSpeed=*/2.0f, 100.0f, 0.0f, 0.0f, 1, 0, 0);
    g.setRuntimeSnapshot(r);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.verifyParameter(slot(2), PlcAxisCommandKind::SetManualSpeed,
                                       2.0f, /*confirmWrite=*/true);
    EXPECT_TRUE(o.ok);
    EXPECT_FLOAT_EQ(o.originalValue, 2.0f);
    EXPECT_TRUE(o.writtenConfirmed);
    EXPECT_TRUE(o.restoredConfirmed);
    // 应产生 2 次写：写测试值 + 恢复，均落在 slot 0。
    const auto writes = g.writtenAxis();
    ASSERT_EQ(writes.size(), 2u);
    EXPECT_EQ(writes[0].slot.value(), 2);
    EXPECT_EQ(writes[0].cmd.kind, PlcAxisCommandKind::SetManualSpeed);
    EXPECT_EQ(writes[1].slot.value(), 2);
}

TEST(CommissioningServiceParameter, VerifyRoundTripMismatchFails) {
    FakePlcRuntimeGateway g; makeReady(g);
    // 脚本原值 = 10，写测试值 = 2；假网关不随写改变读回 → readBack=10 不一致。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2] = makeAxisSnapshot(0, /*manualSpeed=*/10.0f, 100.0f, 0.0f, 0.0f, 1, 0, 0);
    g.setRuntimeSnapshot(r);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.verifyParameter(slot(2), PlcAxisCommandKind::SetManualSpeed,
                                       2.0f, true);
    EXPECT_FALSE(o.ok);
    EXPECT_FALSE(o.writtenConfirmed);
    EXPECT_TRUE(o.restoredConfirmed);
}

TEST(CommissioningServiceParameter, RejectsWithoutConfirmWrite) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.verifyParameter(slot(2), PlcAxisCommandKind::SetManualSpeed,
                                       2.0f, /*confirmWrite=*/false);
    EXPECT_FALSE(o.ok);
    EXPECT_TRUE(g.writtenAxis().empty());  // 拒绝请求不产生任何写报文
}


// ---- Step 3.3 使能：写物理 slot，绝不改写到 slot13 ----
TEST(CommissioningServiceEnable, WritesPhysicalSlotNotLogical13) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.enableAxis(slot(2), true, true);
    ASSERT_TRUE(o.ok);
    EXPECT_EQ(o.gateReason, CommissioningGateReason::Ok);
    const auto writes = g.writtenAxis();
    ASSERT_FALSE(writes.empty());
    for (const auto& w : writes) {
        EXPECT_NE(w.slot.value(), 13);  // 不经过龙门逻辑轴路由
    }
    EXPECT_EQ(writes[0].slot.value(), 2);
    EXPECT_EQ(writes[0].cmd.kind, PlcAxisCommandKind::EnableAxis);
    EXPECT_TRUE(writes[0].cmd.boolValue);
}

TEST(CommissioningServiceEnable, RejectsWithoutConfirmWrite) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.enableAxis(slot(2), true, false);
    EXPECT_FALSE(o.ok);
    EXPECT_TRUE(g.writtenAxis().empty());
}

// ---- Step 3.8 急停触发/解除 ----
TEST(CommissioningServiceEStop, TriggerWritesM224ReleaseWritesM225) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    ASSERT_TRUE(svc.triggerEmergencyStop(true).ok);
    ASSERT_TRUE(svc.requestReleaseEmergencyStop(true).ok);
    const auto coils = g.emergencyCoilWrites();
    ASSERT_EQ(coils.size(), 2u);
    EXPECT_TRUE(coils[0].trigger);   // 第一次：M224 触发
    EXPECT_FALSE(coils[1].trigger);  // 第二次：M225 解除
}

// ---- Step 3.4 点动（回归：内部使能不应被自身 Busy 自锁）----
TEST(CommissioningServiceJog, Slot2JogForwardSucceeds) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    const auto o = svc.jog(slot(2), /*forward=*/true, /*durationMs=*/80,
                           /*confirmWrite=*/true, /*confirmMotion=*/true,
                           /*heartbeatPeriodMs=*/20);
    EXPECT_TRUE(o.ok) << o.diagnostic;
    EXPECT_EQ(o.gateReason, CommissioningGateReason::Ok);
    EXPECT_FALSE(o.heartbeatTimedOut);
}



}  // namespace


TEST(CommissioningServiceGate, RevisionChangedRejects) {
    FakePlcRuntimeGateway g; makeReady(g);
    PhysicalAxisCommissioningService svc(g);
    // 首次评估记录 Revision=0。
    EXPECT_TRUE(svc.evaluateGate(slot(2), CommissioningOperation::Enable).allow());
    // 拓扑 Revision 变化。
    g.setTopologySnapshot(makeValidTopologySnapshot(/*revision=*/7));
    EXPECT_EQ(svc.evaluateGate(slot(2), CommissioningOperation::Enable).reason,
              CommissioningGateReason::RevisionChanged);
}
