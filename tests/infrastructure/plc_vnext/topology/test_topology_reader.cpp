// ============================================================================
// test_topology_reader.cpp —— Step 6 topology: PlcTopologyReader 双读
// ============================================================================
// 对应 6.1 红用例 + 评审阻塞项修复（用 FakeModbus / gmock）：
//   StableRevision_TwoReads        —— Header→Body→Header，Revision 一致 → 成功
//   RequestSequence_HeaderThen125Then53ThenHeader
//                                  —— 严格断言 FC03 分片序列 Header(6)→125→53→Header(6)
//   RevisionChanged_ReturnsChanged —— 两次 Header Revision 不同 → ReadResult::RevisionChanged
//   ConfigValidFalse_ReturnsCoherentInvalidSnapshot
//                                  —— 读取成功，快照保留 ConfigValid=false 与 ConfigErrorCode
//   SchemaVersionUnsupported_ReturnsDecode / DuplicateSlot_ReturnsDecode
//                                  —— Validator 接入读取路径：客户端安全问题 → Decode
//   DisabledGroupGarbage_IsNotRejected
//                                  —— B 组禁用残留脏数据不触发客户端错误
//   BGroupEnabled_InvalidRoles_NotRejected
//                                  —— B 组启用但角色全无效 → 不报错
//   EnabledRole_InvalidSlot_Rejected
//                                  —— 有效角色槽位越界（配置损坏）→ Decode
// ============================================================================
#include <cstdint>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"
#include "infrastructure/plc_vnext/topology/PlcTopologyReader.h"
#include "tests/infrastructure/plc_vnext/support/TopologyFixture.h"

namespace plc_vnext::topology {
namespace {

using contracts::CommunicationResult;
using testing::_;
using testing::DoAll;
using testing::InSequence;
using testing::Return;
using testing::SetArgReferee;

using ReadResult = contracts::ReadResult<contracts::TopologySnapshot>;

// 拓扑 Body 按 FC03 上限 125 分片的两段大小。
constexpr int kBodyChunk1 = 125;                     // D1400..D1524
constexpr int kBodyChunk2 = layout::topologyTotalWords() - kBodyChunk1;  // D1525..D1577 = 53

class MockModbusClient : public transport::IModbusClient {
public:
    MOCK_METHOD(bool, isConnected, (), (const, override));
    MOCK_METHOD(void, requestReconnect, (), (override));
    MOCK_METHOD(CommunicationResult, readCoils,
                (uint16_t, uint16_t, std::vector<uint8_t>&), (override));
    MOCK_METHOD(CommunicationResult, readHoldingRegisters,
                (uint16_t, uint16_t, std::vector<uint16_t>&), (override));
    MOCK_METHOD(CommunicationResult, writeSingleCoil, (uint16_t, bool), (override));
    MOCK_METHOD(CommunicationResult, writeSingleRegister, (uint16_t, uint16_t), (override));
    MOCK_METHOD(CommunicationResult, writeMultipleRegisters,
                (uint16_t, const std::vector<uint16_t>&), (override));
};

// 把整块 178 字拓扑按地址 1400..1577 写入 Fake RAM。
void loadRegisters(fake::FakeModbusClient& fake, const std::vector<uint16_t>& regs) {
    for (std::size_t i = 0; i < regs.size(); ++i) {
        fake.setHoldingRegister(
            static_cast<uint16_t>(layout::topologyMagic().value() + i), regs[i]);
    }
}

void loadFullTopology(fake::FakeModbusClient& fake) {
    loadRegisters(fake, test::makeValidTopologyRegisters(7));
}

// D1400..D1405 头部：Magic=0x013527C6, Schema=1, Revision=revision
std::vector<uint16_t> makeHeader(int32_t revision) {
    std::vector<uint16_t> h(6, 0);
    h[0] = 0x27C6;              // Magic 低字
    h[1] = 0x0135;              // Magic 高字
    h[2] = 1;                   // SchemaVersion
    const uint32_t u = static_cast<uint32_t>(revision);
    h[4] = static_cast<uint16_t>(u & 0xFFFFu);
    h[5] = static_cast<uint16_t>((u >> 16) & 0xFFFFu);
    return h;
}

// ───────────────────────────────────────────────
// Header→Body→Header，Revision 一致 → 成功
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, StableRevision_TwoReads) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    loadFullTopology(*fake);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_TRUE(res.hasValue()) << res.diagnostic();

    const auto& snap = res.value();
    EXPECT_EQ(snap.header.magic, test::kFixtureMagic);
    EXPECT_EQ(snap.header.revision, 7);
    EXPECT_TRUE(snap.header.configValid);
    EXPECT_TRUE(snap.groups[0].valid);
    EXPECT_EQ(snap.groups[0].roles[0].plcAxisIndex, 0);
}

// ───────────────────────────────────────────────
// 严格断言请求序列：Header(6) → 125 → 53 → Header(6)
// （FC03 单次 ≤ 125，整块 178 必须拆为两段拼接后解码）
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, RequestSequence_HeaderThen125Then53ThenHeader) {
    auto mock = std::make_shared<MockModbusClient>();
    auto full = test::makeValidTopologyRegisters(7);
    auto headerA = makeHeader(7);
    auto headerB = makeHeader(7);
    const std::vector<uint16_t> chunk1(full.begin(), full.begin() + kBodyChunk1);
    const std::vector<uint16_t> chunk2(full.begin() + kBodyChunk1, full.end());
    ASSERT_EQ(chunk1.size(), static_cast<std::size_t>(kBodyChunk1));
    ASSERT_EQ(chunk2.size(), static_cast<std::size_t>(kBodyChunk2));

    InSequence seq;
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), 6, _))
        .WillOnce(DoAll(SetArgReferee<2>(headerA), Return(CommunicationResult::sent())));
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), kBodyChunk1, _))
        .WillOnce(DoAll(SetArgReferee<2>(chunk1), Return(CommunicationResult::sent())));
    // 第二段从 D1400+125 = D1525 起，读 53 字
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value() + kBodyChunk1,
                                            kBodyChunk2, _))
        .WillOnce(DoAll(SetArgReferee<2>(chunk2), Return(CommunicationResult::sent())));
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), 6, _))
        .WillOnce(DoAll(SetArgReferee<2>(headerB), Return(CommunicationResult::sent())));

    PlcTopologyReader reader(mock);
    auto res = reader.read();
    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    EXPECT_EQ(res.value().header.revision, 7);
}

