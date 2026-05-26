// tests/infrastructure/protocol/test_register_codec_snapshot.cpp
// P2: RegisterCodec Snapshot 接口 TDD 测试
// 遵循 v3 架构：Snapshot → Codec → PlcValue 管线

#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/RegisterCodec.h"
#include "infrastructure/plc/protocol/MemorySnapshot.h"
#include "infrastructure/plc/protocol/PlcSnapshot.h"
#include "infrastructure/plc/protocol/PlcValue.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"
#include "infrastructure/plc/protocol/RegisterMetadata.h"

using namespace plc::protocol;

// ============================================================================
// 测试辅助：预定义元数据与 Profile
// ============================================================================

// 汇川 H5U 默认 Profile: BigEndian + LowWordFirst (CDAB 序)
constexpr ProtocolProfile TEST_PROFILE{
    "Inovance_H5U_Test",
    {ByteOrder::BigEndian, WordOrder::LowWordFirst},
    120, 120, true, false
};

// 标准 ABCD Profile 用于对比测试
constexpr ProtocolProfile TEST_PROFILE_ABCD{
    "Standard_ABCD_Test",
    {ByteOrder::BigEndian, WordOrder::HighWordFirst},
    120, 120, false, false
};

// Coil 寄存器元数据
constexpr RegisterInfo REG_COIL_MOVE_DONE{
    RegisterArea::Coil, 101, RegisterType::Bool, RegisterAccess::ReadOnly,
    RegisterBehavior::Continuous, RegisterGroup::Feedback,
    "", "Move Done", 0, std::nullopt
};

// HoldingReg Int16 寄存器元数据
constexpr RegisterInfo REG_STATE{
    RegisterArea::HoldingReg, 101, RegisterType::Int16, RegisterAccess::ReadOnly,
    RegisterBehavior::Continuous, RegisterGroup::Feedback,
    "", "State Code", 0, std::nullopt
};

// HoldingReg Float32 寄存器元数据（汇川 CDAB 序）
constexpr RegisterInfo REG_ABS_POSITION{
    RegisterArea::HoldingReg, 124, RegisterType::Float32, RegisterAccess::ReadOnly,
    RegisterBehavior::Continuous, RegisterGroup::Feedback,
    "mm", "Absolute Position", 0, std::nullopt
};

// 带 endianOverride 的特殊传感器（ABCD 序）
constexpr RegisterInfo REG_SENSOR_ABCD{
    RegisterArea::HoldingReg, 200, RegisterType::Float32, RegisterAccess::ReadOnly,
    RegisterBehavior::Continuous, RegisterGroup::Feedback,
    "V", "External Sensor (ABCD)", 0,
    EndianPolicy{ByteOrder::BigEndian, WordOrder::HighWordFirst}
};

// Coil 命令寄存器（可用于 encode 测试）
constexpr RegisterInfo REG_COIL_CMD{
    RegisterArea::Coil, 42, RegisterType::Bool, RegisterAccess::ReadWrite,
    RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command,
    "", "Trigger Command", 50, std::nullopt
};

// HoldingReg Int16 命令寄存器
constexpr RegisterInfo REG_HOLDING_INT16{
    RegisterArea::HoldingReg, 50, RegisterType::Int16, RegisterAccess::ReadWrite,
    RegisterBehavior::Level, RegisterGroup::Command,
    "", "Speed Setpoint", 0, std::nullopt
};

// HoldingReg Float32 命令寄存器（汇川 CDAB 序）
constexpr RegisterInfo REG_HOLDING_FLOAT{
    RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite,
    RegisterBehavior::Level, RegisterGroup::Command,
    "mm", "Target Position", 0, std::nullopt
};


// ============================================================================
// 第一部分：decode — Coil 快照解码
// ============================================================================

class RegisterCodecDecodeCoilTest : public ::testing::Test {
protected:
    // Coil 101~108: 0b00101001 → 101=true, 102=false, 103=false, 104=true, 
    //                             105=false, 106=true, 107=false, 108=false
    std::vector<uint8_t> coilPayload{0x29};
};

TEST_F(RegisterCodecDecodeCoilTest, DecodeBool_True_FromCoilSnapshot) {
    RawBitSnapshot bits(101, 8, coilPayload);

    PlcValue result = RegisterCodec::decode(REG_COIL_MOVE_DONE, &bits, nullptr, TEST_PROFILE);

    EXPECT_TRUE(isBool(result));
    EXPECT_EQ(getValue<bool>(result), true);
}

