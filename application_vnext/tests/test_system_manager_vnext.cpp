// ============================================================================
// test_system_manager_vnext.cpp —— P5 app: SystemManager 迁移到 domain_vnext 集成测试
// ============================================================================
// P5 验证点（设计稿 §8「应用层编译 + 集成测试」）：经 FakePlcRuntimeGateway
// 注入已解码快照，覆盖完整链路：
//   - boot：TopologySnapshot -> AxisSystem 动态建轴 / 分组 / 使能入口路由；
//   - poll：RuntimeSnapshot -> 实体反馈 + 龙门状态注入；
//   - 单轴用例：enable / jog / moveAbs（先写目标再触发）-> writeAxis 断言；
//   - 使能入口路由（§4.6a）：X1/X2 龙门成员 Enable* 改写为逻辑轴槽位 13；
//   - 未 boot / 未注册轴 / 通讯失败等错误聚合；
//   - 急停五态流程；龙门 requestCouple 事务提交与准入拒绝。
// ============================================================================
#include <memory>
#include <variant>

#include <gtest/gtest.h>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/SystemManagerVnext.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace application_vnext {
namespace {

using application_vnext::AppNotBooted;
using application_vnext::AxisNotFound;
using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::SystemManagerVnext;
using domain_vnext::model::AxisFunction;
using plc_vnext::contracts::GantryCommandKind;
using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::contracts::PlcGroupIndex;
using plc_vnext::contracts::RuntimeSnapshot;
using plc_vnext::contracts::TopologyGroup;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeGantryStatusSnapshot;
using plc_vnext::fake::makeRole;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

PlcAxisSlot slotOf(int v) { return *PlcAxisSlot::tryCreate(v); }

/// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。
plc_vnext::contracts::TopologySnapshot makeSixAxisTopology() {
    auto snap = makeValidTopologySnapshot();
    TopologyGroup ga;
    ga.valid = true;
    ga.hmiVisible = true;
    ga.groupCode = 0;
    ga.roles = {
        makeRole(true, 0, 1, 0, 0, 1),      // X1
        makeRole(true, 1, 2, 0, 0, 2),      // X2
        makeRole(true, 2, 3, 0, 0, 3),      // Y
        makeRole(true, 3, 4, 0, 0, 3),      // Z
        makeRole(true, 4, 5, 0, 0, 4),      // R
        makeRole(true, 13, 0, 2, 0, 5),     // X 逻辑轴
        makeRole(false, -1, 0, 0, 0, 0),
        makeRole(false, -1, 0, 0, 0, 0),
    };
    snap.groups[0] = ga;
    return snap;
}

/// 基于可信运行快照，把 A 组龙门状态替换为指定状态。
RuntimeSnapshot runtimeWithGantry(int16_t state, bool x1InGear, bool x2InGear,
                                  bool readyToCouple) {
    auto rt = makeTrustedRuntimeSnapshot();
    auto g = makeGantryStatusSnapshot(0, state, 0, 0, x1InGear, x2InGear);
    g.readyToCouple = readyToCouple;
    rt.gantry[0] = g;
    return rt;
}

class SystemManagerVnextTest : public ::testing::Test {
protected:
    void SetUp() override {
        adapter_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        mgr_ = std::make_unique<SystemManagerVnext>(*adapter_);
    }

    void bootWith(plc_vnext::contracts::TopologySnapshot topo,
                  RuntimeSnapshot rt = makeTrustedRuntimeSnapshot()) {
        gw_.setTopologySnapshot(std::move(topo));
        gw_.setRuntimeSnapshot(std::move(rt));
        ASSERT_TRUE(mgr_->boot());
    }
    void bootValid() { bootWith(makeValidTopologySnapshot()); }
    void bootSixAxis() { bootWith(makeSixAxisTopology()); }