// ───────────────────────────────────────────────
// 两次 Header Revision 不同 → ReadResult::RevisionChanged
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, RevisionChanged_ReturnsChanged) {
    auto mock = std::make_shared<MockModbusClient>();
    auto full = test::makeValidTopologyRegisters(7);
    auto headerA = makeHeader(7);
    auto headerB = makeHeader(8);  // 第二次读到的 Revision 变了
    const std::vector<uint16_t> chunk1(full.begin(), full.begin() + kBodyChunk1);
    const std::vector<uint16_t> chunk2(full.begin() + kBodyChunk1, full.end());

    InSequence seq;
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), 6, _))
        .WillOnce(DoAll(SetArgReferee<2>(headerA), Return(CommunicationResult::sent())));
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), kBodyChunk1, _))
        .WillOnce(DoAll(SetArgReferee<2>(chunk1), Return(CommunicationResult::sent())));
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value() + kBodyChunk1,
                                            kBodyChunk2, _))
        .WillOnce(DoAll(SetArgReferee<2>(chunk2), Return(CommunicationResult::sent())));
    EXPECT_CALL(*mock, readHoldingRegisters(layout::topologyMagic().value(), 6, _))
        .WillOnce(DoAll(SetArgReferee<2>(headerB), Return(CommunicationResult::sent())));

    PlcTopologyReader reader(mock);
    auto res = reader.read();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(), ReadResult::FailureKind::RevisionChanged);
}

// ───────────────────────────────────────────────
// 读取成功，快照保留 ConfigValid=false 与 ConfigErrorCode
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, ConfigValidFalse_ReturnsCoherentInvalidSnapshot) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    test::setConfigValid(regs, false);
    test::setConfigErrorCode(regs, 42);
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    EXPECT_FALSE(res.value().header.configValid);
    EXPECT_EQ(res.value().header.configErrorCode, 42);
}

// ───────────────────────────────────────────────
// Validator 接入：稳定 Revision 但 SchemaVersion 不支持 → Decode
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, SchemaVersionUnsupported_ReturnsDecode) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    const int off = layout::topologySchemaVersion().value() - layout::topologyMagic().value();
    regs[off] = 2;  // D1402 SchemaVersion=2，不支持
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(), ReadResult::FailureKind::Decode);
    EXPECT_FALSE(res.diagnostic().empty());
}

// ───────────────────────────────────────────────
// Validator 接入：稳定 Revision 但有效角色槽位重复 → Decode
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, DuplicateSlot_ReturnsDecode) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    // 让 Role[1] 有效且占用与 Role[0] 相同的槽位 0 → 重复
    test::setRole(regs, 0, 1, true, 0, 2, 0, 0, 2);
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(), ReadResult::FailureKind::Decode);
    EXPECT_FALSE(res.diagnostic().empty());
}

// ───────────────────────────────────────────────
// B 组（禁用）未初始化残留数据不得触发客户端错误：
// 无效角色字段（MotionMode/Reserved 等脏值）一律被跳过，仍成功返回快照
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, DisabledGroupGarbage_IsNotRejected) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    // 模拟现场 B 组禁用但 Role 字段残留脏数据（如 16800）
    test::setRoleMotionMode(regs, 1, 0, 16800);
    test::setRoleReserved(regs, 1, 0, 16800);
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    // 原始脏值仍被机械解码保留，但 B 组无效角色不产生任何校验错误
    const auto& b0 = res.value().groups[1].roles[0];
    EXPECT_FALSE(b0.valid);
    EXPECT_EQ(b0.motionMode, 16800);
    EXPECT_EQ(b0.reserved, 16800);
}

// ───────────────────────────────────────────────
// 现场不保证 B 组 Valid=off：即使 B 组被启用（Valid=true），只要其角色均无效
// （Valid=false）就不应报错（"第二组有效本身不构成客户端错误"）。
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, BGroupEnabled_InvalidRoles_NotRejected) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    test::setGroupValid(regs, 1, true, true, 1);  // B 组启用，但角色全无效
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_TRUE(res.hasValue()) << res.diagnostic();
    EXPECT_TRUE(res.value().groups[1].valid);
}

// ───────────────────────────────────────────────
// 唯一应报错的硬边界：某角色 Valid=true 但槽位越界（现场配置损坏）→ Decode
// ───────────────────────────────────────────────
TEST(TopologyReaderTest, EnabledRole_InvalidSlot_Rejected) {
    auto fake = std::make_shared<fake::FakeModbusClient>();
    auto regs = test::makeValidTopologyRegisters(7);
    test::setRole(regs, 0, 2, true, 20, 0, 0, 0, 0);  // 有效角色槽位越界 0..15
    loadRegisters(*fake, regs);

    PlcTopologyReader reader(fake);
    auto res = reader.read();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(), ReadResult::FailureKind::Decode);
    EXPECT_FALSE(res.diagnostic().empty());
}

}  // namespace
}  // namespace plc_vnext::topology
