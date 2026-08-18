// ============================================================================
// test_motion_control_service.cpp -- Phase 1: MotionControlService skeleton tests
// ============================================================================
// Per <MotionControlService -- unified control coordination layer phase doc> Phase 1
// acceptance:
//   - concurrent submit() all enqueue and are queryable (thread-safe);
//   - tick() empty run no crash, publishes first ControlStateSnapshot;
//   - global lock only released after boot succeeded + first trusted read; without boot
//     even trusted runtime/safety must stay locked;
//   - safety read failure / disconnect -> globallyLocked_=true and snapshot reflects it;
//   - readRuntime() called exactly once per tick (fake counter), no duplicate Modbus read;
//   - urgent estop/release handled before read, not dropped by TTL, and state reflects
//     the CommunicationResult (Accepted / CommitUncertain / Failed).
// Uses FakeControlRuntime (IControlRuntime fake) + PlcRuntimeDriverAdapter over
// FakePlcRuntimeGateway (IPlcDriver source for boot/topology).
// ============================================================================
#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "application_vnext/tests/fake/FakeControlRuntime.h"
#include "domain_vnext/model/GantryParam.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace application_vnext::control {
namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using plc_vnext::contracts::CommunicationResult;
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::SafetySnapshot;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeRole;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

class MotionControlServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        // Script a valid topology so the service can boot on first tick, plus a healthy
        // trusted baseline for tick-based tests.
        gw_.setTopologySnapshot(makeValidTopologySnapshot());
        runtime_.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
        SafetySnapshot safety;
        safety.trusted = true;
        safety.emergencyStop = false;
        runtime_.setSafetySnapshot(safety);
        runtime_.setConnected(true);
    }

    std::unique_ptr<MotionControlService> makeService() {
        return std::make_unique<MotionControlService>(*driver_, runtime_);
    }

    FakePlcRuntimeGateway          gw_;
    std::unique_ptr<PlcRuntimeDriverAdapter> driver_;
    FakeControlRuntime             runtime_;
};

ControlCommand startRelMove() {
    ControlCommand cmd;
    cmd.source = ControlSource::Udp;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    cmd.target.function = domain_vnext::model::AxisFunction::Y;
    cmd.action = ControlAction::StartRelMove;
    return cmd;
}

ControlCommand emergencyStopCmd() {
    ControlCommand estop;
    estop.source = ControlSource::Ui;
    estop.action = ControlAction::EmergencyStop;
    return estop;
}
// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。
// 默认 makeValidTopologySnapshot 仅绑定 X1/X2/X；仲裁测试需要普通轴 Y。
plc_vnext::contracts::TopologySnapshot makeSixAxisTopology() {
    auto snap = makeValidTopologySnapshot();
    plc_vnext::contracts::TopologyGroup ga;
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

ControlCommand startYRel() {
    auto cmd = startRelMove();
    cmd.source = ControlSource::Udp;
    cmd.target.function = domain_vnext::model::AxisFunction::Y;
    cmd.motion = MotionRequest{150.0f, 50.0f};
    return cmd;
}

ControlCommand startYJogForward(ControlSource src) {
    ControlCommand jog;
    jog.source = src;
    jog.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    jog.target.function = domain_vnext::model::AxisFunction::Y;
    jog.action = ControlAction::StartJogForward;
    return jog;
}
ControlCommand startXJogForward(ControlSource src) {
    ControlCommand jog;
    jog.source = src;
    jog.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    jog.target.function = domain_vnext::model::AxisFunction::X;  // 龙门逻辑轴（A 组=slot13）
    jog.action = ControlAction::StartJogForward;
    return jog;
}



bool wroteAxis(const FakePlcRuntimeGateway& gw, PlcAxisCommandKind kind, bool level) {
    for (const auto& w : gw.writtenAxis()) {
        if (w.cmd.kind == kind && w.cmd.boolValue == level) return true;
    }
    return false;
}

// 把 Y(slot2) motionState 置为 2（电机使能空闲），驱动服务使点动会话真正进入 Jogging：
// tick1 建会话（PostEnableDelay）-> sleep 过 0.4s -> tick2 到 IssuingJog -> tick3 到 Jogging。
// 前提：已 gw_.setTopologySnapshot(makeSixAxisTopology())。
std::string startJogToJogging(MotionControlService& svc, FakeControlRuntime& rt,
                              ControlSource src) {
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].motionState = 2;
    rt.setRuntimeSnapshot(r);
    const auto id = svc.submit(startYJogForward(src));
    svc.tick();   // 建会话 -> PostEnableDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(450));  // 过 PostEnableDelay(0.4s)
    svc.tick();   // PostEnableDelay -> IssuingJog
    svc.tick();   // IssuingJog -> Jogging
    return id;
}

