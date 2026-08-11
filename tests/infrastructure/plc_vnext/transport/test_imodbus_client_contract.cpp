// ============================================================================
// test_imodbus_client_contract.cpp —— Step 5 transport: 接口契约
// ============================================================================
// 对 FakeModbusClient 走通 IModbusClient 的完整契约：
//   FC01/FC03 读回脚本化数据、FC05/FC10 记录写入、脚本化故障返回失败态、
//   0 基址地址原样透传（不加 1/40001，Modbus 偏移由 layout 层负责）。
// ============================================================================
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"

namespace plc_vnext {
namespace {

using contracts::CommunicationResult;
using fake::FakeModbusClient;

// ───────────────────────────────────────────────
// FC01 读回脚本化的位数据
// ───────────────────────────────────────────────
TEST(IModbusClientContract, ReadCoils_ReturnsScriptedBits) {
    FakeModbusClient fake;
    fake.setCoil(5, true);
    fake.setCoil(7, true);  // 位 0 与位 2 置位

    std::vector<uint8_t> payload;
    auto res = fake.readCoils(5, 3, payload);
    ASSERT_TRUE(res.ok()) << res.diagnostic;

    ASSERT_EQ(payload.size(), 1u);
    // bit0 = 线圈 5 = true；bit2 = 线圈 7 = true
    EXPECT_TRUE((payload[0] & 0x01) != 0);
    EXPECT_TRUE((payload[0] & 0x04) != 0);
}

// ───────────────────────────────────────────────
// FC03 读回脚本化的字数据
// ───────────────────────────────────────────────
TEST(IModbusClientContract, ReadHolding_ReturnsScriptedWords) {
    FakeModbusClient fake;
    fake.setHoldingRegister(10, 0xABCD);
    fake.setHoldingRegister(11, 0x1234);

    std::vector<uint16_t> payload;
    auto res = fake.readHoldingRegisters(10, 2, payload);
    ASSERT_TRUE(res.ok());
    ASSERT_EQ(payload.size(), 2u);
    EXPECT_EQ(payload[0], 0xABCD);
    EXPECT_EQ(payload[1], 0x1234);
}

// ───────────────────────────────────────────────
// FC05 记录写入地址/值，并真正写入内存
// ───────────────────────────────────────────────
TEST(IModbusClientContract, WriteSingleCoil_RecordsAddressAndValue) {
    FakeModbusClient fake;
    auto res = fake.writeSingleCoil(0x0010, true);
    ASSERT_TRUE(res.ok());

    auto written = fake.writtenCoils();
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].address, 0x0010u);
    EXPECT_TRUE(written[0].value);

    // 写入应真实生效，可读回。
    auto coil = fake.readCoil(0x0010);
    ASSERT_TRUE(coil.has_value());
    EXPECT_TRUE(*coil);
}

// ───────────────────────────────────────────────
// FC10 写入序列正确
// ───────────────────────────────────────────────
TEST(IModbusClientContract, WriteMultiple_RecordsSequence) {
    FakeModbusClient fake;
    std::vector<uint16_t> values{0x0001, 0x0002, 0x0003};
    auto res = fake.writeMultipleRegisters(20, values);
    ASSERT_TRUE(res.ok());

    auto multi = fake.writtenMulti();
    ASSERT_EQ(multi.size(), 1u);
    EXPECT_EQ(multi[0].startAddress, 20u);
    ASSERT_EQ(multi[0].values.size(), 3u);
    EXPECT_EQ(multi[0].values[0], 0x0001u);
    EXPECT_EQ(multi[0].values[2], 0x0003u);

    // 每个字应真实落盘，可逐地址读回。
    EXPECT_EQ(*fake.readRegister(20), 0x0001u);
    EXPECT_EQ(*fake.readRegister(22), 0x0003u);
}

// ───────────────────────────────────────────────
// 脚本化故障 → CommunicationResult 失败态
// ───────────────────────────────────────────────
TEST(IModbusClientContract, ScriptedTransportFailure_ReturnsResult) {
    FakeModbusClient fake;
    fake.scriptTransportFailure(CommunicationResult::Status::NetworkError,
                                "simulated cable pull");

    std::vector<uint16_t> payload;
    auto res = fake.readHoldingRegisters(0, 1, payload);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(res.status, CommunicationResult::Status::NetworkError);
    EXPECT_EQ(res.diagnostic, "simulated cable pull");

    // 失败后 payload 不得被填成"看似正常"的数据。
    EXPECT_TRUE(payload.empty());
}