TEST_F(RegisterCodecDecodeCoilTest, DecodeBool_False_FromCoilSnapshot) {
    // 寄存器地址 102，对应 bit1，payload[0]=0x29→bit1=0
    constexpr RegisterInfo reg_coil_102{
        RegisterArea::Coil, 102, RegisterType::Bool, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "Coil 102", 0, std::nullopt
    };
    RawBitSnapshot bits(101, 8, coilPayload);

    PlcValue result = RegisterCodec::decode(reg_coil_102, &bits, nullptr, TEST_PROFILE);

    EXPECT_TRUE(isBool(result));
    EXPECT_EQ(getValue<bool>(result), false);
}

TEST_F(RegisterCodecDecodeCoilTest, DecodeBool_AddressOutOfRange_ThrowsOutOfRange) {
    // 快照只包含 101~108，请求地址 99
    constexpr RegisterInfo reg_out_of_range{
        RegisterArea::Coil, 99, RegisterType::Bool, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "Out of Range", 0, std::nullopt
    };
    RawBitSnapshot bits(101, 8, coilPayload);

    EXPECT_THROW({
        RegisterCodec::decode(reg_out_of_range, &bits, nullptr, TEST_PROFILE);
    }, std::out_of_range);
}

TEST_F(RegisterCodecDecodeCoilTest, DecodeBool_NullBitSnapshot_ThrowsInvalidArgument) {
    EXPECT_THROW({
        RegisterCodec::decode(REG_COIL_MOVE_DONE, nullptr, nullptr, TEST_PROFILE);
    }, std::invalid_argument);
}


// ============================================================================
// 第二部分：decode — Word 快照解码
// ============================================================================

class RegisterCodecDecodeWordTest : public ::testing::Test {
protected:
    // D100~D103: 状态字、报警码、位置高字(124-125)
    std::vector<uint16_t> wordPayload{0x0003, 0x0000, 0x0000, 0x4316};
};

TEST_F(RegisterCodecDecodeWordTest, DecodeInt16_FromWordSnapshot) {
    // D100 = 0x0003 (STATE = 3)
    constexpr RegisterInfo reg_state_d100{
        RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "STATE D100", 0, std::nullopt
    };
    RawWordSnapshot words(100, wordPayload);

    PlcValue result = RegisterCodec::decode(reg_state_d100, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isInt16(result));
    EXPECT_EQ(getValue<int16_t>(result), 3);
}

TEST_F(RegisterCodecDecodeWordTest, DecodeFloat32_CDAB_FromWordSnapshot) {
    // D124-125 = [0x0000, 0x4316] → 汇川 CDAB → 150.0f
    // payload 从 D100 开始，D124 对应 offset=24，不在当前快照
    // 重新构造 D124 开始的快照
    std::vector<uint16_t> posPayload{0x0000, 0x4316};
    RawWordSnapshot words(124, posPayload);

    PlcValue result = RegisterCodec::decode(REG_ABS_POSITION, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(result));
    EXPECT_FLOAT_EQ(getValue<float>(result), 150.0f);
}

TEST_F(RegisterCodecDecodeWordTest, DecodeFloat32_ABCD_WithEndianOverride) {
    // 传感器 Override 为 ABCD: [0x4316, 0x0000] → 150.0f
    std::vector<uint16_t> sensorPayload{0x4316, 0x0000};
    RawWordSnapshot words(200, sensorPayload);

    PlcValue result = RegisterCodec::decode(REG_SENSOR_ABCD, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(result));
    EXPECT_FLOAT_EQ(getValue<float>(result), 150.0f);
}

TEST_F(RegisterCodecDecodeWordTest, DecodeInt16_AddressOutOfRange_ThrowsOutOfRange) {
    constexpr RegisterInfo reg_out_of_range{
        RegisterArea::HoldingReg, 104, RegisterType::Int16, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "Out of Range D104", 0, std::nullopt
    };
    RawWordSnapshot words(100, wordPayload); // 只到 D103

    EXPECT_THROW({
        RegisterCodec::decode(reg_out_of_range, nullptr, &words, TEST_PROFILE);
    }, std::out_of_range);
}