// ---- Phase 7：龙门生命周期夹具 ----

domain_vnext::model::GantryParamModel validGantryConfig() {
    domain_vnext::model::GantryParamModel cfg;
    cfg.valid = true;   // ConfigValid（D1600 参数区）是 requestCouple 的准入来源
    return cfg;
}

// 设置 A 组龙门运行时反馈（经 IControlRuntime 注入服务），并同步逻辑轴 slot13 motionState。
// 字段对齐 GantryStatusSnapshot（State=1已解除 / 2建立中 / 3已联动 / 5故障）。
void setGantryRuntime(FakeControlRuntime& rt, int16_t ms, int32_t ackSeq,
                      int16_t state, int16_t step, int16_t result, int16_t err,
                      bool x1, bool x2, bool logical, bool member) {
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[13].motionState = ms;
    auto& s = r.gantry[0];
    s.state = state; s.internalStep = step; s.commandResult = result;
    s.commandErrorCode = err; s.ackSeq = ackSeq;
    s.x1InGear = x1; s.x2InGear = x2;
    s.logicalControlAllowed = logical; s.memberControlAllowed = member;
    s.readyToCouple = (state == 1);
    s.readyToDecouple = (state == 3);
    s.fault = false; s.faultCode = 0; s.trusted = true;
    rt.setRuntimeSnapshot(r);
}

// 构造 A 组龙门生命周期命令（建立 / 解除），target 为逻辑轴 X。
ControlCommand gantryCmd(ControlAction a) {
    ControlCommand c;
    c.source = ControlSource::Ui;
    c.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    c.target.function = domain_vnext::model::AxisFunction::X;
    c.action = a;
    return c;
}

// 驱动服务把「GantryEnableAndCouple」跑完到 Succeeded（经统一 tick 逐帧推进）。
// 使能序列用 ms=2 + state==1（readyToCouple）反馈；反复 tick 直到 fake 网关收到 Couple 提交
// （即已进入 WaitCoupleFinal，requestCouple 在 state==1 下被接受），随后注入 coupled
// （State3/Step80/CommandResult2/AckSeq1）推进到 Ready -> tickSessions 收口 Succeeded。
// 反馈注入与策略 tick 同 tick：SubmitCouple 前必须保持 state==1，进入 WaitCoupleFinal 后再注入 coupled。
std::string driveCoupleToSucceeded(MotionControlService& svc, FakeControlRuntime& rt,
                                   FakePlcRuntimeGateway& gw) {
    setGantryRuntime(rt, 2, 0, 1, 10, 0, 0, false, false, false, true);  // ms=2, decoupled
    const auto id = svc.submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    for (int i = 0; i < 40 && gw.gantrySubmissions().empty(); ++i) svc.tick();
    setGantryRuntime(rt, 2, 1, 3, 80, 2, 0, true, true, true, false);    // coupled ackSeq1
    for (int i = 0; i < 5; ++i) svc.tick();    // WaitCoupleFinal -> Ready -> Succeeded
    return id;
}

// 先让服务 boot（sys.reset() 会清空龙门耦合状态机的 configValid），随后注入有效龙门参数。
// 必须在 boot 之后注入，否则 submit 前注入会被 boot 重置（Phase 7 龙门准入来源）。
void bootAndConfigureGantry(MotionControlService& svc) {
    svc.tick();   // boot：空队列，readTopology -> bootFromTopology
    svc.applyGantryConfig(plc_vnext::contracts::PlcGroupIndex(0), validGantryConfig());
}



// ---------- command queue + submit ----------

TEST_F(MotionControlServiceTest, SubmitReturnsQueuedOperationId) {
    auto svc = makeService();
    const std::string id = svc->submit(startRelMove());

    EXPECT_FALSE(id.empty());
    EXPECT_EQ(svc->queuedCount(), 1u);

    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->operationId, id);
    EXPECT_EQ(op->state, OperationState::Queued);
    EXPECT_EQ(op->source, ControlSource::Udp);
    EXPECT_EQ(op->kind, OperationKind::Positioning);
    EXPECT_EQ(op->axis, "A.Y");
}

TEST_F(MotionControlServiceTest, OperationIdPrefixesPerSource) {
    auto svc = makeService();
    ControlCommand ui = startRelMove();
    ui.source = ControlSource::Ui;
    EXPECT_EQ(svc->submit(ui).rfind("ui-", 0), 0u);

    ControlCommand udp = startRelMove();
    udp.source = ControlSource::Udp;
    EXPECT_EQ(svc->submit(udp).rfind("udp-", 0), 0u);

    ControlCommand maint = startRelMove();
    maint.source = ControlSource::Maintenance;
    EXPECT_EQ(svc->submit(maint).rfind("maint-", 0), 0u);
}

