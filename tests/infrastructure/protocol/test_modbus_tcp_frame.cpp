// tests/infrastructure/protocol/test_modbus_tcp_frame.cpp
// P4 Phase 1 — ModbusTcpFrame TDD 测试套件
//
// 测试策略:
//   1. MBAP 头构建与解析 — 基本正确性
//   2. 各 FC 请求帧构建 — 字节精确匹配
//   3. 各 FC 响应帧解析 — 寄存器数据提取
//   4. 异常帧检测 — 错误路径覆盖
//   5. 边界条件 — 空数据、帧过短、非法 Protocol ID

#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/ModbusTcpFrame.h"

using namespace plc::protocol::tcp;

// ═══════════════════════════════════════════════════
//  测试组 1: MBAP 头解析
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, ParseMbap_ValidFrame_ReturnsCorrectHeader) {
    // 构造合法的 MBAP 头
    std::vector<uint8_t> frame = {
        0x00, 0x01,  // Trans ID = 1
        0x00, 0x00,  // Protocol ID = 0x0000
        0x00, 0x06,  // Length = 6 (UnitID + FC + StartAddr + Quantity)
        0x01         // Unit ID = 1
    };
    auto hdr = ModbusTcpFrame::parseMbap(frame);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->transactionId, 1);
    EXPECT_EQ(hdr->length, 6);
}

TEST(ModbusTcpFrameTest, ParseMbap_TooShort_ReturnsNullopt) {
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00}; // 仅5字节
    auto hdr = ModbusTcpFrame::parseMbap(frame);
    EXPECT_FALSE(hdr.has_value());
}

TEST(ModbusTcpFrameTest, ParseMbap_InvalidProtocolId_ReturnsNullopt) {
    // Protocol ID != 0x0000
    std::vector<uint8_t> frame = {
        0x00, 0x01,
        0x00, 0x01,  // Protocol ID = 1 (非法)
        0x00, 0x06,
        0x01
    };
    auto hdr = ModbusTcpFrame::parseMbap(frame);
    EXPECT_FALSE(hdr.has_value());
}

TEST(ModbusTcpFrameTest, ParseMbap_LargeTransactionId) {
    std::vector<uint8_t> frame = {
        0xFF, 0xFE,  // Trans ID = 65534
        0x00, 0x00,
        0x00, 0x06,
        0x01
    };
    auto hdr = ModbusTcpFrame::parseMbap(frame);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->transactionId, 65534);
}

TEST(ModbusTcpFrameTest, ParseMbap_ZeroLength) {
    std::vector<uint8_t> frame = {
        0x00, 0x01,
        0x00, 0x00,
        0x00, 0x00,  // Length = 0
        0x01
    };
    auto hdr = ModbusTcpFrame::parseMbap(frame);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->length, 0);
}

// ═══════════════════════════════════════════════════
//  测试组 2: FC01 (读线圈) 请求帧构建
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, BuildReadCoils_BasicFrame) {
    auto frame = ModbusTcpFrame::buildReadCoils(1, 1, 0x0000, 10);
    // 帧总长 = 7(MBAP) + 1(FC) + 2(StartAddr) + 2(Quantity) = 12
    ASSERT_EQ(frame.size(), 12u);
    // MBAP: TransID=1
    EXPECT_EQ(frame[0], 0x00);  EXPECT_EQ(frame[1], 0x01);
    // MBAP: ProtoID=0
    EXPECT_EQ(frame[2], 0x00);  EXPECT_EQ(frame[3], 0x00);
    // MBAP: Length=6 (UnitID(1) + FC(1) + StartAddr(2) + Quantity(2))
    EXPECT_EQ(frame[4], 0x00);  EXPECT_EQ(frame[5], 0x06);
    // MBAP: UnitID=1
    EXPECT_EQ(frame[6], 0x01);
    // PDU: FC=01 (紧跟在 MBAP 后)
    EXPECT_EQ(frame[7], 0x01);
    // PDU: StartAddr=0x0000
    EXPECT_EQ(frame[8], 0x00);  EXPECT_EQ(frame[9], 0x00);
    // PDU: Quantity=10 = 0x000A
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x0A);
}