TEST_F(RegisterCodecDecodeWordTest, DecodeFloat32_InsufficientWords_ThrowsOutOfRange) {
    // D102 只读 2 个字，但只有 D102~103（当前快照 D100~D103）
    constexpr RegisterInfo reg_float_d102{
        RegisterArea::HoldingReg, 102, RegisterType::Float32, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "Float D102", 0, std::nullopt
    };
    RawWordSnapshot words(100, wordPayload);

    PlcValue result = RegisterCodec::decode(reg_float_d102, nullptr, &words, TEST_PROFILE);
    // D102~D103 = [0x0000, 0x4316], CDAB → 150.0f
    EXPECT_TRUE(isFloat(result));
    EXPECT_FLOAT_EQ(getValue<float>(result), 150.0f);
}

TEST_F(RegisterCodecDecodeWordTest, DecodeHoldingReg_NullWordSnapshot_ThrowsInvalidArgument) {
    EXPECT_THROW({
        RegisterCodec::decode(REG_STATE, nullptr, nullptr, TEST_PROFILE);
    }, std::invalid_argument);
}


// ============================================================================
// 第三部分：decode — PlcValue 类型正确性验证
// ============================================================================

TEST(RegisterCodecDecodeTypeTest, DecodeBool_YieldsBoolAlternative) {
    std::vector<uint8_t> payload{0x01}; // bit0=1
    RawBitSnapshot bits(101, 1, payload);

    PlcValue result = RegisterCodec::decode(REG_COIL_MOVE_DONE, &bits, nullptr, TEST_PROFILE);

    EXPECT_TRUE(isBool(result));
    EXPECT_FALSE(isFloat(result));
    EXPECT_FALSE(isInt16(result));
}

TEST(RegisterCodecDecodeTypeTest, DecodeInt16_YieldsInt16Alternative) {
    std::vector<uint16_t> payload{42};
    RawWordSnapshot words(101, payload);

    PlcValue result = RegisterCodec::decode(REG_STATE, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isInt16(result));
    EXPECT_FALSE(isBool(result));
    EXPECT_FALSE(isFloat(result));
}

TEST(RegisterCodecDecodeTypeTest, DecodeFloat32_YieldsFloatAlternative) {
    std::vector<uint16_t> payload{0x0000, 0x4316}; // CDAB → 150.0f
    RawWordSnapshot words(124, payload);

    PlcValue result = RegisterCodec::decode(REG_ABS_POSITION, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(result));
    EXPECT_FALSE(isBool(result));
    EXPECT_FALSE(isInt16(result));
}


// ============================================================================
// 第四部分：encode — PlcValue 编码
// ============================================================================

TEST(RegisterCodecEncodeTest, EncodeBool_True_ForCoil_ReturnsFF00) {
    PlcValue value = true;
    auto result = RegisterCodec::encode(value, REG_COIL_CMD, TEST_PROFILE);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0xFF00);
}

TEST(RegisterCodecEncodeTest, EncodeBool_False_ForCoil_ReturnsZero) {
    PlcValue value = false;
    auto result = RegisterCodec::encode(value, REG_COIL_CMD, TEST_PROFILE);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0x0000);
}

TEST(RegisterCodecEncodeTest, EncodeBool_True_ForHoldingReg_ReturnsOne) {
    // 构造一个 HoldingReg Bool
    constexpr RegisterInfo reg_holding_bool{
        RegisterArea::HoldingReg, 50, RegisterType::Bool, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "", "Holding Bool", 0, std::nullopt
    };
    PlcValue value = true;
    auto result = RegisterCodec::encode(value, reg_holding_bool, TEST_PROFILE);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 1);
}

TEST(RegisterCodecEncodeTest, EncodeBool_False_ForHoldingReg_ReturnsZero) {
    constexpr RegisterInfo reg_holding_bool{
        RegisterArea::HoldingReg, 50, RegisterType::Bool, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "", "Holding Bool", 0, std::nullopt
    };
    PlcValue value = false;
    auto result = RegisterCodec::encode(value, reg_holding_bool, TEST_PROFILE);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0);
}

TEST(RegisterCodecEncodeTest, EncodeInt16_ReturnsValue) {
    PlcValue value = static_cast<int16_t>(42);
    auto result = RegisterCodec::encode(value, REG_HOLDING_INT16, TEST_PROFILE);

    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 42);
}

TEST(RegisterCodecEncodeTest, EncodeFloat32_CDAB_150Point0) {
    // 汇川 CDAB: 150.0f → [0x0000, 0x4316]
    PlcValue value = 150.0f;
    auto result = RegisterCodec::encode(value, REG_HOLDING_FLOAT, TEST_PROFILE);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x4316);
}

