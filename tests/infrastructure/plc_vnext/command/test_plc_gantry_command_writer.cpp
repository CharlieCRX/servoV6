// ============================================================================
// test_plc_gantry_command_writer.cpp —— Step 9 command: 龙门请求写入（红）
// ============================================================================
// 依据《TDD实施文档》Step 9 与《PLC变量协议_Modbus最终地址表.md》§7：
//   WriteCouple_CommandThenSeq   先写 D180 Command=1，再在独立事务写 D181..182 Seq
//   WriteOrder_CommandBeforeSeq  调用记录证明 Command 成功后才写 RequestSeq
//   ProvidedSeq_IsWrittenExactly writer 原样写 application 提供的 N+1，不自增
//   GroupIndex_AddressesGroup    command(1) 落在 B 组槽位 D184/D185
//   AckSeqAlignment_NotDecidedHere 确认/超时判定交给 application（writer 不读回）
// 并覆盖用户 Step 9 分析中的 A(协议/地址/编码) B(写入顺序/失败无重放) C(成组原子)。
//
// 全部经 FakeModbusClient 离线执行；不接触真实 PLC / AsioModbusTcpClient。
// 一个龙门请求 = A 组事务（SYN0 + X1 + X2），writer 只提交 Command+RequestSeq；
// 前置准入、GearIn/GearOut、State/InGear 确认均不属本 writer（Step 10 reader）。
// ============================================================================
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/command/PlcGantryCommandWriter.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext {
namespace {

using contracts::CommunicationResult;
using contracts::GantryRequest;
using contracts::PlcGroupIndex;

// ------------------------------------------------------------------
// 单一交错日志客户端：把"单寄存器写（Command）"与"多寄存器写（RequestSeq）"
// 记入同一条日志，供严格断言 Command→RequestSeq 的提交顺序与成组原子性。
// ------------------------------------------------------------------
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

    CommunicationResult readCoils(uint16_t a, uint16_t c,
                                  std::vector<uint8_t>& p) override {
        return m_inner->readCoils(a, c, p);
    }
    CommunicationResult readHoldingRegisters(uint16_t a, uint16_t c,
                                             std::vector<uint16_t>& p) override {
        return m_inner->readHoldingRegisters(a, c, p);
    }
    CommunicationResult writeSingleCoil(uint16_t a, bool v) override {
        return m_inner->writeSingleCoil(a, v);
    }
    CommunicationResult writeSingleRegister(uint16_t a, uint16_t v) override {
        auto r = m_inner->writeSingleRegister(a, v);  // Command 单寄存器
        { std::lock_guard<std::mutex> l(m_logMtx); m_log.push_back('C'); }
        return r;
    }
    CommunicationResult writeMultipleRegisters(
        uint16_t a, const std::vector<uint16_t>& vals) override {
        auto r = m_inner->writeMultipleRegisters(a, vals);  // RequestSeq 多寄存器
        { std::lock_guard<std::mutex> l(m_logMtx); m_log.push_back('S'); }
        return r;
    }

private:
    std::shared_ptr<fake::FakeModbusClient> m_inner;
    mutable std::mutex m_logMtx;
    std::vector<char> m_log;
};

// 组号别名，方便阅读。
const PlcGroupIndex kG0(0);
const PlcGroupIndex kG1(1);

// ─────────────────────────────────────────────
// 建立联动：先 Command(D180)=1，再独立事务写 RequestSeq(D181..182)
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, WriteCouple_CommandThenSeq) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    auto res = writer.submit(kG0, GantryRequest::couple(7));  // N=6 → N+1=7
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    // 严格顺序：Command 记录在 RequestSeq 之前。
    EXPECT_EQ(client->log(), (std::vector<char>{'C', 'S'}));

    // Command：D180 = 1（FC06 单寄存器）。
    auto single = fake->writtenRegisters();
    ASSERT_EQ(single.size(), 1u);
    EXPECT_EQ(single[0].address, layout::gantryCommand(0).command.value());
    EXPECT_EQ(single[0].value, 1u);

    // RequestSeq：D181..182 = {0x0007, 0x0000}（CDAB 低字在前）。
    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    EXPECT_EQ(multi[0].startAddress, layout::gantryCommand(0).requestSeq.value());
    ASSERT_EQ(multi[0].values.size(), 2u);
    EXPECT_EQ(multi[0].values[0], 0x0007u);
    EXPECT_EQ(multi[0].values[1], 0x0000u);
}