TEST(ModbusTcpFrameTest, BuildReadCoils_NonZeroStartAddr) {
    auto frame = ModbusTcpFrame::buildReadCoils(5, 3, 0x0064, 20); // start=100
    ASSERT_EQ(frame.size(), 12u);
    // MBAP: TransID=5
    EXPECT_EQ(frame[0], 0x00);  EXPECT_EQ(frame[1], 0x05);
    // MBAP: UnitID=3
    EXPECT_EQ(frame[6], 0x03);
    // PDU: FC=01
    EXPECT_EQ(frame[7], 0x01);
    // PDU: StartAddr=100 = 0x0064
    EXPECT_EQ(frame[8], 0x00);  EXPECT_EQ(frame[9], 0x64);
    // PDU: Quantity=20 = 0x0014
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x14);
}

// ═══════════════════════════════════════════════════
//  测试组 3: FC03 (读保持寄存器) 请求帧构建
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, BuildReadHoldingRegisters_BasicFrame) {
    auto frame = ModbusTcpFrame::buildReadHoldingRegisters(1, 1, 0x006B, 1); // addr 107
    ASSERT_EQ(frame.size(), 12u);
    // PDU: FC=03 (frame[7], 紧跟在 MBAP 后)
    EXPECT_EQ(frame[7], 0x03);
    // PDU: StartAddr=0x006B
    EXPECT_EQ(frame[8], 0x00);  EXPECT_EQ(frame[9], 0x6B);
    // PDU: Quantity=1
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x01);
}

TEST(ModbusTcpFrameTest, BuildReadHoldingRegisters_MultipleRegs) {
    auto frame = ModbusTcpFrame::buildReadHoldingRegisters(2, 1, 0x0000, 125);
    ASSERT_EQ(frame.size(), 12u);
    // PDU: FC=03 (frame[7])
    EXPECT_EQ(frame[7], 0x03);
    // PDU: Quantity=125 = 0x007D
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x7D);
}

// ═══════════════════════════════════════════════════
//  测试组 4: FC05 (写单线圈) 请求帧构建
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, BuildWriteSingleCoil_On) {
    auto frame = ModbusTcpFrame::buildWriteSingleCoil(1, 1, 0x000A, true);
    ASSERT_EQ(frame.size(), 12u);
    EXPECT_EQ(frame[7], 0x05);   // PDU: FC=05 (紧跟在 MBAP 后)
    // PDU: Addr=0x000A
    EXPECT_EQ(frame[8], 0x00);   EXPECT_EQ(frame[9], 0x0A);
    // PDU: Value=0xFF00 (ON)
    EXPECT_EQ(frame[10], 0xFF);  EXPECT_EQ(frame[11], 0x00);
}

TEST(ModbusTcpFrameTest, BuildWriteSingleCoil_Off) {
    auto frame = ModbusTcpFrame::buildWriteSingleCoil(1, 1, 0x0001, false);
    ASSERT_EQ(frame.size(), 12u);
    // Value=0x0000 (OFF)
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x00);
}

// ═══════════════════════════════════════════════════
//  测试组 5: FC06 (写单寄存器) 请求帧构建
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, BuildWriteSingleRegister_BasicFrame) {
    auto frame = ModbusTcpFrame::buildWriteSingleRegister(1, 1, 0x0001, 0x03FF);
    ASSERT_EQ(frame.size(), 12u);
    EXPECT_EQ(frame[7], 0x06);   // PDU: FC=06 (紧跟在 MBAP 后)
    // PDU: Addr=1
    EXPECT_EQ(frame[8], 0x00);   EXPECT_EQ(frame[9], 0x01);
    // PDU: Value=0x03FF
    EXPECT_EQ(frame[10], 0x03);  EXPECT_EQ(frame[11], 0xFF);
}