// ───────────────────────────────────────────────
// 0 基址地址原样透传（不加 1 / 40001）
// ───────────────────────────────────────────────
TEST(IModbusClientContract, ZeroBasedAddress_Passthrough) {
    FakeModbusClient fake;

    // 地址 0 必须被原样记录为 0，绝不能变成 1 或 40001。
    ASSERT_TRUE(fake.writeSingleCoil(0, true).ok());
    ASSERT_TRUE(fake.writeSingleRegister(0, 0x1234).ok());

    auto coils = fake.writtenCoils();
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0].address, 0u);

    auto regs = fake.writtenRegisters();
    ASSERT_EQ(regs.size(), 1u);
    EXPECT_EQ(regs[0].address, 0u);
    EXPECT_EQ(regs[0].value, 0x1234u);

    // 地址 0 的读/写走同一 0 基址空间，能读回自己写入的值。
    EXPECT_EQ(*fake.readRegister(0), 0x1234u);
}

// ───────────────────────────────────────────────
// 断连状态必须拒绝一切 I/O，且失败写不落盘、不记录
// ───────────────────────────────────────────────
TEST(IModbusClientContract, Disconnected_RejectsAllIo_AndNoWriteRecord) {
    FakeModbusClient fake;
    fake.setHoldingRegister(5, 0x1234);
    fake.setCoil(0, true);
    fake.setConnected(false);  // 模拟断线

    // 读：拒绝且不填 payload。
    std::vector<uint16_t> words;
    auto rr = fake.readHoldingRegisters(5, 1, words);
    EXPECT_FALSE(rr.ok());
    EXPECT_EQ(rr.status, CommunicationResult::Status::Disconnected);
    EXPECT_TRUE(words.empty());

    // 写：拒绝且绝不落盘 / 绝不留下写记录。
    auto wr = fake.writeSingleRegister(5, 0xABCD);
    EXPECT_FALSE(wr.ok());
    EXPECT_TRUE(fake.writtenRegisters().empty());
    EXPECT_EQ(*fake.readRegister(5), 0x1234u);  // 原值未被改写

    auto wc = fake.writeSingleCoil(0, false);
    EXPECT_FALSE(wc.ok());
    EXPECT_TRUE(fake.writtenCoils().empty());
    EXPECT_EQ(*fake.readCoil(0), true);  // 原值未被改写

    std::vector<uint8_t> bits;
    auto rc = fake.readCoils(0, 1, bits);
    EXPECT_FALSE(rc.ok());
    EXPECT_TRUE(bits.empty());
}

// ───────────────────────────────────────────────
// 地址范围越界必须拒绝，避免 uint16 地址回绕到 0
// ───────────────────────────────────────────────
TEST(IModbusClientContract, AddressRangeOverflow_Rejected) {
    FakeModbusClient fake;
    fake.setHoldingRegister(65535, 0x1234);

    // 65535 + 2 越过 65536 → 拒绝（否则回绕到 0，读到错误数据）。
    std::vector<uint16_t> payload;
    auto r = fake.readHoldingRegisters(65535, 2, payload);
    EXPECT_FALSE(r.ok());
    EXPECT_TRUE(payload.empty());

    // 恰好合法：地址上限 65535 读 1 个。
    auto ok = fake.readHoldingRegisters(65535, 1, payload);
    EXPECT_TRUE(ok.ok());
    ASSERT_EQ(payload.size(), 1u);
    EXPECT_EQ(payload[0], 0x1234u);

    // 写多个越过上限同样拒绝，且不落盘、不记录。
    auto w = fake.writeMultipleRegisters(65535, std::vector<uint16_t>{1, 2});
    EXPECT_FALSE(w.ok());
    EXPECT_TRUE(fake.writtenMulti().empty());
    EXPECT_EQ(*fake.readRegister(65535), 0x1234u);  // 原值未被改写
}

}  // namespace
}  // namespace plc_vnext