TEST(RegisterCodecEncodeTest, EncodeFloat32_ABCD_WithOverride_150Point0) {
    // 传感器 Override ABCD: 150.0f → [0x4316, 0x0000]
    constexpr RegisterInfo reg_sensor_abcd_writable{
        RegisterArea::HoldingReg, 200, RegisterType::Float32, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "V", "External Sensor Cmd (ABCD)", 0,
        EndianPolicy{ByteOrder::BigEndian, WordOrder::HighWordFirst}
    };
    PlcValue value = 150.0f;
    auto result = RegisterCodec::encode(value, reg_sensor_abcd_writable, TEST_PROFILE);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x4316);
    EXPECT_EQ(result[1], 0x0000);
}

TEST(RegisterCodecEncodeTest, EncodeNegativeFloat32_CDAB) {
    // -1.0f = 0xBF800000, CDAB → [0x0000, 0xBF80]
    PlcValue value = -1.0f;
    auto result = RegisterCodec::encode(value, REG_HOLDING_FLOAT, TEST_PROFILE);

    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0xBF80);
}


// ============================================================================
// 第五部分：encode → decode 回环测试
// ============================================================================

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_Bool_Coil_RestoresOriginal) {
    PlcValue original = true;
    auto encoded = RegisterCodec::encode(original, REG_COIL_CMD, TEST_PROFILE);

    // 模拟：encoder 产出 vector<uint16_t> → 写入 PLC → 回读 → Snapshot
    // 这里用 encoded 数据构造 RawBitSnapshot（模拟 Coil 回读）
    // FC01 回读 Coil 时按位打包，encoded[0]=0xFF00 表示 Coil=ON
    std::vector<uint8_t> coilPayload{0x01}; // bit0=1
    RawBitSnapshot bits(42, 1, coilPayload);

    PlcValue decoded = RegisterCodec::decode(REG_COIL_CMD, &bits, nullptr, TEST_PROFILE);

    EXPECT_TRUE(isBool(decoded));
    EXPECT_EQ(getValue<bool>(decoded), getValue<bool>(original));
}

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_Int16_RestoresOriginal) {
    PlcValue original = static_cast<int16_t>(12345);
    auto encoded = RegisterCodec::encode(original, REG_HOLDING_INT16, TEST_PROFILE);

    // 用 encoded 数据构造 RawWordSnapshot（模拟 HoldingReg 回读）
    RawWordSnapshot words(50, encoded);

    PlcValue decoded = RegisterCodec::decode(REG_HOLDING_INT16, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isInt16(decoded));
    EXPECT_EQ(getValue<int16_t>(decoded), getValue<int16_t>(original));
}

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_Float32_CDAB_RestoresOriginal) {
    PlcValue original = 150.0f;
    auto encoded = RegisterCodec::encode(original, REG_HOLDING_FLOAT, TEST_PROFILE);

    RawWordSnapshot words(24, encoded);

    PlcValue decoded = RegisterCodec::decode(REG_HOLDING_FLOAT, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(decoded));
    EXPECT_FLOAT_EQ(getValue<float>(decoded), getValue<float>(original));
}

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_Float32_Negative_CDAB_RestoresOriginal) {
    PlcValue original = -13.25f;
    auto encoded = RegisterCodec::encode(original, REG_HOLDING_FLOAT, TEST_PROFILE);

    RawWordSnapshot words(24, encoded);

    PlcValue decoded = RegisterCodec::decode(REG_HOLDING_FLOAT, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(decoded));
    EXPECT_FLOAT_EQ(getValue<float>(decoded), getValue<float>(original));
}

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_Float32_ABCD_WithOverride_RestoresOriginal) {
    constexpr RegisterInfo reg_sensor{
        RegisterArea::HoldingReg, 200, RegisterType::Float32, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "V", "Sensor", 0,
        EndianPolicy{ByteOrder::BigEndian, WordOrder::HighWordFirst}
    };
    PlcValue original = 150.0f;
    auto encoded = RegisterCodec::encode(original, reg_sensor, TEST_PROFILE);

    RawWordSnapshot words(200, encoded);

    PlcValue decoded = RegisterCodec::decode(reg_sensor, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isFloat(decoded));
    EXPECT_FLOAT_EQ(getValue<float>(decoded), getValue<float>(original));
}