// ─────────────────────────────────────────────
// Command 正常响应后才写 RequestSeq（调用记录证明顺序）
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, WriteOrder_CommandBeforeSeq) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(1)).ok());
    EXPECT_EQ(client->log(), (std::vector<char>{'C', 'S'}));
}

// ─────────────────────────────────────────────
// writer 原样写 application 提供的 N+1，不自行生成/递增序号
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, ProvidedSeq_IsWrittenExactly) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(7)).ok());
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(1000)).ok());

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 2u);
    // 7   = 0x00000007 → 低字在前 {0x0007, 0x0000}
    EXPECT_EQ(multi[0].values[0], 0x0007u);
    EXPECT_EQ(multi[0].values[1], 0x0000u);
    // 1000 = 0x000003E8 → {0x03E8, 0x0000}
    EXPECT_EQ(multi[1].values[0], 0x03E8u);
    EXPECT_EQ(multi[1].values[1], 0x0000u);
}

// ─────────────────────────────────────────────
// DINT 正/负/边界值编码（CDAB 低字在前，对齐 codec 自测断言）
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, RequestSeq_DintCoding_AllCases) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    // -65536 → {0x0000, 0xFFFF}
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(-65536)).ok());
    // INT32_MAX = 0x7FFFFFFF → {0xFFFF, 0x7FFF}
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(INT32_MAX)).ok());
    // INT32_MIN = 0x80000000 → {0x0000, 0x8000}
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(INT32_MIN)).ok());

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 3u);
    EXPECT_EQ(multi[0].values[0], 0x0000u); EXPECT_EQ(multi[0].values[1], 0xFFFFu);
    EXPECT_EQ(multi[1].values[0], 0xFFFFu); EXPECT_EQ(multi[1].values[1], 0x7FFFu);
    EXPECT_EQ(multi[2].values[0], 0x0000u); EXPECT_EQ(multi[2].values[1], 0x8000u);
}

// ─────────────────────────────────────────────
// 组号寻址：command(1) 落在 B 组槽位 D184/D185
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, GroupIndex_AddressesGroup) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    ASSERT_TRUE(writer.submit(kG1, GantryRequest::decouple(3)).ok());

    auto single = fake->writtenRegisters();
    ASSERT_EQ(single.size(), 1u);
    EXPECT_EQ(single[0].address, layout::gantryCommand(1).command.value());  // D184

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    EXPECT_EQ(multi[0].startAddress, layout::gantryCommand(1).requestSeq.value());  // D185
}

// ─────────────────────────────────────────────
// 非法 Command（非 0/1/2/3）：拒绝且不产生任何写操作
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, IllegalCommand_NoWrite) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    GantryRequest bad;
    bad.command = static_cast<contracts::GantryCommandKind>(5);  // 越界命令码
    auto res = writer.submit(kG0, bad);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::ProtocolError);
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());
}

// ─────────────────────────────────────────────
// 组提交策略（注入 gate）：Group[1].Valid=FALSE 时拒绝 B 组，A 组放行
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, GroupGate_RejectsDisabledGroup) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter::GroupGate gate =
        [](PlcGroupIndex g) { return g.value() == 0; };  // 只放行 A 组
    command::PlcGantryCommandWriter writer(fake, gate);

    // A 组（g=0）：允许。
    EXPECT_TRUE(writer.submit(kG0, GantryRequest::couple(1)).ok());
    // B 组（g=1）：拒绝且不写。
    auto res = writer.submit(kG1, GantryRequest::couple(1));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::ProtocolError);
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);  // 只有 A 组那一次 Command
    EXPECT_EQ(fake->writtenMulti().size(), 1u);
}