// 注：本测试只验证「并发提交均可入队且均可查询、队列数量正确」，不承诺跨线程的
// 确定顺序（无 global barrier，顺序由调度决定）。
TEST_F(MotionControlServiceTest, ConcurrentSubmitAllEnqueuedAndQueryable) {
    auto svc = makeService();
    constexpr int kThreads = 4;
    constexpr int kEach = 50;
    std::vector<std::string> ids;
    std::mutex idsMtx;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < kEach; ++i) {
                const std::string id = svc->submit(startRelMove());
                std::lock_guard<std::mutex> lock(idsMtx);
                ids.push_back(id);
            }
        });
    }
    for (auto& th : threads) th.join();

    EXPECT_EQ(svc->queuedCount(), static_cast<std::size_t>(kThreads * kEach));
    for (const auto& id : ids) {
        const auto op = svc->queryOperation(id);
        ASSERT_TRUE(op.has_value());
        EXPECT_EQ(op->state, OperationState::Queued);
    }
}

// ---------- tick skeleton + boot gating ----------

TEST_F(MotionControlServiceTest, TickEmptyRunPublishesFirstSnapshot) {
    auto svc = makeService();
    ASSERT_NO_THROW(svc->tick());

    const auto snap = svc->store().snapshot();
    EXPECT_TRUE(snap.connection.connected);
    EXPECT_TRUE(snap.safety.trusted);
    EXPECT_FALSE(snap.safety.emergencyStop);
}

TEST_F(MotionControlServiceTest, TickHealthyBaselineReleasesGlobalLock) {
    auto svc = makeService();
    EXPECT_TRUE(svc->globallyLocked());  // initial locked
    svc->tick();                          // boot + first trusted read -> release
    EXPECT_FALSE(svc->globallyLocked());
}

TEST_F(MotionControlServiceTest, SnapshotReflectsRuntimeAxisFeedback) {
    auto svc = makeService();
    svc->tick();
    const auto snap = svc->store().snapshot();
    // makeTrustedRuntimeSnapshot: slot i -> absPosition = i*1000, motionState = 1, trusted.
    EXPECT_TRUE(snap.axes[3].trusted);
    EXPECT_EQ(snap.axes[3].absPosition, 3000.0f);
    EXPECT_EQ(snap.axes[3].motionState, 1);
    EXPECT_TRUE(snap.gantries[0].trusted);
    EXPECT_EQ(snap.gantries[0].state, 3);  // A coupled
}

TEST_F(MotionControlServiceTest, NotBootedKeepsGlobalLockEvenWhenTrusted) {
    // 无 topology（boot 失败）：即使 runtime/safety/connection 均可信，也必须保持锁定。
    FakePlcRuntimeGateway gwNoTopo;
    PlcRuntimeDriverAdapter drvNoTopo(gwNoTopo);
    MotionControlService svc(drvNoTopo, runtime_);

    EXPECT_TRUE(svc.globallyLocked());
    svc.tick();
    EXPECT_TRUE(svc.globallyLocked());  // 未 boot，不允许解除
}


TEST_F(MotionControlServiceTest, BootRetriesAfterTransientTopologyFailure) {
    // 启动时 topology 暂不可读 -> boot 失败锁定；拓扑恢复后经退避窗口重试并解锁。
    FakePlcRuntimeGateway gw2;
    PlcRuntimeDriverAdapter drv2(gw2);
    MotionControlService svc(drv2, runtime_);

    svc.tick();  // 首次：无 topology，boot 失败，保持锁定
    EXPECT_TRUE(svc.globallyLocked());

    // 拓扑恢复（模拟 PLC 启动完成），等退避窗口后下一 tick 重试成功并解锁。
    gw2.setTopologySnapshot(makeValidTopologySnapshot());
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    svc.tick();
    EXPECT_FALSE(svc.globallyLocked());
}

TEST_F(MotionControlServiceTest, SafetyReadFailureLocksGlobally) {
    auto svc = makeService();
    runtime_.scriptSafetyReadFailure(
        plc_vnext::contracts::ReadResult<SafetySnapshot>::FailureKind::Transport,
        "mock safety down");
    svc->tick();

    EXPECT_TRUE(svc->globallyLocked());
    const auto snap = svc->store().snapshot();
    EXPECT_FALSE(snap.safety.trusted);
}

TEST_F(MotionControlServiceTest, DisconnectLocksGloballyAndReflectedInSnapshot) {
    auto svc = makeService();
    runtime_.setConnected(false, "link down");
    svc->tick();

    EXPECT_TRUE(svc->globallyLocked());
    const auto snap = svc->store().snapshot();
    EXPECT_FALSE(snap.connection.connected);
}