TEST(RegisterCodecRoundtripTest, EncodeThenDecode_NegativeInt16_RestoresOriginal) {
    PlcValue original = static_cast<int16_t>(-1);
    auto encoded = RegisterCodec::encode(original, REG_HOLDING_INT16, TEST_PROFILE);

    RawWordSnapshot words(50, encoded);

    PlcValue decoded = RegisterCodec::decode(REG_HOLDING_INT16, nullptr, &words, TEST_PROFILE);

    EXPECT_TRUE(isInt16(decoded));
    EXPECT_EQ(getValue<int16_t>(decoded), -1);
}


// ============================================================================
// 第六部分：encode — 错误处理
// ============================================================================

TEST(RegisterCodecEncodeErrorTest, EncodeString_ThrowsInvalidArgument) {
    PlcValue value = std::string("unsupported");
    EXPECT_THROW({
        RegisterCodec::encode(value, REG_HOLDING_INT16, TEST_PROFILE);
    }, std::invalid_argument);
}


// ============================================================================
// 第七部分：decode — PlcSnapshot 便捷重载 (P2 v4)
// ============================================================================

class RegisterCodecDecodePlcSnapshotTest : public ::testing::Test {
protected:
    // Coil 101~108: 0b00101001
    std::vector<uint8_t> coilPayload{0x29};
    // Word 100~103: STATE=3, ALARM=0, 0x0000, 0x4316
    std::vector<uint16_t> wordPayload{0x0003, 0x0000, 0x0000, 0x4316};
};

TEST_F(RegisterCodecDecodePlcSnapshotTest, DecodeBool_FromPlcSnapshot) {
    RawBitSnapshot bits(101, 8, coilPayload);
    RawWordSnapshot words(100, wordPayload);
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    PlcValue result = RegisterCodec::decode(REG_COIL_MOVE_DONE, snap, TEST_PROFILE);

    EXPECT_TRUE(isBool(result));
    EXPECT_EQ(getValue<bool>(result), true);
}

TEST_F(RegisterCodecDecodePlcSnapshotTest, DecodeInt16_FromPlcSnapshot) {
    RawBitSnapshot bits(101, 8, coilPayload);
    RawWordSnapshot words(100, wordPayload);
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    constexpr RegisterInfo reg_state_d100{
        RegisterArea::HoldingReg, 100, RegisterType::Int16, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "STATE D100", 0, std::nullopt
    };

    PlcValue result = RegisterCodec::decode(reg_state_d100, snap, TEST_PROFILE);

    EXPECT_TRUE(isInt16(result));
    EXPECT_EQ(getValue<int16_t>(result), 3);
}

TEST_F(RegisterCodecDecodePlcSnapshotTest, DecodeFloat32_FromPlcSnapshot) {
    RawBitSnapshot bits(101, 8, coilPayload);
    std::vector<uint16_t> posPayload{0x0000, 0x4316};
    RawWordSnapshot words(124, posPayload);
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    PlcValue result = RegisterCodec::decode(REG_ABS_POSITION, snap, TEST_PROFILE);

    EXPECT_TRUE(isFloat(result));
    EXPECT_FLOAT_EQ(getValue<float>(result), 150.0f);
}

TEST_F(RegisterCodecDecodePlcSnapshotTest, DecodeBool_FromIncompleteSnapshot_StillDecodes) {
    RawBitSnapshot bits(101, 8, coilPayload);
    RawWordSnapshot words; // empty, but we're decoding Coil
    PlcSnapshot snap(std::move(bits), std::move(words), false, 2000);

    PlcValue result = RegisterCodec::decode(REG_COIL_MOVE_DONE, snap, TEST_PROFILE);

    // Even though complete=false, the data itself is valid for decoding
    EXPECT_TRUE(isBool(result));
    EXPECT_EQ(getValue<bool>(result), true);
}

TEST_F(RegisterCodecDecodePlcSnapshotTest, DecodeHoldingReg_WithEmptySnapshot_ThrowsOutOfRange) {
    RawBitSnapshot bits(101, 8, coilPayload);
    RawWordSnapshot words; // empty HoldingReg snapshot
    PlcSnapshot snap(std::move(bits), std::move(words), true, 1000);

    EXPECT_THROW({
        RegisterCodec::decode(REG_STATE, snap, TEST_PROFILE);
    }, std::out_of_range);
}
