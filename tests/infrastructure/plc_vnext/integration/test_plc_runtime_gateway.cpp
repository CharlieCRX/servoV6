// ============================================================================
// test_plc_runtime_gateway.cpp —— Step 10 Gateway: 组合门面（红/绿）
// ============================================================================
// 依据《TDD实施文档》Step 10 红用例（10.1），全部经 FakeModbusClient 离线执行，
// 不接触真实 PLC / AsioModbusTcpClient：
//   ReadTopology_ReturnsSnapshot           走通 reader+decoder+validator
//   ReadRuntime_ReturnsSnapshotWithQuality 走通 plan+io+decode（quality==Trusted）
//   WriteAxis_ReportsCommResult            单轴写入走通（字序 CDAB 低字在前）
//   SubmitGantryRequest_OrderedWrite       龙门请求走通（Command→RequestSeq，无读回）
//   ConnectionState_Exposed                connectionState() 返回连接状态
//   RequestReconnect_Delegated             requestReconnect() 委托 transport
//   DoesNotTouchSystemContext              接口签名不含 SystemContext/Axis/ViewModel
// 并补充：readRuntime() 在整体质量非 Trusted（TransportFailed）时返回 ReadResult
// Transport 失败（Gateway 不做业务降级决策，只透传失败类别）。
//
// Step 10.3 禁止：Gateway 内不得出现联动业务编排、重试策略、超时判定、ViewModel。
// ============================================================================
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"
#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"
// 注：仅复用 TopologyFixture（makeValidTopologyRegisters/kFixtureMagic）。Telemetry
// fixture 与本 fixture 在 test::writeInt16/test::setBit 上同名冲突，故 telemetry
// 侧需要的极简 helper（makeAxisBlock/makeGantryBlock/writeFloat）在本文件自包含。
#include "tests/infrastructure/plc_vnext/support/TopologyFixture.h"

namespace plc_vnext {
namespace {

using contracts::GantryRequest;
using contracts::PlcAxisCommand;
using contracts::PlcAxisSlot;
using contracts::PlcGroupIndex;
using contracts::SnapshotQuality;

// 把整块 178 字拓扑按地址 1400..1577 写入 Fake RAM（与 test_topology_reader 一致）。
void loadTopologyRegisters(fake::FakeModbusClient& fake,
                           const std::vector<uint16_t>& regs) {
    for (std::size_t i = 0; i < regs.size(); ++i) {
        fake.setHoldingRegister(
            static_cast<uint16_t>(layout::topologyMagic().value() + i), regs[i]);
    }
}

// 轴区块（D0..D175，向量下标 == 绝对 D 地址）写入 Fake RAM。
void loadAxis(fake::FakeModbusClient& fake, const std::vector<uint16_t>& block) {
    for (std::size_t i = 0; i < block.size(); ++i) {
        fake.setHoldingRegister(static_cast<uint16_t>(i), block[i]);
    }
}

// 龙门状态区块（下标 0 == D190）写入 Fake RAM。
void loadGantry(fake::FakeModbusClient& fake, const std::vector<uint16_t>& block) {
    const uint16_t base = static_cast<uint16_t>(layout::gantryStatusBase(0).value());
    for (std::size_t i = 0; i < block.size(); ++i) {
        fake.setHoldingRegister(static_cast<uint16_t>(base + i), block[i]);
    }
}

// —— 自包含的 telemetry 极简 helper（D0..D175 轴区块 / D190..D225 龙门块 / CDAB 低字在前 REAL）——
std::vector<uint16_t> makeAxisBlock() {
    return std::vector<uint16_t>(176, 0);
}
std::vector<uint16_t> makeGantryBlock() {
    return std::vector<uint16_t>(36, 0);
}
void writeFloat(std::vector<uint16_t>& r, int idx, float v) {
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(float));
    r[static_cast<std::size_t>(idx)] = static_cast<uint16_t>(raw & 0xFFFFu);
    r[static_cast<std::size_t>(idx) + 1] = static_cast<uint16_t>((raw >> 16) & 0xFFFFu);
}

// —— 单一交错日志客户端：把 telemetry 读('R')、单寄存器写('C')、多寄存器写('S')
//    记入同一日志，供跨模块并发断言"Gantry Command→RequestSeq 不被插入"。 ——
class OrderLoggingClient : public transport::IModbusClient {
public:
    explicit OrderLoggingClient(std::shared_ptr<fake::FakeModbusClient> inner)
        : m_inner(std::move(inner)) {}