TEST_F(MotionControlServiceTest, ReadRuntimeCalledExactlyOncePerTick) {
    auto svc = makeService();
    EXPECT_EQ(runtime_.readRuntimeCallCount(), 0u);
    EXPECT_EQ(gw_.readRuntimeCallCount(), 0u);   // driver 侧不得重复读 runtime
    svc->tick();
    EXPECT_EQ(runtime_.readRuntimeCallCount(), 1u);
    EXPECT_EQ(gw_.readRuntimeCallCount(), 0u);   // 服务只经 IControlRuntime 读 runtime
    svc->tick();
    EXPECT_EQ(runtime_.readRuntimeCallCount(), 2u);
    EXPECT_EQ(gw_.readRuntimeCallCount(), 0u);
}
TEST_F(MotionControlServiceTest, EmergencyStopSubmittedBeforeReadNotDroppedByTtl) {
    auto svc = makeService();
    ControlCommand estop = emergencyStopCmd();
    estop.createdAt = std::chrono::steady_clock::now() - std::chrono::seconds(10);
    estop.ttl = std::chrono::milliseconds(1);  // already expired, must not be dropped
    const auto id = svc->submit(estop);

    svc->tick();
    EXPECT_EQ(runtime_.emergencyStopCallCount(), 1u);
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    // write succeeded (sent) -> Accepted, not TimedOut (urgent never dropped by TTL).
    EXPECT_EQ(op->state, OperationState::Accepted);
}

TEST_F(MotionControlServiceTest, EmergencyStopWriteTimeoutIsCommitUncertain) {
    auto svc = makeService();
    runtime_.scriptEmergencyStopFailure(CommunicationResult::Status::Timeout, "mock busy");
    const auto id = svc->submit(emergencyStopCmd());

    svc->tick();
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    // 超时/忙：写入结果未知 -> CommitUncertain（不得无条件 Accepted）。
    EXPECT_EQ(op->state, OperationState::CommitUncertain);
}

TEST_F(MotionControlServiceTest, EmergencyStopWriteHardFailureIsFailed) {
    auto svc = makeService();
    runtime_.scriptEmergencyStopFailure(CommunicationResult::Status::NetworkError,
                                        "mock offline");
    const auto id = svc->submit(emergencyStopCmd());

    svc->tick();
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::Failed);
    EXPECT_FALSE(op->diag.empty());
}

TEST_F(MotionControlServiceTest, OrdinaryCommandExpiresAfterTtl) {
    auto svc = makeService();
    ControlCommand cmd = startRelMove();
    cmd.createdAt = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);
    cmd.ttl = std::chrono::milliseconds(1);
    const auto id = svc->submit(cmd);

    svc->tick();
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::TimedOut);
    EXPECT_EQ(svc->queuedCount(), 0u);  // drained by tick
}

TEST_F(MotionControlServiceTest, QueuedCommandRemainsQueuedAfterTickBeforeTtl) {
    auto svc = makeService();
    const auto id = svc->submit(startRelMove());  // default TTL 1000ms, not expired
    svc->tick();
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    // Phase 3：非过期普通命令会被仲裁（Y 未绑定 -> Failed），不再滞留 Queued。
    EXPECT_NE(op->state, OperationState::Queued);
}

// ================= Phase 3：仲裁 / 执行 / 会话 =================

TEST_F(MotionControlServiceTest, Phase3_MotionAcceptedAndRunningAfterArbitration) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto id = svc->submit(startYRel());
    svc->tick();  // boot -> 解锁 -> 仲裁通过 -> 创建会话 -> Running
    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::Running);
}

TEST_F(MotionControlServiceTest, Phase3_SecondMotionOnSameResourceRejected) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto id1 = svc->submit(startYRel());
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id1)->state, OperationState::Running);

    const auto id2 = svc->submit(startYRel());  // 同资源 axis:A:Y 已被 id1 占用
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id2)->state, OperationState::Rejected);
    EXPECT_EQ(svc->queryOperation(id1)->state, OperationState::Running);  // 不抢占
}

TEST_F(MotionControlServiceTest, Phase3_GlobalLockRejectsOrdinaryMotion) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    runtime_.setConnected(false, "link down");
    const auto id = svc->submit(startYRel());
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Rejected);
}

TEST_F(MotionControlServiceTest, Phase3_SameSourceJogRefreshIsIdempotent) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto id1 = svc->submit(startYJogForward(ControlSource::Joystick));
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id1)->state, OperationState::Running);

    const auto id2 = svc->submit(startYJogForward(ControlSource::Joystick));  // 同源重复点动
    svc->tick();
    // 幂等刷新：并入父会话（parentOperationId）并镜像其状态（Running），不重复占用/不拒绝。
    EXPECT_EQ(svc->queryOperation(id2)->parentOperationId, id1);
    EXPECT_EQ(svc->queryOperation(id2)->state, OperationState::Running);
    EXPECT_EQ(svc->queryOperation(id1)->state, OperationState::Running);
}