// ═══════════════════════════════════════════════════
//  测试组 6: FC16 (写多寄存器) 请求帧构建
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, BuildWriteMultipleRegisters_SingleRegister) {
    auto frame = ModbusTcpFrame::buildWriteMultipleRegisters(1, 1, 0x0001, {0x03FF});
    // PDU = FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) + 1*2 = 8
    // Total = MBAP(7) + PDU(8) = 15
    ASSERT_EQ(frame.size(), 15u);
    EXPECT_EQ(frame[7], 0x10);   // PDU: FC=16 (紧跟在 MBAP 后)
    // PDU: StartAddr=1
    EXPECT_EQ(frame[8], 0x00);   EXPECT_EQ(frame[9], 0x01);
    // PDU: Quantity=1
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x01);
    // PDU: ByteCount=2
    EXPECT_EQ(frame[12], 0x02);
    // PDU: Value=0x03FF
    EXPECT_EQ(frame[13], 0x03);  EXPECT_EQ(frame[14], 0xFF);
}

TEST(ModbusTcpFrameTest, BuildWriteMultipleRegisters_MultipleRegs) {
    auto frame = ModbusTcpFrame::buildWriteMultipleRegisters(
        1, 1, 0x0000, {0x0001, 0x0002, 0x0003});
    // PDU = FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) + 3*2 = 12
    // Total = MBAP(7) + PDU(12) = 19
    ASSERT_EQ(frame.size(), 19u);
    EXPECT_EQ(frame[7], 0x10);   // PDU: FC=16 (紧跟在 MBAP 后)
    EXPECT_EQ(frame[12], 0x06);  // PDU: ByteCount = 6
    // PDU: Value1
    EXPECT_EQ(frame[13], 0x00);  EXPECT_EQ(frame[14], 0x01);
    // PDU: Value2
    EXPECT_EQ(frame[15], 0x00);  EXPECT_EQ(frame[16], 0x02);
    // PDU: Value3
    EXPECT_EQ(frame[17], 0x00);  EXPECT_EQ(frame[18], 0x03);
}

TEST(ModbusTcpFrameTest, BuildWriteMultipleRegisters_EmptyValues) {
    // 空值列表 — 应仅包含 FC + StartAddr + Quantity + ByteCount=0
    auto frame = ModbusTcpFrame::buildWriteMultipleRegisters(
        1, 1, 0x0000, {});
    // PDU = FC(1) + StartAddr(2) + Quantity(2) + ByteCount(1) = 6
    // Total = MBAP(7) + PDU(6) = 13
    ASSERT_EQ(frame.size(), 13u);
    EXPECT_EQ(frame[7], 0x10);   // PDU: FC=16 (紧跟在 MBAP 后)
    // PDU: Quantity=0
    EXPECT_EQ(frame[10], 0x00);  EXPECT_EQ(frame[11], 0x00);
    // PDU: ByteCount=0
    EXPECT_EQ(frame[12], 0x00);
}

// ═══════════════════════════════════════════════════
//  测试组 7: 异常检测
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, CheckException_NormalResponse_ReturnsZero) {
    std::vector<uint8_t> data = {0x02}; // ByteCount for FC03 normal
    int exc = ModbusTcpFrame::checkException(0x03, 0x03, data);
    EXPECT_EQ(exc, 0);
}

TEST(ModbusTcpFrameTest, CheckException_ExceptionResponse_ReturnsExceptionCode) {
    // FC=0x83 = 0x03 | 0x80, 异常码 = 02 (Illegal Data Address)
    std::vector<uint8_t> data = {0x02};
    int exc = ModbusTcpFrame::checkException(0x03, 0x83, data);
    EXPECT_EQ(exc, 2);
}

TEST(ModbusTcpFrameTest, CheckException_ExceptionResponse_NoData_ReturnsNegativeOne) {
    // 异常帧但没有异常码数据
    std::vector<uint8_t> data = {};
    int exc = ModbusTcpFrame::checkException(0x03, 0x83, data);
    EXPECT_EQ(exc, -1);
}

TEST(ModbusTcpFrameTest, CheckException_DifferentFc) {
    // FC01 异常
    std::vector<uint8_t> data = {0x03}; // Illegal Data Value
    int exc = ModbusTcpFrame::checkException(0x01, 0x81, data);
    EXPECT_EQ(exc, 3);
}

