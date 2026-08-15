// ============================================================================
// test_udp_command_dispatcher.cpp -- Phase 5: UDP 异步 operationId 改造测试
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 5 验收：
//   - UDP MOVE_OFFSET 立即返回 Queued + operationId，不阻塞；下一 tick 转 Accepted/Running；
//   - queryOperation(id) 回包 OperationEntry → JSON（状态含 Queued/Accepted/Running/...）；
//   - 已有旧 UDP cmd 兼容（返回 Queued，异步完成；查询类不产生租约）。
//
// UdpCommandDispatcher 已不再持有策略 / 不再直接写 PLC，全部改为解析 JSON →
// ControlCommand → MotionControlService::submit() → 立即回 Queued + operationId，
// 运动由统一协调层唯一 tick 异步仲裁/执行/发布快照。
//
// 依赖：application（UdpCommandDispatcher）+ application_vnext（MotionControlService/
// PlcRuntimeDriverAdapter）+ FakeControlRuntime + FakePlcRuntimeGateway（六轴拓扑，R 轴可仲裁）。
// ============================================================================
#include <memory>
#include <string>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include "application/udp/UdpCommandDispatcher.h"

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "fake/FakeControlRuntime.h"

#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::control::ControlSource;
using application_vnext::control::FakeControlRuntime;
using application_vnext::control::MotionControlService;
using application_vnext::control::OperationState;
using plc_vnext::contracts::SafetySnapshot;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeRole;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

/// gtest 不提供 main；UdpCommandDispatcher 依赖 QJson（Qt）。local-static 保证一次。
QCoreApplication* ensureApp() {
    static int argc = 1;
    static char a0[] = "presentation_tests";
    static char* argv[] = { a0, nullptr };
    static QCoreApplication app(argc, argv);
    return &app;
}

/// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。UDP 用 motor=2 → R。
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

/// 真实 service + 六轴拓扑 + UdpCommandDispatcher（UDP 源命令可被仲裁执行）。
class UdpCommandDispatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensureApp();
        gw_.setTopologySnapshot(makeSixAxisTopology());
        runtime_.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
        SafetySnapshot s;
        s.trusted = true;
        s.emergencyStop = false;
        runtime_.setSafetySnapshot(s);
        runtime_.setConnected(true);
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
        svc_->tick();  // boot + 首读可信 → 解除全局锁定
        dispatcher_ = std::make_unique<UdpCommandDispatcher>(*svc_);
    }

    /// 解析 JSON 回包中的字段。
    QJsonObject parse(const std::string& s) {
        return QJsonDocument::fromJson(QByteArray::fromStdString(s)).object();
    }

    FakePlcRuntimeGateway            gw_;
    FakeControlRuntime               runtime_;
    std::unique_ptr<PlcRuntimeDriverAdapter> driver_;
    std::unique_ptr<MotionControlService>   svc_;
    std::unique_ptr<UdpCommandDispatcher>   dispatcher_;
};

/// 解析回包中的 operationId。
std::string extractOpId(const QJsonObject& reply) {
    return reply[QString::fromUtf8(UdpField::OP_ID)].toString().toStdString();
}

// ---------- 测试 1：MOVE_OFFSET 立即返回 Queued + operationId，不阻塞 ----------

TEST_F(UdpCommandDispatcherTest, MoveOffsetReturnsQueuedAndOpIdThenRunning) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"Machine_A","offset":10,"speed":5})"));

    ASSERT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 1);
    const std::string opId = extractOpId(reply);
    ASSERT_FALSE(opId.empty());
    EXPECT_TRUE(opId.rfind("udp-", 0) == 0) << "operationId 应以 udp- 开头: " << opId;
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::STATE)].toString().toStdString(), "Queued");

    // 尚未 tick：仍在队列
    EXPECT_GT(svc_->queuedCount(), 0u);

    // 唯一 tick 仲裁并执行 → Accepted → Running（与 Phase 3 单测一致）
    svc_->tick();
    const auto op = svc_->queryOperation(opId);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::Running);
}

// ---------- 测试 2：queryOperation(id) 返回 OperationEntry → JSON ----------

TEST_F(UdpCommandDispatcherTest, QueryOperationReturnsOperationJson) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"Machine_A","offset":5,"speed":5})"));
    const std::string opId = extractOpId(reply);
    ASSERT_FALSE(opId.empty());

    // 先 tick 到 Running，再查询 → 状态 JSON 含 motionState/position/axis 等。
    svc_->tick();
    const auto q = parse(dispatcher_->queryOperation(opId));
    EXPECT_EQ(q[QString::fromUtf8(UdpField::RESULT)].toInt(), 1);
    EXPECT_EQ(q[QString::fromUtf8(UdpField::OP_ID)].toString().toStdString(), opId);
    EXPECT_EQ(q[QString::fromUtf8(UdpField::STATE)].toString().toStdString(), "Running");
    EXPECT_EQ(q[QString::fromUtf8(UdpField::SOURCE)].toString().toStdString(), "UDP");
    EXPECT_EQ(q[QString::fromUtf8(UdpField::AXIS)].toString().toStdString(), "A.R");
    EXPECT_TRUE(q.contains(QString::fromUtf8(UdpField::MOTION)));
    EXPECT_TRUE(q.contains(QString::fromUtf8(UdpField::POS)));
}