TEST_F(MotionControlServiceTest, Phase3_EstopCancelsAllSessions) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto jogId = svc->submit(startYJogForward(ControlSource::Ui));
    svc->tick();
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Running);

    // PLC 确认急停激活 -> 领域安全锁自终止会话。
    SafetySnapshot s;
    s.trusted = true;
    s.emergencyStop = true;
    runtime_.setSafetySnapshot(s);
    const auto estopId = svc->submit(emergencyStopCmd());
    svc->tick();
    EXPECT_EQ(svc->queryOperation(estopId)->state, OperationState::Accepted);
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Cancelled);
}

TEST_F(MotionControlServiceTest, Phase7_CoupleWithoutConfigFailsAtSubmit) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    // 不调用 applyGantryConfig：couple 前向步骤正常，SubmitCouple 时 RejectedUnconfigured。
    setGantryRuntime(runtime_, 1, 0, 1, 10, 0, 0, false, false, false, true);
    const auto id = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    svc->tick();   // Validate -> EnsureAxisControl
    svc->tick();   // EnableAxis -> WaitAxisControlReady
    setGantryRuntime(runtime_, 1, 0, 1, 10, 0, 0, false, false, false, true);
    svc->tick();   // WaitAxisControlReady -> EnsureMotor
    svc->tick();   // EnableMotor -> WaitMotorReady
    setGantryRuntime(runtime_, 2, 0, 1, 10, 0, 0, false, false, false, true);
    svc->tick();   // WaitMotorReady -> CheckGantryError
    svc->tick();   // CheckErr -> SubmitCouple
    svc->tick();   // SubmitCouple -> RejectedUnconfigured -> Error
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Failed);
    // 失败已释放龙门组租约（可再次提交而不被 Rejected）。
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
}

TEST_F(MotionControlServiceTest, Phase7_GantryCoupleRunsToSucceededAndReleasesLease) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    const auto id = driveCoupleToSucceeded(*svc, runtime_, gw_);

    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Succeeded);
    // couple 成功 -> Ready 终态 -> 释放 gantry:A:0 租约（UI/UDP 可查空闲）。
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
    // 逻辑轴 X 的租约投影同样释放。
    bool xBoundLeased = false;
    for (const auto& a : svc->store().snapshot().axes) {
        if (a.role == domain_vnext::model::AxisFunction::X) xBoundLeased = xBoundLeased || a.leased;
    }
    EXPECT_FALSE(xBoundLeased);
}

TEST_F(MotionControlServiceTest, Phase7_GantryDuringCoupleRejectsAxisOps) {
    // 龙门建立进行中持有 {gantry:A:0, X, X1, X2}：普通 X1 单轴运动与逻辑轴 X 运动都被拒
    // （资源集合重叠 -> 结构性互斥，非 if/else 特判）。
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);

    // 推进 couple 到 WaitMotorReady（尚未 Ready，租约仍在）。
    setGantryRuntime(runtime_, 1, 0, 1, 10, 0, 0, false, false, false, true);
    const auto coupleId = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    svc->tick();   // Validate -> EnsureAxisControl
    svc->tick();   // EnableAxis -> WaitAxisControlReady
    setGantryRuntime(runtime_, 1, 0, 1, 10, 0, 0, false, false, false, true);
    svc->tick();   // WaitAxisControlReady -> EnsureMotor
    svc->tick();   // EnableMotor -> WaitMotorReady
    EXPECT_EQ(svc->queryOperation(coupleId)->state, OperationState::Running);
    EXPECT_TRUE(svc->store().snapshot().gantries[0].lifecycleLeased);

    // X1 单轴运动 -> 与 couple 的 axis:A:X1 冲突 -> Rejected。
    auto x1 = startRelMove();
    x1.target.function = domain_vnext::model::AxisFunction::X1;
    x1.motion = MotionRequest{10.f, 10.f};
    const auto x1Id = svc->submit(x1);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(x1Id)->state, OperationState::Rejected);

    // 逻辑轴 X 运动 -> 与 couple 的 gantry:A:0 组冲突 -> Rejected。
    auto xmove = startRelMove();
    xmove.target.function = domain_vnext::model::AxisFunction::X;
    xmove.motion = MotionRequest{10.f, 10.f};
    const auto xId = svc->submit(xmove);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(xId)->state, OperationState::Rejected);
}