// ═══════════════════════════════════════════════════
//  测试组 8: parseCoilResponse — FC01 响应解析
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, ParseCoilResponse_ValidResponse) {
    // 模拟 FC01 响应帧：读取 10 个线圈，返回 2 字节数据
    // 帧结构: MBAP(7) | PDU(FC + ByteCount + Data)
    // Length = UnitID(1) + FC(1) + ByteCount(1) + Data(2) = 5
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, // MBAP: Len=4
        0x01, // FC = 01 (frame[7], 紧跟在 MBAP 后)
        0x02, // ByteCount = 2
        0xCD, // Coil Data 1 (1100 1101)
        0x01  // Coil Data 2 (0000 0001)
    };
    auto coils = ModbusTcpFrame::parseCoilResponse(frame);
    ASSERT_EQ(coils.size(), 2u);
    EXPECT_EQ(coils[0], 0xCD);
    EXPECT_EQ(coils[1], 0x01);
}

TEST(ModbusTcpFrameTest, ParseCoilResponse_TooShort_ReturnsEmpty) {
    // 帧太短：仅 MBAP(7) + FC(1) = 8 字节，缺少 ByteCount
    std::vector<uint8_t> frame = {0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01};
    auto coils = ModbusTcpFrame::parseCoilResponse(frame);
    EXPECT_TRUE(coils.empty());
}

// ═══════════════════════════════════════════════════
//  测试组 9: parseRegisterResponse — FC03 响应解析
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, ParseRegisterResponse_SingleRegister) {
    // 模拟 FC03 响应帧：读取 1 个寄存器，值=0x000A
    // 帧结构: MBAP(7) | PDU(FC + ByteCount + Data)
    // Length = UnitID(1) + FC(1) + ByteCount(1) + Data(2) = 5
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x01, // MBAP: Len=4
        0x03, // FC = 03 (frame[7], 紧跟在 MBAP 后)
        0x02, // ByteCount = 2
        0x00, 0x0A // Value = 10
    };
    auto regs = ModbusTcpFrame::parseRegisterResponse(frame);
    ASSERT_EQ(regs.size(), 1u);
    EXPECT_EQ(regs[0], 10);
}

TEST(ModbusTcpFrameTest, ParseRegisterResponse_MultipleRegisters) {
    // 读取 3 个寄存器: 0x000A, 0x000B, 0x000C
    // Length = UnitID(1) + FC(1) + ByteCount(1) + Data(6) = 9
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x01, // MBAP: Len=8
        0x03, // FC (frame[7])
        0x06, // ByteCount = 6
        0x00, 0x0A, // Reg 1 = 10
        0x00, 0x0B, // Reg 2 = 11
        0x00, 0x0C  // Reg 3 = 12
    };
    auto regs = ModbusTcpFrame::parseRegisterResponse(frame);
    ASSERT_EQ(regs.size(), 3u);
    EXPECT_EQ(regs[0], 10);
    EXPECT_EQ(regs[1], 11);
    EXPECT_EQ(regs[2], 12);
}

TEST(ModbusTcpFrameTest, ParseRegisterResponse_TooShort_ReturnsEmpty) {
    // MBAP(7) + FC(1) + ByteCount(1) = 9 字节，但 ByteCount=2 数据不完整
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01,
        0x03, 0x02 // 仅 9 字节，缺少 2 字节寄存器数据
    };
    auto regs = ModbusTcpFrame::parseRegisterResponse(frame);
    EXPECT_TRUE(regs.empty());
}

// ═══════════════════════════════════════════════════
//  测试组 10: writeUint16BE — BigEndian 字节序
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, WriteUint16BE_LargeValue) {
    std::vector<uint8_t> buf(2);
    ModbusTcpFrame::writeUint16BE(buf, 0, 0xABCD);
    EXPECT_EQ(buf[0], 0xAB);
    EXPECT_EQ(buf[1], 0xCD);
}

TEST(ModbusTcpFrameTest, WriteUint16BE_ZeroValue) {
    std::vector<uint8_t> buf(2);
    ModbusTcpFrame::writeUint16BE(buf, 0, 0);
    EXPECT_EQ(buf[0], 0x00);
    EXPECT_EQ(buf[1], 0x00);
}