    FakePlcRuntimeGateway gw_;
    std::unique_ptr<PlcRuntimeDriverAdapter> adapter_;
    std::unique_ptr<SystemManagerVnext> mgr_;
};

// ---------- boot / poll ----------

TEST_F(SystemManagerVnextTest, Boot_RegistersAxesFromTopology) {
    bootValid();
    EXPECT_TRUE(mgr_->booted());
    // makeValidTopologySnapshot 仅 A 组绑定 X1(0)/X2(1)/X(13)。
    EXPECT_EQ(mgr_->system().totalAxes(), 3u);
    EXPECT_TRUE(mgr_->system().group(PlcGroupIndex(0)).isReady());
    EXPECT_FALSE(mgr_->system().group(PlcGroupIndex(1)).isReady());  // B 组禁用
}

TEST_F(SystemManagerVnextTest, Boot_InvalidTopology_SystemNotReady) {
    auto topo = makeValidTopologySnapshot();
    topo.header.configValid = false;
    topo.header.configErrorCode = 0x08;
    bootWith(std::move(topo));
    // configValid=false 属快照内容，boot 本身无降级；是否开放控制由 isReady 表达。
    EXPECT_FALSE(mgr_->system().isReady());
}

TEST_F(SystemManagerVnextTest, Poll_InjectAxisFeedback) {
    bootValid();
    // boot 已 poll 一次注入 makeTrustedRuntimeSnapshot；X 位于 slot13，manualSpeed=130。
    auto* x = mgr_->system().registry().find(slotOf(13));
    ASSERT_NE(x, nullptr);
    EXPECT_FLOAT_EQ(x->feedback().manualSpeed, 130.f);
}

// ---------- 单轴用例与使能入口路由 ----------

TEST_F(SystemManagerVnextTest, EnableAxis_GantryMember_RoutesToLogicalSlot) {
    bootSixAxis();
    const auto r = mgr_->enableAxis(AxisFunction::X1, true);
    ASSERT_TRUE(appResultOk(r));
    const auto written = gw_.writtenAxis();
    ASSERT_EQ(written.size(), 1u);
    // §4.6a：X1 龙门成员使能改写为逻辑轴槽位 13。
    EXPECT_EQ(written[0].slot, slotOf(13));
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::EnableAxis);
    EXPECT_TRUE(written[0].cmd.boolValue);
}

TEST_F(SystemManagerVnextTest, EnableMotor_IndependentAxis_WritesOwnSlot) {
    bootSixAxis();
    const auto r = mgr_->enableMotor(AxisFunction::Y, true);
    ASSERT_TRUE(appResultOk(r));
    const auto written = gw_.writtenAxis();
    ASSERT_EQ(written.size(), 1u);
    // 独立轴 Y 写自身槽位 2。
    EXPECT_EQ(written[0].slot, slotOf(2));
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::EnableMotor);
}

TEST_F(SystemManagerVnextTest, Jog_WriteCoilLevel) {
    bootSixAxis();
    ASSERT_TRUE(appResultOk(mgr_->jog(AxisFunction::X, /*forward=*/true, /*on=*/true)));
    const auto written = gw_.writtenAxis();
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].slot, slotOf(13));
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::JogForward);
    EXPECT_TRUE(written[0].cmd.boolValue);
}

// §4.2 解耦铁律：setAbsTarget 与 triggerAbsMove 是独立接口——设置目标不触发，
// 触发也不依赖先前的 set 调用。下例验证二者可分别、独立提交。
TEST_F(SystemManagerVnextTest, SetAbsTarget_WritesTargetOnly_NoAutoTrigger) {
    bootSixAxis();
    ASSERT_TRUE(appResultOk(mgr_->setAbsTarget(AxisFunction::X, 250.f)));
    const auto written = gw_.writtenAxis();
    // 只写目标，绝不附带触发。
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::SetAbsTarget);
    EXPECT_FLOAT_EQ(written[0].cmd.realValue, 250.f);
}

TEST_F(SystemManagerVnextTest, TriggerAbsMove_IndependentOfSet) {
    bootSixAxis();
    // 未先 setAbsTarget 也能独立触发（触发仅提交 TriggerAbsMove 一笔）。
    ASSERT_TRUE(appResultOk(mgr_->triggerAbsMove(AxisFunction::X)));
    const auto written = gw_.writtenAxis();
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::TriggerAbsMove);
}

TEST_F(SystemManagerVnextTest, SetRelTarget_Then_TriggerRelMove_InOrder) {
    bootSixAxis();
    // 相对定位同样解耦：设置距离与相对触发是独立接口。
    ASSERT_TRUE(appResultOk(mgr_->setRelTarget(AxisFunction::X, 50.f)));
    ASSERT_TRUE(appResultOk(mgr_->triggerRelMove(AxisFunction::X)));
    const auto written = gw_.writtenAxis();
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[0].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::SetRelTarget);
    EXPECT_FLOAT_EQ(written[0].cmd.realValue, 50.f);
    EXPECT_EQ(written[1].cmd.kind, plc_vnext::contracts::PlcAxisCommandKind::TriggerRelMove);
}