    std::vector<char> log() const {
        std::lock_guard<std::mutex> l(m_logMtx);
        return m_log;
    }

    bool isConnected() const override { return m_inner->isConnected(); }
    void requestReconnect() override { m_inner->requestReconnect(); }

    contracts::CommunicationResult readCoils(uint16_t a, uint16_t c,
                                             std::vector<uint8_t>& p) override {
        return m_inner->readCoils(a, c, p);
    }
    contracts::CommunicationResult readHoldingRegisters(
        uint16_t a, uint16_t c, std::vector<uint16_t>& p) override {
        auto r = m_inner->readHoldingRegisters(a, c, p);
        { std::lock_guard<std::mutex> l(m_logMtx); m_log.push_back('R'); }
        return r;
    }
    contracts::CommunicationResult writeSingleCoil(uint16_t a, bool v) override {
        return m_inner->writeSingleCoil(a, v);
    }
    contracts::CommunicationResult writeSingleRegister(uint16_t a, uint16_t v) override {
        auto r = m_inner->writeSingleRegister(a, v);
        { std::lock_guard<std::mutex> l(m_logMtx); m_log.push_back('C'); }
        return r;
    }
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t a, const std::vector<uint16_t>& vals) override {
        auto r = m_inner->writeMultipleRegisters(a, vals);
        { std::lock_guard<std::mutex> l(m_logMtx); m_log.push_back('S'); }
        return r;
    }

private:
    std::shared_ptr<fake::FakeModbusClient> m_inner;
    mutable std::mutex m_logMtx;
    std::vector<char> m_log;
};

// ─────────────────────────────────────────────
// 走通 reader + decoder + validator：返回可信拓扑快照
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, ReadTopology_ReturnsSnapshot) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    loadTopologyRegisters(*fake, test::makeValidTopologyRegisters(7));

    PlcRuntimeGateway gateway(fake);
    auto res = gateway.readTopology();

    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    EXPECT_EQ(res.value().header.magic, test::kFixtureMagic);
    EXPECT_TRUE(res.value().header.configValid);
    EXPECT_EQ(res.value().groups.size(), 2u);
    // A 组 Role[0] 绑定 X1（PlcAxisIndex=0, MotionMode=1 龙门X1）。
    EXPECT_EQ(res.value().groups[0].roles[0].plcAxisIndex, 0);
    EXPECT_EQ(res.value().groups[0].roles[0].motionMode, 1);
}

// ─────────────────────────────────────────────
// 走通 plan + io + decode：quality == Trusted 时返回 success 快照
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, ReadRuntime_ReturnsSnapshotWithQuality) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto axis = makeAxisBlock();
    writeFloat(axis, layout::absPosition(0).value(), 100.0f);
    loadAxis(*fake, axis);
    loadGantry(*fake, makeGantryBlock());

    PlcRuntimeGateway gateway(fake);
    auto res = gateway.readRuntime();

    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    EXPECT_EQ(res.value().quality, SnapshotQuality::Trusted);
    EXPECT_FLOAT_EQ(res.value().axes[0].absPosition, 100.0f);
    EXPECT_TRUE(res.value().axes[0].trusted);
    EXPECT_TRUE(res.value().gantry[0].trusted);
}

// ─────────────────────────────────────────────
// 单轴写入走通：ok() 证明写到达；字序 CDAB 低字在前（25.8f -> {0x6666, 0x41CE}）
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, WriteAxis_ReportsCommResult) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    PlcRuntimeGateway gateway(fake);

    auto slot = PlcAxisSlot::tryCreate(3);
    ASSERT_TRUE(slot.has_value());

    auto res = gateway.writeAxis(*slot, PlcAxisCommand::makeSetManualSpeed(25.8f));
    EXPECT_TRUE(res.ok()) << res.diagnostic;
    EXPECT_EQ(fake->writtenMulti().size(), 1u);

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    ASSERT_EQ(multi[0].values.size(), 2u);
    EXPECT_EQ(multi[0].values[0], 0x6666u);
    EXPECT_EQ(multi[0].values[1], 0x41CEu);
}