// ═══════════════════════════════════════════════════
//  测试组 11: appendUint16BE
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, AppendUint16BE_AppendsCorrectly) {
    std::vector<uint8_t> buf;
    ModbusTcpFrame::appendUint16BE(buf, 0x1234);
    ASSERT_EQ(buf.size(), 2u);
    EXPECT_EQ(buf[0], 0x12);
    EXPECT_EQ(buf[1], 0x34);
}

// ═══════════════════════════════════════════════════
//  测试组 12: 集成 — 请求-响应往返测试
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, Roundtrip_BuildAndParseRegisterResponse) {
    // Step 1: 构建 FC03 请求帧
    auto request = ModbusTcpFrame::buildReadHoldingRegisters(1, 1, 0x0000, 3);

    // Step 2: 模拟响应帧（符合标准 Modbus TCP 格式）
    // 帧结构: MBAP(7) | PDU(FC + ByteCount + Data)
    // Length = UnitID(1) + FC(1) + ByteCount(1) + 3*2 bytes = 8
    std::vector<uint8_t> response = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x08, 0x01, // MBAP: ID=1, Len=8
        0x03, // FC = 03 (frame[7])
        0x06, // ByteCount = 6
        0x00, 0x01, // Reg 1
        0x00, 0x02, // Reg 2
        0x00, 0x03  // Reg 3
    };

    // Step 3: 解析 MBAP
    auto hdr = ModbusTcpFrame::parseMbap(response);
    ASSERT_TRUE(hdr.has_value());
    EXPECT_EQ(hdr->transactionId, 1);
    EXPECT_EQ(hdr->length, 8);

    // Step 4: 检查异常（FC 在 frame[7]，数据 data 从 frame[8] 开始）
    int exc = ModbusTcpFrame::checkException(0x03, response[7], {response.begin() + 8, response.end()});
    EXPECT_EQ(exc, 0);

    // Step 5: 提取寄存器数据
    auto regs = ModbusTcpFrame::parseRegisterResponse(response);
    ASSERT_EQ(regs.size(), 3u);
    EXPECT_EQ(regs[0], 1);
    EXPECT_EQ(regs[1], 2);
    EXPECT_EQ(regs[2], 3);
}

TEST(ModbusTcpFrameTest, Roundtrip_CoilRequestResponse) {
    // 构建 FC01 读取 8 个线圈
    auto request = ModbusTcpFrame::buildReadCoils(1, 1, 0x0000, 8);

    // 模拟响应: 1 byte 数据 (8 coils)
    // Length = UnitID(1) + FC(1) + ByteCount(1) + Data(1) = 4
    std::vector<uint8_t> frame = {
        0x00, 0x01, 0x00, 0x00, 0x00, 0x03, 0x01, // MBAP: Len=3
        0x01, // FC = 01 (frame[7])
        0x01, // ByteCount = 1
        0xAA  // Coil Data
    };

    auto coils = ModbusTcpFrame::parseCoilResponse(frame);
    ASSERT_EQ(coils.size(), 1u);
    EXPECT_EQ(coils[0], 0xAA);
}

// ═══════════════════════════════════════════════════
//  测试组 13: 组合 FC — FC05 与 FC06 帧长度一致性
// ═══════════════════════════════════════════════════

TEST(ModbusTcpFrameTest, AllSingleWriteFrames_HaveSameLength) {
    auto fc05_on  = ModbusTcpFrame::buildWriteSingleCoil(1, 1, 0, true);
    auto fc05_off = ModbusTcpFrame::buildWriteSingleCoil(1, 1, 0, false);
    auto fc06     = ModbusTcpFrame::buildWriteSingleRegister(1, 1, 0, 0);
    auto fc01     = ModbusTcpFrame::buildReadCoils(1, 1, 0, 1);
    auto fc03     = ModbusTcpFrame::buildReadHoldingRegisters(1, 1, 0, 1);

    // 标准 Modbus TCP: MBAP(7) + PDU(5) = 12 字节
    EXPECT_EQ(fc05_on.size(), 12u);
    EXPECT_EQ(fc05_off.size(), 12u);
    EXPECT_EQ(fc06.size(), 12u);
    EXPECT_EQ(fc01.size(), 12u);
    EXPECT_EQ(fc03.size(), 12u);
}