// ---------- 测试 3：GET_REL_POSITION（cmd=2）从统一快照读取，不产生租约 ----------

TEST_F(UdpCommandDispatcherTest, GetRelPositionReadsFromUnifiedSnapshot) {
    // 脚本化 R 轴的相对位置，验证快照查询路径。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[4].relPosition = 123.5f;  // six-axis: slot4 = R
    runtime_.setRuntimeSnapshot(r);
    svc_->tick();  // 发布新快照（relPosition 注入领域并投影）

    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":2,"motor":2,"group":"Machine_A"})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 1);
    EXPECT_FLOAT_EQ(reply[QString::fromUtf8(UdpField::CURR)].toDouble(),
                    static_cast<double>(123.5));
    // 查询类不产生租约/队列
    EXPECT_EQ(svc_->queuedCount(), 0u);
}

// ---------- 测试 4：旧 UDP cmd 兼容 + 错误分支 ----------

TEST_F(UdpCommandDispatcherTest, MissingOffsetRejected) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"Machine_A"})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
    EXPECT_FALSE(reply[QString::fromUtf8(UdpField::MSG)].toString().isEmpty());
}

TEST_F(UdpCommandDispatcherTest, UnsupportedMotorRejected) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":99,"group":"Machine_A","offset":1,"speed":5})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
}

TEST_F(UdpCommandDispatcherTest, UnknownGroupRejected) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"NoSuchGroup","offset":1,"speed":5})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
}

// ---------- 测试 5：SET_REL_ZERO（cmd=5）→ SetRelZero 一次性写入，tick 后 Succeeded ----------

TEST_F(UdpCommandDispatcherTest, SetRelZeroSubmitsThenSucceeded) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":5,"motor":2,"group":"Machine_A"})"));
    ASSERT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 1);
    const std::string opId = extractOpId(reply);
    ASSERT_FALSE(opId.empty());
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::STATE)].toString().toStdString(), "Queued");

    svc_->tick();
    const auto op = svc_->queryOperation(opId);
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->state, OperationState::Succeeded);  // one-shot：成功即 Succeeded
}

// ---------- 测试 6：运动速度必须为正（Phase 4 P0：绝不写 0 速度） ----------

TEST_F(UdpCommandDispatcherTest, MoveOffsetRejectsNonPositiveSpeed) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"Machine_A","offset":10,"speed":0})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
    // 未提交任何运动命令
    EXPECT_EQ(svc_->queuedCount(), 0u);
}

// ---------- 测试 7：cmd=6 QUERY_OPERATION 从协议入口查询最终状态 ----------

TEST_F(UdpCommandDispatcherTest, QueryOperationViaProtocolCmd) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":1,"motor":2,"group":"Machine_A","offset":5,"speed":5})"));
    const std::string opId = extractOpId(reply);
    ASSERT_FALSE(opId.empty());
    svc_->tick();  // -> Running

    // cmd=6：仅需 cmd + operationId，不要求 motor/group（真实 UDP 客户端凭回执查询）。
    const std::string qReq = std::string(R"({"cmd":6,"operationId":")") + opId + R"("})";
    const auto q = parse(dispatcher_->dispatch(qReq));
    EXPECT_EQ(q[QString::fromUtf8(UdpField::RESULT)].toInt(), 1);
    EXPECT_EQ(q[QString::fromUtf8(UdpField::STATE)].toString().toStdString(), "Running");
    EXPECT_EQ(q[QString::fromUtf8(UdpField::OP_ID)].toString().toStdString(), opId);
}

TEST_F(UdpCommandDispatcherTest, QueryOperationViaProtocolMissingOpIdRejected) {
    const auto reply = parse(dispatcher_->dispatch(R"({"cmd":6})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
}

TEST_F(UdpCommandDispatcherTest, QueryOperationViaProtocolUnknownOpIdRejected) {
    const auto reply = parse(dispatcher_->dispatch(
        R"({"cmd":6,"operationId":"udp-no-such"})"));
    EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0);
}

// ---------- 测试 8：cmd=3 SET_MOVE_SPEED 拒绝 0/负速度，且不入队 ----------

TEST_F(UdpCommandDispatcherTest, SetMoveSpeedRejectsZeroAndNegative) {
    for (const char* speedVal : { "0", "-1" }) {
        const std::string req = std::string(
            R"({"cmd":3,"motor":2,"group":"Machine_A","speed":)") + speedVal + R"(})";
        const auto reply = parse(dispatcher_->dispatch(req));
        EXPECT_EQ(reply[QString::fromUtf8(UdpField::RESULT)].toInt(), 0) << "speed=" << speedVal;
        EXPECT_EQ(svc_->queuedCount(), 0u) << "speed=" << speedVal;  // 拒绝且不入队
    }
}

}  // namespace