// ─────────────────────────────────────────────
// 解除联动与故障复位：Command 分别为 2 / 3，且都走 Command→RequestSeq 顺序
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, DecoupleAndReset_CommandCodeAndOrder) {
    {
        auto fake = std::make_shared<fake::FakeModbusClient>();
        auto client = std::make_shared<OrderLoggingClient>(fake);
        command::PlcGantryCommandWriter writer(client);
        ASSERT_TRUE(writer.submit(kG0, GantryRequest::decouple(8)).ok());
        EXPECT_EQ(client->log(), (std::vector<char>{'C', 'S'}));
        EXPECT_EQ(fake->writtenRegisters()[0].value, 2u);  // 解除=2
    }
    {
        auto fake = std::make_shared<fake::FakeModbusClient>();
        auto client = std::make_shared<OrderLoggingClient>(fake);
        command::PlcGantryCommandWriter writer(client);
        ASSERT_TRUE(writer.submit(kG0, GantryRequest::reset(9)).ok());
        EXPECT_EQ(client->log(), (std::vector<char>{'C', 'S'}));
        EXPECT_EQ(fake->writtenRegisters()[0].value, 3u);  // 复位=3
    }
}

// ─────────────────────────────────────────────
// Command 失败：不写 RequestSeq，返回真实通讯错误
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, CommandFailure_NoSeqWrite_ReturnsRealError) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    fake->scriptTransportFailure(CommunicationResult::Status::NetworkError,
                                 "simulated cable pull");
    auto res = writer.submit(kG0, GantryRequest::couple(1));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::NetworkError);
    // 只有一次 Command 尝试（日志只含 'C'），未发 RequestSeq。
    EXPECT_EQ(client->log(), (std::vector<char>{'C'}));
    EXPECT_TRUE(fake->writtenMulti().empty());
}

// ─────────────────────────────────────────────
// Command 成功、RequestSeq 失败：返回真实错误，且不自动重发 Command
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, SeqFailure_NoCommandResend_ReturnsRealError) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    // Command(D180) 成功；RequestSeq(D181) 起失败。
    fake->setFailureThreshold(static_cast<uint16_t>(
        layout::gantryCommand(0).requestSeq.value()));  // 181

    auto res = writer.submit(kG0, GantryRequest::couple(1));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::NetworkError);

    // 只写了一次 Command，RequestSeq 失败未落盘；不自动重发 Command。
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);
    EXPECT_TRUE(fake->writtenMulti().empty());
    // submit 已返回，日志再无追加（无重试/无重放）。
    EXPECT_EQ(client->log(), (std::vector<char>{'C', 'S'}));
}

// ─────────────────────────────────────────────
// 断线：不落盘、无重放；重连后仅显式提交才写入
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, NoReplayAfterDisconnect) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    fake->setConnected(false);
    auto res = writer.submit(kG0, GantryRequest::couple(1));
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::Disconnected);
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());

    // 重连后 writer 不自动重放建立命令。
    fake->setConnected(true);
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());

    // 下一次显式提交才写入（全新事务）。
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(2)).ok());
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);
}

// ─────────────────────────────────────────────
// writer 不维护跨请求序号状态：相同 RequestSeq 显式重提时原样重写，
// "相同 seq 不重复提交 / 新命令必须新 seq"由上层（session/Gateway）保证。
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, ProvidedSeq_Stateless_NoHiddenDedup) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    // 两次显式提交相同 seq：writer 均原样写入，不自行去重。
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(7)).ok());
    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(7)).ok());

    auto multi = fake->writtenMulti();
    ASSERT_EQ(multi.size(), 2u);
    EXPECT_EQ(multi[0].values[0], 0x0007u);
    EXPECT_EQ(multi[1].values[0], 0x0007u);
    // 无任何读回（AckSeq 对齐判定不在 writer）。
    EXPECT_EQ(fake->readCount(), 0u);
}

