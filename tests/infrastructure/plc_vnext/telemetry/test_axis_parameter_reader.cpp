// ============================================================================
// test_axis_parameter_reader.cpp —— 阶段3：参数区读取器单元测试
// ============================================================================
// 用 FakeModbusClient 预置 slot 0 参数区寄存器（D1064 相对原点记录 / D1096 绝对
// 定位距离 / D1128 相对定位距离 / D1160 软负 / D1192 软正 / D1228 软限位控制），
// 验证 AxisParameterReader 解码正确（REAL 低字在前 CDAB；WORD 原样），并验证任一
// 字段读取失败时 trusted=false。
// ============================================================================
#include <memory>
#include <vector>

#include "gtest/gtest.h"
#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"
#include "infrastructure/plc_vnext/telemetry/AxisParameterReader.h"

namespace {

using plc_vnext::codec::ByteOrder;
using plc_vnext::codec::EndianPolicy;
using plc_vnext::codec::RegisterCodec;
using plc_vnext::codec::WordOrder;
using plc_vnext::fake::FakeModbusClient;
using plc_vnext::telemetry::AxisParameterReader;

constexpr EndianPolicy kPlcEndian{ByteOrder::BigEndian, WordOrder::LowWordFirst};

// 把 float 编码成 2 个保持寄存器字并预置到假 PLC。
void presetFloat(FakeModbusClient& c, uint16_t base, float v) {
    const auto words = RegisterCodec::encodeFloat(v, kPlcEndian);
    ASSERT_EQ(words.size(), 2u);
    c.setHoldingRegister(base, words[0]);
    c.setHoldingRegister(base + 1, words[1]);
}

TEST(AxisParameterReaderTest, DecodesSlotZeroParameterArea) {
    auto client = std::make_shared<FakeModbusClient>();
    presetFloat(*client, 1064, 5.0f);   // relZeroRecord  D1064
    presetFloat(*client, 1096, 20.0f);  // absMoveDistance D1096
    presetFloat(*client, 1128, 8.0f);   // relMoveDistance D1128
    presetFloat(*client, 1160, -50.0f); // softNegLimit    D1160
    presetFloat(*client, 1192, 50.0f);  // softPosLimit    D1192
    client->setHoldingRegister(1228, 0x0003);  // softLimitControl D1228 (bit0+bit1)

    AxisParameterReader reader(client);
    const auto s = reader.read(0);
    EXPECT_TRUE(s.trusted);
    EXPECT_EQ(s.slot, 0);
    EXPECT_FLOAT_EQ(s.relZeroRecord, 5.0f);
    EXPECT_FLOAT_EQ(s.absMoveDistance, 20.0f);
    EXPECT_FLOAT_EQ(s.relMoveDistance, 8.0f);
    EXPECT_FLOAT_EQ(s.softNegLimit, -50.0f);
    EXPECT_FLOAT_EQ(s.softPosLimit, 50.0f);
    EXPECT_EQ(s.softLimitControl, 0x0003u);
}

TEST(AxisParameterReaderTest, UntrustedOnReadFailure) {
    auto client = std::make_shared<FakeModbusClient>();
    // 注入一次通讯故障：首个字段读取失败 → 整快照不可信。
    client->scriptTransportFailure(
        plc_vnext::contracts::CommunicationResult::Status::Timeout);
    AxisParameterReader reader(client);
    const auto s = reader.read(0);
    EXPECT_FALSE(s.trusted);
}

TEST(AxisParameterReaderTest, OutOfRangeSlotUntrusted) {
    auto client = std::make_shared<FakeModbusClient>();
    AxisParameterReader reader(client);
    EXPECT_FALSE(reader.read(16).trusted);
    EXPECT_FALSE(reader.read(-1).trusted);
}

}  // namespace