TEST_F(MotionControlServiceTest, Phase7_GantryDecoupleRunsToSucceeded) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    driveCoupleToSucceeded(*svc, runtime_, gw_);   // 先建立到 Ready（租约已释放）

    // 解除：coupled(ms==2) -> Decouple(seq2) -> WaitFinal(State1/Step10/AckSeq2) -> 掉电 -> Done。
    setGantryRuntime(runtime_, 2, 1, 3, 80, 2, 0, true, true, true, false);  // coupled idle
    const auto id = svc->submit(gantryCmd(ControlAction::GantryDecoupleAndDisable));
    svc->tick();   // EnsureLogicalAxisStopped(ms==2) -> SubmitDecouple
    svc->tick();   // Decouple(seq2) -> WaitDecoupleFinal
    setGantryRuntime(runtime_, 2, 2, 1, 10, 2, 0, false, false, false, true);  // decoupled ackSeq2
    svc->tick();   // WaitDecoupleFinal -> DisableMotor
    svc->tick();   // 掉电 -> Done
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Succeeded);
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
}

// ---- Phase 7：龙门生命周期取消安全收口（P0-B）----
// 规则：取消发生在 Couple 提交前 -> 立即终止、绝无 Couple/使能写；
//       取消发生在 Couple 已提交（WaitCoupleFinal）-> 只读观察，等 AckSeq 自然收口 -> Cancelled。

TEST_F(MotionControlServiceTest, Phase7_CancelBeforeCoupleCommit_NoGantryWrite) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // 推进到 CheckGantryError（SubmitCouple 之前）：state=1/ms=2 反馈 tick 5 次。
    setGantryRuntime(runtime_, 2, 0, 1, 10, 0, 0, false, false, false, true);
    const auto id = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    for (int i = 0; i < 5; ++i) svc->tick();   // -> CheckGantryError（Couple 尚未提交）
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
    EXPECT_TRUE(gw_.gantrySubmissions().empty());   // Couple 尚未提交

    // 断线 -> 全局锁定 -> 会话 cancel -> 策略立即终止，绝不发 Couple。
    runtime_.setConnected(false, "link down");
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Cancelled);
    EXPECT_TRUE(gw_.gantrySubmissions().empty());   // 绝无 Couple 写
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);   // 租约已释放
}

TEST_F(MotionControlServiceTest, Phase7_CancelAfterCoupleCommit_ObservesThenCancelled) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // 推进到 WaitCoupleFinal（Couple 已提交）：state=1/ms=2，tick 直到 gantrySubmissions 非空。
    setGantryRuntime(runtime_, 2, 0, 1, 10, 0, 0, false, false, false, true);
    const auto id = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    for (int i = 0; i < 40 && gw_.gantrySubmissions().empty(); ++i) svc->tick();
    ASSERT_FALSE(gw_.gantrySubmissions().empty());   // Couple 已提交 -> WaitCoupleFinal
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);

    // 断线 -> 取消 -> 只读观察：保持 WaitCoupleFinal（不发新写），租约保持。
    runtime_.setConnected(false, "link down");
    svc->tick();
    EXPECT_TRUE(svc->store().snapshot().gantries[0].lifecycleLeased);

    // 反馈到 coupled（AckSeq=1）-> 观察收口为 Cancelled（不是 Succeeded），并释放租约。
    setGantryRuntime(runtime_, 2, 1, 3, 80, 2, 0, true, true, true, false);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Cancelled);
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
}

TEST_F(MotionControlServiceTest, Phase7_CancelDuringResetWait_ObservesThenCancelled) {
    // Reset 已提交（WaitResetFinal）：取消后保持租约只读观察，等 AckSeq==Reset seq 收口为
    // Cancelled；期间不得提交新的 Couple（gantrySubmissions 仍只有 Reset 一条）。
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // CheckGantryError(err!=0) -> SubmitReset -> WaitResetFinal：err=123 触发复位路径。
    setGantryRuntime(runtime_, 2, 0, 1, 10, 0, /*err=*/123, false, false, false, true);
    const auto id = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    for (int i = 0; i < 40 && gw_.gantrySubmissions().empty(); ++i) svc->tick();
    ASSERT_FALSE(gw_.gantrySubmissions().empty());   // Reset 已提交 -> WaitResetFinal
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
    const auto resetCount = gw_.gantrySubmissions().size();

    // 断线 -> 取消 -> 只读观察：保持租约。
    runtime_.setConnected(false, "link down");
    svc->tick();
    EXPECT_TRUE(svc->store().snapshot().gantries[0].lifecycleLeased);

    // 反馈 Reset 完成（AckSeq==1、state=1、step=10、cmdResult=2、member）-> Cancelled。
    setGantryRuntime(runtime_, 2, 1, 1, 10, 2, 0, false, false, false, true);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Cancelled);
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
    // 观察期不得提交新龙门请求（未新增 Couple）。
    EXPECT_EQ(gw_.gantrySubmissions().size(), resetCount);
}

