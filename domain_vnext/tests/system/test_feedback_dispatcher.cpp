// ============================================================================
// test_feedback_dispatcher.cpp —— P4 system: RuntimeSnapshot -> 实体注入
// ============================================================================
// P4 验证点（设计稿 §8 / §5.1）：RuntimeSnapshot -> 实体注入。覆盖：
//   - dispatch：按已注册槽位注入运行反馈前 7 项（用 plc_vnext::fake 快照夹具）；
//   - dispatchGantry：GantryStatusSnapshot -> 领域联动状态机；
//   - dispatchSafety：急停反馈驱动全局安全状态机（首次同步）；
//   - dispatchParameters：参数区 8~13 注入（plc_vnext AxisParameterSnapshot）。
// ============================================================================
#include <array>

#include <gtest/gtest.h>

#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/model/GantryStatus.h"
#include "domain_vnext/system/AxisSystem.h"
#include "domain_vnext/system/FeedbackDispatcher.h"
#include "domain_vnext/system/SystemBoot.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace domain_vnext::system {
namespace {

using domain_vnext::model::AxisFunction;
using domain_vnext::model::GantryCouplingState;
using plc_vnext::contracts::AxisParameterSnapshot;
using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::fake::makeAxisSnapshot;
using plc_vnext::fake::makeGantryStatusSnapshot;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

PlcAxisSlot slotOf(int v) {
    return *PlcAxisSlot::tryCreate(v);
}

// makeValidTopologySnapshot 绑定：X1->slot0、X2->slot1、X(逻辑轴)->slot13。
TEST(FeedbackDispatcherTest, Dispatch_InjectsAxisRuntimeFeedback) {
    AxisSystem sys;
    SystemBoot::initialize(sys, makeValidTopologySnapshot());

    plc_vnext::contracts::RuntimeSnapshot rt;
    rt.axes[13] = makeAxisSnapshot(13, /*manualSpeed=*/130.f,
                                   /*positioningSpeed=*/320.f,
                                   /*absPosition=*/13000.f,
                                   /*relPosition=*/50.f,
                                   /*motionState=*/4, /*motionLimit=*/0,
                                   /*alarmWord=*/0x1F);
    rt.gantry[0] = makeGantryStatusSnapshot(0);

    FeedbackDispatcher::dispatch(sys, rt);

    auto* x = sys.registry().find(slotOf(13));
    ASSERT_NE(x, nullptr);
    EXPECT_FLOAT_EQ(x->feedback().manualSpeed, 130.f);
    EXPECT_FLOAT_EQ(x->feedback().positioningSpeed, 320.f);
    EXPECT_FLOAT_EQ(x->feedback().absPosition, 13000.f);
    EXPECT_FLOAT_EQ(x->feedback().relPosition, 50.f);
    EXPECT_EQ(x->feedback().motionState, static_cast<int16_t>(4));
    EXPECT_EQ(x->feedback().alarmWord, 0x1Fu);
    EXPECT_TRUE(x->feedback().trusted);
}

TEST(FeedbackDispatcherTest, Dispatch_SkipsUnregisteredSlots) {
    AxisSystem sys;
    SystemBoot::initialize(sys, makeValidTopologySnapshot());
    // 未注册槽位（如 2..12、14..15）不注入，也不崩溃。
    plc_vnext::contracts::RuntimeSnapshot rt;
    for (int i = 0; i < static_cast<int>(plc_vnext::contracts::kRuntimeAxisCount); ++i) {
        rt.axes[static_cast<std::size_t>(i)] = makeAxisSnapshot(i, 1.f);
    }
    rt.gantry[0] = makeGantryStatusSnapshot(0);
    rt.gantry[1] = makeGantryStatusSnapshot(1);

    FeedbackDispatcher::dispatch(sys, rt);

    // X1(slot0) 已注册 -> 注入生效；slot2 未注册 -> 无轴可查（不崩溃、不注入）。
    auto* x1 = sys.registry().find(slotOf(0));
    ASSERT_NE(x1, nullptr);
    EXPECT_FLOAT_EQ(x1->feedback().manualSpeed, 1.f);
    EXPECT_FALSE(sys.registry().find(slotOf(2)));
}

TEST(FeedbackDispatcherTest, DispatchGantry_DrivesCouplingStateMachine) {
    AxisSystem sys;
    SystemBoot::initialize(sys, makeValidTopologySnapshot());
    const auto g0 = plc_vnext::contracts::PlcGroupIndex(0);

    // A 组已联动（State=3），注入 -> 联动状态机进入 Coupled。
    const auto snap = makeGantryStatusSnapshot(0, /*state=*/3, /*ackSeq=*/9,
                                               /*commandResult=*/2,
                                               /*x1InGear=*/true,
                                               /*x2InGear=*/true);
    FeedbackDispatcher::dispatchGantry(sys.group(g0), snap);

    EXPECT_EQ(sys.group(g0).gantryCoupling().state(),
              GantryCouplingState::Coupled);
}

TEST(FeedbackDispatcherTest, DispatchSafety_FirstSyncToRunning) {
    AxisSystem sys;
    // 未急停 -> 首次同步进入 Running。
    FeedbackDispatcher::dispatchSafety(sys, /*plcEmergencyStopped=*/false);
    EXPECT_FALSE(sys.safety().isSystemLocked());
    EXPECT_FALSE(sys.safety().isEmergencyStopped());
}

TEST(FeedbackDispatcherTest, DispatchSafety_FirstSyncToEmergencyStopped) {
    AxisSystem sys;
    // M224 ON -> 首次同步进入 EmergencyStopped，系统锁定。
    FeedbackDispatcher::dispatchSafety(sys, /*plcEmergencyStopped=*/true);
    EXPECT_TRUE(sys.safety().isEmergencyStopped());
    EXPECT_TRUE(sys.safety().isSystemLocked());
}

TEST(FeedbackDispatcherTest, DispatchParameters_InjectsParamZone) {
    AxisSystem sys;
    SystemBoot::initialize(sys, makeValidTopologySnapshot());

    std::array<AxisParameterSnapshot, plc_vnext::contracts::kRuntimeAxisCount> params{};
    auto& p = params[13];
    p.slot = 13;
    p.relZeroRecord = 10.f;
    p.absMoveDistance = 100.f;
    p.relMoveDistance = 200.f;
    p.softNegLimit = -500.f;
    p.softPosLimit = 500.f;
    p.softLimitControl = 0x03u;  // bit0正 bit1负都使能
    p.trusted = true;

    FeedbackDispatcher::dispatchParameters(sys, params);

    auto* x = sys.registry().find(slotOf(13));
    ASSERT_NE(x, nullptr);
    EXPECT_FLOAT_EQ(x->feedback().relZeroRecord, 10.f);
    EXPECT_FLOAT_EQ(x->feedback().absMoveDistance, 100.f);
    EXPECT_FLOAT_EQ(x->feedback().relMoveDistance, 200.f);
    EXPECT_FLOAT_EQ(x->feedback().softNegLimit, -500.f);
    EXPECT_FLOAT_EQ(x->feedback().softPosLimit, 500.f);
    EXPECT_EQ(x->feedback().softLimitControl, 0x03u);
    // 参数区 trusted 与运行反馈 trusted 做与：此处未先注入运行反馈 -> 默认 false。
    EXPECT_FALSE(x->feedback().trusted);
}

TEST(FeedbackDispatcherTest, Dispatch_FromFakePlcRuntimeGateway) {
    // 端到端：经 FakePlcRuntimeGateway 读出 RuntimeSnapshot，注入实体（§5.1 链路）。
    plc_vnext::fake::FakePlcRuntimeGateway gw;
    gw.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
    auto res = gw.readRuntime();
    ASSERT_TRUE(res.hasValue());

    AxisSystem sys;
    SystemBoot::initialize(sys, makeValidTopologySnapshot());
    FeedbackDispatcher::dispatch(sys, *res);

    // X 逻辑轴（SYN0）位于 slot13；makeTrustedRuntimeSnapshot 里 manualSpeed=i*10、
    // absPosition=i*1000、motionState=1。
    auto* x = sys.registry().find(slotOf(13));
    ASSERT_NE(x, nullptr);
    EXPECT_FLOAT_EQ(x->feedback().manualSpeed, 130.f);
    EXPECT_FLOAT_EQ(x->feedback().absPosition, 13000.f);
    EXPECT_EQ(x->feedback().motionState, static_cast<int16_t>(1));
    // A 组龙门已联动（State=3）-> 联动状态机 Coupled。
    EXPECT_EQ(sys.group(plc_vnext::contracts::PlcGroupIndex(0)).gantryCoupling().state(),
              GantryCouplingState::Coupled);
}

}  // namespace
}  // namespace domain_vnext::system