// ---------- 错误聚合 ----------

TEST_F(SystemManagerVnextTest, NotBooted_ReturnsAppNotBooted) {
    const auto r = mgr_->enableMotor(AxisFunction::X, true);
    EXPECT_FALSE(appResultOk(r));
    EXPECT_NE(appErrorOf<AppNotBooted>(r), nullptr);
}

TEST_F(SystemManagerVnextTest, AxisNotFound_ReturnsError) {
    bootValid();  // 该拓扑未绑定 Y
    const auto r = mgr_->enableMotor(AxisFunction::Y, true);
    EXPECT_FALSE(appResultOk(r));
    EXPECT_NE(appErrorOf<AxisNotFound>(r), nullptr);
}

TEST_F(SystemManagerVnextTest, WriteAxisFailure_ReturnsCommFailed) {
    bootSixAxis();
    gw_.scriptWriteAxisFailure(plc_vnext::contracts::CommunicationResult::Status::Timeout,
                               "mock busy");
    const auto r = mgr_->jog(AxisFunction::X, true, true);
    EXPECT_FALSE(appResultOk(r));
    EXPECT_NE(appErrorOf<CommFailed>(r), nullptr);
}

// ---------- 安全（急停五态） ----------

TEST_F(SystemManagerVnextTest, EmergencyStop_FullFlow) {
    bootValid();
    // 先同步：未急停 -> Running。
    mgr_->applyEmergencyStopFeedback(false);
    EXPECT_FALSE(mgr_->isSystemLocked());

    ASSERT_TRUE(appResultOk(mgr_->requestEmergencyStop()));
    ASSERT_TRUE(mgr_->hasPendingEStop());
    const auto cmd = mgr_->popPendingEStop();
    EXPECT_TRUE(cmd.active);  // EStopCommand{true} -> M224 锁存

    mgr_->applyEmergencyStopFeedback(true);  // PLC 确认急停完成
    EXPECT_TRUE(mgr_->isSystemLocked());
    EXPECT_TRUE(mgr_->system().safety().isEmergencyStopped());

    // 幂等：已在急停态，再次请求被拒。
    EXPECT_FALSE(appResultOk(mgr_->requestEmergencyStop()));

    ASSERT_TRUE(appResultOk(mgr_->requestReleaseEmergencyStop()));
    EXPECT_FALSE(mgr_->popPendingEStop().active);  // EStopCommand{false} -> M225
    mgr_->applyEmergencyStopFeedback(false);
    EXPECT_FALSE(mgr_->isSystemLocked());
}

// ---------- 龙门联动 ----------

TEST_F(SystemManagerVnextTest, GantryCouple_SubmitsRequestTransaction) {
    bootValid();
    // 将 A 组龙门状态改写为 Decoupled + ReadyToCouple，再注入。
    gw_.setRuntimeSnapshot(runtimeWithGantry(/*state=*/1, false, false, /*readyToCouple=*/true));
    ASSERT_TRUE(mgr_->poll());

    mgr_->applyGantryConfig(PlcGroupIndex(0),
                            domain_vnext::model::GantryParamModel{/*valid=*/true});

    ASSERT_TRUE(appResultOk(mgr_->gantryCouple(PlcGroupIndex(0))));
    const auto subs = gw_.gantrySubmissions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].group, PlcGroupIndex(0));
    EXPECT_EQ(subs[0].req.command, GantryCommandKind::Couple);
    EXPECT_EQ(subs[0].req.requestSeq, 1);  // 领域自增 seq 从 1 起
}

TEST_F(SystemManagerVnextTest, GantryCouple_NotReady_Rejected) {
    bootValid();
    gw_.setRuntimeSnapshot(runtimeWithGantry(/*state=*/1, false, false, /*readyToCouple=*/false));
    ASSERT_TRUE(mgr_->poll());
    mgr_->applyGantryConfig(PlcGroupIndex(0),
                            domain_vnext::model::GantryParamModel{/*valid=*/true});
    const auto r = mgr_->gantryCouple(PlcGroupIndex(0));
    EXPECT_FALSE(appResultOk(r));
    EXPECT_NE(appErrorOf<GantryRequestRejected>(r), nullptr);
    EXPECT_TRUE(gw_.gantrySubmissions().empty());
}

}  // namespace
}  // namespace application_vnext