// 龙门逻辑轴 X 点动必须走 GantryMotionApi（LifecycleManaged + GantryMotionGuard）。
// 已联动（State=3/Step=80/CommandResult=2/err=0/InGear/LogicalControlAllowed）时放行，
// 跳过自管理使能、直接进入 Jogging，并向逻辑轴 slot13 写方向/心跳线圈。
TEST_F(MotionControlServiceTest, Phase7_GantryXJogRunsWhenCoupled) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // 龙门已联动：逻辑轴 ms=2（电机空闲），guard 全部满足。
    setGantryRuntime(runtime_, /*ms=*/2, /*ackSeq=*/0, /*state=*/3, /*step=*/80,
                     /*result=*/2, /*err=*/0, /*x1=*/true, /*x2=*/true,
                     /*logical=*/true, /*member=*/false);
    const auto id = svc->submit(startXJogForward(ControlSource::Ui));
    svc->tick();   // 建会话 -> PostEnableDelay（LifecycleManaged 不自行使能电机）
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    svc->tick();   // PostEnableDelay -> IssuingJog
    svc->tick();   // IssuingJog -> Jogging
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
    EXPECT_TRUE(wroteAxis(gw_, PlcAxisCommandKind::JogHeartbeat, true));
    EXPECT_TRUE(wroteAxis(gw_, PlcAxisCommandKind::JogForward, true));
}

// 龙门未联动（State=1/readyToCouple）时，龙门 X 点动走 GantryAutoJogSession 自动建立联动
// （复刻 gantry-run-jog）：会话首段进入 Couple，向 PLC 提交 Couple 请求，操作保持 Running。
TEST_F(MotionControlServiceTest, Phase7_GantryXJog_AutoCouplesWhenDecoupled) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // 未联动：State=1/Step=10/member 开放、logical 关闭，readyToCouple=true。
    setGantryRuntime(runtime_, /*ms=*/2, /*ackSeq=*/0, /*state=*/1, /*step=*/10,
                     /*result=*/0, /*err=*/0, /*x1=*/false, /*x2=*/false,
                     /*logical=*/false, /*member=*/true);
    const auto id = svc->submit(startXJogForward(ControlSource::Ui));
    // 推进会话 couple 段：直到 fake 网关收到 Couple 提交（进入 WaitCoupleFinal）。
    for (int i = 0; i < 80 && gw_.gantrySubmissions().empty(); ++i) svc->tick();
    ASSERT_FALSE(gw_.gantrySubmissions().empty());
    EXPECT_EQ(gw_.gantrySubmissions().back().req.command, plc_vnext::contracts::GantryCommandKind::Couple);
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
    EXPECT_TRUE(svc->store().snapshot().gantries[0].lifecycleLeased);
}
// 龙门故障（State=5）时，建立联动应先复位（GantryCommand=3 + RequestSeq++）清错，
// 复位完成后（State=1/Step=10/err=0）再走完整使能+联动序列，最终提交 Couple。
TEST_F(MotionControlServiceTest, Phase7_GantryCouple_FaultResetsThenCouples) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    bootAndConfigureGantry(*svc);
    // 故障：State=5、err=123，readyToCouple=false。
    setGantryRuntime(runtime_, /*ms=*/2, /*ackSeq=*/0, /*state=*/5, /*step=*/0,
                     /*result=*/0, /*err=*/123, /*x1=*/false, /*x2=*/false,
                     /*logical=*/false, /*member=*/false);
    const auto id = svc->submit(gantryCmd(ControlAction::GantryEnableAndCouple));
    // ValidatePreconditions 识别故障 -> SubmitReset -> 提交 Reset。
    for (int i = 0; i < 40 && gw_.gantrySubmissions().empty(); ++i) svc->tick();
    ASSERT_FALSE(gw_.gantrySubmissions().empty());
    EXPECT_EQ(gw_.gantrySubmissions().back().req.command,
              plc_vnext::contracts::GantryCommandKind::Reset);
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
    // 注入复位完成：State=1/Step=10/result=2/err=0/member 开放。
    setGantryRuntime(runtime_, /*ms=*/2, /*ackSeq=*/1, /*state=*/1, /*step=*/10,
                     /*result=*/2, /*err=*/0, /*x1=*/false, /*x2=*/false,
                     /*logical=*/false, /*member=*/true);
    // 复位后走完整序列并提交 Couple（循环直到提交 Couple 或达到上限）。
    bool sawCouple = false;
    for (int i = 0; i < 200 && svc->queryOperation(id)->state == OperationState::Running; ++i) {
        svc->tick();
        if (!gw_.gantrySubmissions().empty() &&
            gw_.gantrySubmissions().back().req.command ==
                plc_vnext::contracts::GantryCommandKind::Couple) {
            sawCouple = true;
            break;
        }
    }
    EXPECT_TRUE(sawCouple);
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Running);
}




