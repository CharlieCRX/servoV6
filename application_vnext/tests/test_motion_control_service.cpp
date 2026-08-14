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
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "application_vnext/tests/fake/FakeControlRuntime.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace application_vnext::control {
namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using plc_vnext::contracts::CommunicationResult;
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

TEST_F(MotionControlServiceTest, Phase3_GantryLifecycleRejectedUntilPhase7) {
    gw_.setTopologySnapshot(makeSixAxisTopology());
    auto svc = makeService();
    ControlCommand couple;
    couple.source = ControlSource::Ui;
    couple.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    couple.target.function = domain_vnext::model::AxisFunction::X;
    couple.action = ControlAction::GantryEnableAndCouple;
    const auto id = svc->submit(couple);
    svc->tick();
    EXPECT_EQ(svc->queryOperation(id)->state, OperationState::Rejected);
    // 不产生龙门组资源占用。
    EXPECT_FALSE(svc->store().snapshot().gantries[0].lifecycleLeased);
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

}  // namespace
}  // namespace application_vnext::control