// ─────────────────────────────────────────────
// 龙门请求走通：Command→RequestSeq 两笔有序写入，且不做读回确认
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, SubmitGantryRequest_OrderedWrite) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    PlcRuntimeGateway gateway(fake);

    auto g = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g.has_value());

    auto res = gateway.submitGantryRequest(*g, GantryRequest::couple(5));
    EXPECT_TRUE(res.ok()) << res.diagnostic;
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);  // Command（FC06）
    EXPECT_EQ(fake->writtenMulti().size(), 1u);      // RequestSeq（FC10）
    // Gateway 只提交，不做读回/确认（AckSeq 对齐交上层）。
    EXPECT_EQ(fake->readCount(), 0u);
}

// ─────────────────────────────────────────────
// connectionState() 返回连接状态；每次查询前与底层同步一次
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, ConnectionState_Exposed) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setConnected(true);
    PlcRuntimeGateway gateway(fake);

    auto state = gateway.connectionState();
    EXPECT_TRUE(state.connected);

    // 底层断线后，connectionState() 反映最新状态（查询时同步）。
    fake->setConnected(false);
    state = gateway.connectionState();
    EXPECT_FALSE(state.connected);
}

// ─────────────────────────────────────────────
// requestReconnect() 委托 transport 连接层（FakeModbus 记录重连次数）
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, RequestReconnect_Delegated) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    PlcRuntimeGateway gateway(fake);

    gateway.requestReconnect();
    EXPECT_EQ(fake->requestReconnectCount(), 1u);
}

// ─────────────────────────────────────────────
// readRuntime() 在整体质量非 Trusted（TransportFailed）时返回 Transport 失败，
// 不提供“可信完整快照”，也不做业务降级决策。
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, ReadRuntime_TransportFailure_ReturnsFailure) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    fake->setFailureThreshold(0);  // 全部读请求失败 → quality TransportFailed

    PlcRuntimeGateway gateway(fake);
    auto res = gateway.readRuntime();

    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::Transport);
    EXPECT_FALSE(res.diagnostic().empty());
}

// ─────────────────────────────────────────────
// 接口签名不触碰 SystemContext/Axis/ViewModel：编译期性质。IPlcRuntimeGateway
// 只引用 contracts::* 纯 DTO，若被加入 SystemContext 等依赖本编译单元无法通过；
// 此处做抽象接口 + 多态可达性的冒烟验证。
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, DoesNotTouchSystemContext) {
    static_assert(std::is_abstract<IPlcRuntimeGateway>::value,
                  "IPlcRuntimeGateway must be abstract");
    EXPECT_TRUE(std::is_abstract<IPlcRuntimeGateway>::value);

    // 通过基类指针访问派生实现，确认接口可被 PlcRuntimeGateway 实现。
    std::unique_ptr<IPlcRuntimeGateway> gw =
        std::make_unique<PlcRuntimeGateway>(std::make_shared<fake::FakeModbusClient>());
    EXPECT_TRUE(gw != nullptr);
}

// ─────────────────────────────────────────────
// B 组默认拒绝：Gateway 缺省 gate 只放行 Group 0（当前 PLC 事实 B 组未开放）。
// "B 组禁用"不依赖调用方记得传 gate，而是 Gateway 的默认安全策略。
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, SubmitGantryRequest_Group1_DefaultRejected) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    PlcRuntimeGateway gateway(fake);  // 不传 gate → 默认只放行 Group 0

    auto g1 = PlcGroupIndex::tryCreate(1);
    ASSERT_TRUE(g1.has_value());

    auto res = gateway.submitGantryRequest(*g1, GantryRequest::couple(1));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, contracts::CommunicationResult::Status::ProtocolError);
    // 本地拒绝：零写入。
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());
}

