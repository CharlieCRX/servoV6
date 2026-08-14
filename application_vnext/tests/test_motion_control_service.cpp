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
    EXPECT_EQ(op->state, OperationState::Queued);  // arbitration is Phase 3
}

}  // namespace
}  // namespace application_vnext::control