TEST_F(MotionControlServiceTest, Phase3_StopJogCancelsSession) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto jogId = startJogToJogging(*svc, runtime_, ControlSource::Joystick);
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Running);

    ControlCommand stop;
    stop.source = ControlSource::Joystick;
    stop.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    stop.target.function = domain_vnext::model::AxisFunction::Y;
    stop.action = ControlAction::StopJog;
    svc->submit(stop);
    svc->tick();   // Jogging -> IssuingStop
    EXPECT_TRUE(wroteAxis(gw_, PlcAxisCommandKind::JogHeartbeat, false));
    EXPECT_TRUE(wroteAxis(gw_, PlcAxisCommandKind::JogForward, false));
    EXPECT_TRUE(wroteAxis(gw_, PlcAxisCommandKind::JogBackward, false));
    svc->tick();   // -> WaitingForIdle
    svc->tick();   // WaitingForIdle (ms==2) -> PostStopDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // 过 PostStopDelay(0.5s)
    svc->tick();   // PostStopDelay -> EnsuringDisabled
    svc->tick();   // -> Done -> Cancelled
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Cancelled);
}

TEST_F(MotionControlServiceTest, Phase3_DisconnectCancelsSessionNoReplay) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto jogId = startJogToJogging(*svc, runtime_, ControlSource::Udp);
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Running);

    // 断线 -> 全局锁定 -> cancel 会话，驱动到 Done 收口为 Cancelled。
    runtime_.setConnected(false, "link down");
    svc->tick();   // Jogging -> IssuingStop（cancel 已应用）
    svc->tick();   // -> WaitingForIdle
    svc->tick();   // -> PostStopDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // 过 PostStopDelay(0.5s)
    svc->tick();   // -> EnsuringDisabled
    svc->tick();   // -> Done -> Cancelled
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Cancelled);

    // 重连后不得重放该运动：operation 保持终局，不重新进入 Running。
    runtime_.setConnected(true, "up");
    svc->tick();
    EXPECT_EQ(svc->queryOperation(jogId)->state, OperationState::Cancelled);
}

TEST_F(MotionControlServiceTest, Phase3_OneShotWriteRespectsLease) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto moveId = svc->submit(startYRel());
    svc->tick();
    EXPECT_EQ(svc->queryOperation(moveId)->state, OperationState::Running);

    // 他来源在 Y 运动中尝试 EnableMotor=false -> 尊重租约拒绝（改变运行安全性）。
    ControlCommand enable;
    enable.source = ControlSource::Ui;
    enable.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    enable.target.function = domain_vnext::model::AxisFunction::Y;
    enable.action = ControlAction::EnableMotor;
    enable.level = false;
    const auto id = svc->submit(enable);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Rejected);
}

TEST_F(MotionControlServiceTest, Phase3_LeaseProjectedIntoSnapshot) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    const auto moveId = svc->submit(startYRel());   // Udp 源
    svc->tick();
    const auto snap = svc->store().snapshot();
    const auto& a = snap.axes[2];                   // six-axis: slot2 = Y
    ASSERT_TRUE(a.bound);
    EXPECT_TRUE(a.leased);
    EXPECT_EQ(a.leaseOperationId, moveId);
    EXPECT_EQ(a.leaseOwnerName, "UDP");
}

TEST_F(MotionControlServiceTest, Phase3_UnboundAxisMoveFailsAndReleasesLease) {
    auto svc = makeService();   // 默认拓扑只绑定 X1/X2/X，不绑定 Z
    auto cmd = startRelMove();
    cmd.target.function = domain_vnext::model::AxisFunction::Z;
    cmd.motion = MotionRequest{10.f, 10.f};
    const auto id = svc->submit(cmd);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Failed);

    // 失败已释放租约（否则第二次会 Rejected 而非 Failed）。
    const auto id2 = svc->submit(cmd);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id2)->state, OperationState::Failed);
}

TEST_F(MotionControlServiceTest, Phase3_AxisAtLimitRejectsPositioning) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();

    // 注入 Y(slot2) 触发负软限位（motionLimit=2），boot 时注入 domain feedback。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].motionLimit = 2;
    runtime_.setRuntimeSnapshot(r);
    svc->tick();   // boot + 注入 feedback（Y.motionLimit=2）

    // 限位状态下发起定位（StartRelMove，目标 Y）→ 权威拒绝（Failed），
    // 且不进入会话、不占租约（限位后只能点动撤离）。
    auto cmd = startRelMove();
    cmd.motion = MotionRequest{150.0f, 50.0f};
    const auto id = svc->submit(cmd);
    svc->tick();

    const auto op = svc->queryOperation(id);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::Failed);
    EXPECT_NE(op->diag.find("jog away"), std::string::npos);

    // 失败已释放租约（轴不处于 leased）。
    const auto snap = svc->store().snapshot();
    EXPECT_FALSE(snap.axes[2].leased);
}

}  // namespace
}  // namespace application_vnext::control