// ─────────────────────────────────────────────
// writer 只提交，不做读回确认（AckSeq 对齐交由 application）
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, AckSeqAlignment_NotDecidedHere) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    ASSERT_TRUE(writer.submit(kG0, GantryRequest::couple(1)).ok());
    // 未发生任何读操作（writer 不做 AckSeq 读回/确认）。
    EXPECT_EQ(fake->readCount(), 0u);
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);
    EXPECT_EQ(fake->writtenMulti().size(), 1u);
}

// ─────────────────────────────────────────────
// 串行化：并发提交时，PLC 看到每个请求仍是完整的 Command→RequestSeq，
// 不出现 CommandA→CommandB→RequestSeqA 的交叉。
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, ConcurrentSubmits_CommandSeqNeverInterleave) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    constexpr int kThreads = 8;
    constexpr int kIters = 50;
    std::atomic<bool> start{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t] {
            while (!start.load()) { std::this_thread::yield(); }
            for (int i = 0; i < kIters; ++i) {
                // 每个请求使用不同 requestSeq，便于万一出错时定位。
                writer.submit(kG0, GantryRequest::couple(t * kIters + i + 1));
            }
        });
    }
    start.store(true);
    for (auto& th : threads) th.join();

    auto log = client->log();
    ASSERT_EQ(log.size(), static_cast<size_t>(kThreads * kIters * 2));
    // 严格交替 C,S,C,S,...：任一笔请求的 RequestSeq 都不会被其它请求插入隔开。
    for (size_t i = 0; i + 1 < log.size(); ++i) {
        EXPECT_NE(log[i], log[i + 1])
            << "interleaved Command/RequestSeq at log index " << i;
    }
}

// ─────────────────────────────────────────────
// None=0 是 PLC 寄存器的"无命令状态"，不是有效事务：必须本地拒绝、零写入
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, NoneCommand_IsRejectedWithoutAnyWrite) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto client = std::make_shared<OrderLoggingClient>(fake);
    command::PlcGantryCommandWriter writer(client);

    // 默认构造：command=None(0)、requestSeq=0。
    contracts::GantryRequest none;
    auto detail = writer.submitDetailed(kG0, none);

    EXPECT_EQ(detail.state, contracts::GantrySubmitState::RejectedLocally);
    EXPECT_FALSE(detail.ok());
    EXPECT_EQ(detail.result.status, CommunicationResult::Status::ProtocolError);

    // 零写入：没有任何 I/O 发出（日志为空），也不写 RequestSeq。
    EXPECT_EQ(client->log(), (std::vector<char>{}));
    EXPECT_TRUE(fake->writtenRegisters().empty());
    EXPECT_TRUE(fake->writtenMulti().empty());
}

// ─────────────────────────────────────────────
// Command 成功、RequestSeq 失败 → CommitUncertain：
// PLC 可能已收 Command 也可能没收到，绝不能据此生成新序号重发
// ─────────────────────────────────────────────
TEST(PlcGantryCommandWriterTest, SeqFailure_ReportsCommitUncertain) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    command::PlcGantryCommandWriter writer(fake);

    // Command(D180) 成功；RequestSeq(D181) 起失败（响应丢失/超时语义）。
    fake->setFailureThreshold(static_cast<uint16_t>(
        layout::gantryCommand(0).requestSeq.value()));  // 181

    auto detail = writer.submitDetailed(kG0, GantryRequest::couple(1));

    // 提交阶段必须是 CommitUncertain（Command 已写、RequestSeq 结果未知）。
    EXPECT_EQ(detail.state, contracts::GantrySubmitState::CommitUncertain);
    EXPECT_FALSE(detail.ok());
    EXPECT_TRUE(detail.committedUnknown());
    EXPECT_FALSE(detail.result.ok());

    // 只写了一次 Command；RequestSeq 未落盘；不自动重发 Command。
    EXPECT_EQ(fake->writtenRegisters().size(), 1u);
    EXPECT_TRUE(fake->writtenMulti().empty());
}

}  // namespace
}  // namespace plc_vnext