// ─────────────────────────────────────────────
// 跨模块全局 I/O 串行化（评审修补①）：单轴写 / telemetry 读 / 龙门写并发时，
// 任意 Gantry Command('C') 之后必须紧跟 RequestSeq('S')，不得插入其它 I/O。
// 这是"共享串行化通道 + executeGroup 组锁"应保证的跨组件成组原子性。
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, ConcurrentIo_DoesNotInterleaveGantryCommit) {
    auto inner = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(inner);
    // 预置 telemetry 数据，使 readRuntime() 走通（避免失败分支干扰日志断言）。
    loadAxis(*inner, makeAxisBlock());
    loadGantry(*inner, makeGantryBlock());

    PlcRuntimeGateway gateway(client);

    auto slot = PlcAxisSlot::tryCreate(2);
    ASSERT_TRUE(slot.has_value());
    auto g0 = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g0.has_value());

    constexpr int kIters = 200;
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;

    // 线程 1：单轴写（走共享通道，单笔 writeMultipleRegisters 'S'）。
    threads.emplace_back([&] {
        while (!start.load()) { std::this_thread::yield(); }
        for (int i = 0; i < kIters; ++i) {
            gateway.writeAxis(*slot, PlcAxisCommand::makeSetManualSpeed(1.0f));
        }
    });
    // 线程 2：龙门提交（Command 'C' → RequestSeq 'S'，经 executeGroup 成组）。
    threads.emplace_back([&] {
        while (!start.load()) { std::this_thread::yield(); }
        for (int i = 0; i < kIters; ++i) {
            gateway.submitGantryRequest(*g0, GantryRequest::couple(i + 1));
        }
    });
    // 线程 3：运行读（telemetry 多笔 readHoldingRegisters 'R'）。
    threads.emplace_back([&] {
        while (!start.load()) { std::this_thread::yield(); }
        for (int i = 0; i < kIters; ++i) {
            gateway.readRuntime();
        }
    });

    start.store(true);
    for (auto& th : threads) th.join();

    const auto log = client->log();

    // 核心不变量：每个 Command('C') 后必须紧跟 RequestSeq('S')。
    // 若单轴写或 telemetry 读插入 C→S 之间，此断言即失败。
    for (std::size_t i = 0; i + 1 < log.size(); ++i) {
        if (log[i] == 'C') {
            EXPECT_EQ(log[i + 1], 'S')
                << "interleaved I/O inside Gantry commit at log index " << i;
        }
    }
    // 确保确实发生过龙门提交（不是空跑通过）。
    EXPECT_GT(static_cast<std::size_t>(std::count(log.begin(), log.end(), 'C')),
              static_cast<std::size_t>(0));
}

// ─────────────────────────────────────────────
// submitGantryRequestDetailed 必须原样传播提交阶段与 requestSeq：
// Command 成功、RequestSeq 失败 → CommitUncertain，不因经过 Gateway 退化为
// 普通通讯失败；上层据此知道必须等待 AckSeq（ackSeq == requestSeq）且禁止重发。
// ─────────────────────────────────────────────
TEST(PlcRuntimeGatewayTest, SubmitGantryDetailed_PropagatesCommitUncertain) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    // Command(D180) 成功；RequestSeq(D181) 起失败（响应丢失/超时语义）。
    fake->setFailureThreshold(static_cast<uint16_t>(
        layout::gantryCommand(0).requestSeq.value()));  // 181

    PlcRuntimeGateway gateway(fake);
    auto g0 = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g0.has_value());

    auto detail = gateway.submitGantryRequestDetailed(*g0, GantryRequest::couple(7));
    EXPECT_EQ(detail.state, contracts::GantrySubmitState::CommitUncertain);
    EXPECT_EQ(detail.requestSeq, 7);
    EXPECT_TRUE(detail.committedUnknown());
    EXPECT_FALSE(detail.result.ok());

    // 兼容入口只返回底层通讯结果（不携带阶段），仍为失败。
    auto plain = gateway.submitGantryRequest(*g0, GantryRequest::couple(8));
    EXPECT_FALSE(plain.ok());
}

}  // namespace
}  // namespace plc_vnext
