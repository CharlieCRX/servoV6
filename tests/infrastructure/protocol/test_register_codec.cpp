#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/RegisterCodec.h"
#include "infrastructure/plc/protocol/ProtocolProfile.h"

using namespace plc::protocol;

// 预定义 4 种常见的协议组合方式，方便测试调用与语义理解
constexpr EndianPolicy POLICY_ABCD = { ByteOrder::BigEndian,    WordOrder::HighWordFirst };
constexpr EndianPolicy POLICY_CDAB = { ByteOrder::BigEndian,    WordOrder::LowWordFirst };
constexpr EndianPolicy POLICY_DCBA = { ByteOrder::LittleEndian, WordOrder::LowWordFirst };
constexpr EndianPolicy POLICY_BADC = { ByteOrder::LittleEndian, WordOrder::HighWordFirst };


// ============================================================================
// 第 1 步：单寄存器类型 (bool 和 uint16_t)
// ============================================================================
TEST(RegisterCodecTest, EncodeBool_True_ReturnsOne) {
    auto result = RegisterCodec::encodeBool(true);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 1);
}

TEST(RegisterCodecTest, EncodeBool_False_ReturnsZero) {
    auto result = RegisterCodec::encodeBool(false);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0);
}

TEST(RegisterCodecTest, DecodeBool_ReturnsCorrectValue) {
    EXPECT_TRUE(RegisterCodec::decodeBool({1}));
    EXPECT_FALSE(RegisterCodec::decodeBool({0}));
}

TEST(RegisterCodecTest, EncodeUint16_ReturnsDirectly) {
    auto result = RegisterCodec::encodeUint16(0x1234);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0x1234);
}

TEST(RegisterCodecTest, DecodeUint16_ReturnsCorrectValue) {
    EXPECT_EQ(RegisterCodec::decodeUint16({0x5678}), 0x5678);
}


// ============================================================================
// 第 2 步：双寄存器整型 (int32_t) 与基础字节序
// 0x12345678 拆分：A=0x12, B=0x34, C=0x56, D=0x78
// ============================================================================

// --- 测试 Int32 编码 ---
TEST(RegisterCodecTest, EncodeInt32_ABCD) {
    auto result = RegisterCodec::encodeInt32(0x12345678, POLICY_ABCD);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x1234);
    EXPECT_EQ(result[1], 0x5678);
}

TEST(RegisterCodecTest, EncodeInt32_CDAB) {
    auto result = RegisterCodec::encodeInt32(0x12345678, POLICY_CDAB);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x5678);
    EXPECT_EQ(result[1], 0x1234);
}

TEST(RegisterCodecTest, EncodeInt32_DCBA) {
    auto result = RegisterCodec::encodeInt32(0x12345678, POLICY_DCBA);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x7856);
    EXPECT_EQ(result[1], 0x3412);
}

TEST(RegisterCodecTest, EncodeInt32_BADC) {
    auto result = RegisterCodec::encodeInt32(0x12345678, POLICY_BADC);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x3412);
    EXPECT_EQ(result[1], 0x7856);
}


// --- 测试 Int32 解码 ---
TEST(RegisterCodecTest, DecodeInt32_ABCD) {
    int32_t result = RegisterCodec::decodeInt32({0x1234, 0x5678}, POLICY_ABCD);
    EXPECT_EQ(result, 0x12345678);
}

TEST(RegisterCodecTest, DecodeInt32_CDAB) {
    int32_t result = RegisterCodec::decodeInt32({0x5678, 0x1234}, POLICY_CDAB);
    EXPECT_EQ(result, 0x12345678);
}

TEST(RegisterCodecTest, DecodeInt32_DCBA) {
    int32_t result = RegisterCodec::decodeInt32({0x7856, 0x3412}, POLICY_DCBA);
    EXPECT_EQ(result, 0x12345678);
}

TEST(RegisterCodecTest, DecodeInt32_BADC) {
    int32_t result = RegisterCodec::decodeInt32({0x3412, 0x7856}, POLICY_BADC);
    EXPECT_EQ(result, 0x12345678);
}


// --- 测试 Int32 编解码回环 ---
TEST(RegisterCodecTest, EncodeThenDecodeInt32_ABCD_ShouldRestoreOriginal) {
    int32_t original = 0x12345678;
    auto encoded = RegisterCodec::encodeInt32(original, POLICY_ABCD);
    int32_t decoded = RegisterCodec::decodeInt32(encoded, POLICY_ABCD);
    EXPECT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeInt32_CDAB_ShouldRestoreOriginal) {
    int32_t original = 0x12345678;
    auto encoded = RegisterCodec::encodeInt32(original, POLICY_CDAB);
    int32_t decoded = RegisterCodec::decodeInt32(encoded, POLICY_CDAB);
    EXPECT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeInt32_DCBA_ShouldRestoreOriginal) {
    int32_t original = 0x12345678;
    auto encoded = RegisterCodec::encodeInt32(original, POLICY_DCBA);
    int32_t decoded = RegisterCodec::decodeInt32(encoded, POLICY_DCBA);
    EXPECT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeInt32_BADC_ShouldRestoreOriginal) {
    int32_t original = 0x12345678;
    auto encoded = RegisterCodec::encodeInt32(original, POLICY_BADC);
    int32_t decoded = RegisterCodec::decodeInt32(encoded, POLICY_BADC);
    EXPECT_EQ(decoded, original);
}


// ============================================================================
// 第 3 步：浮点数 (float) 内存拷贝转换
// 1.0f 的 IEEE 754 为 0x3F800000 
// 13.25f 的 IEEE 754 为 0x41540000
// -1.0f 的 IEEE 754 为 0xBF800000
// ============================================================================

// --- 测试 Float 编码 ---
TEST(RegisterCodecTest, EncodeFloat_ABCD_One) {
    auto result = RegisterCodec::encodeFloat(1.0f, POLICY_ABCD);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x3F80);
    EXPECT_EQ(result[1], 0x0000);
}

TEST(RegisterCodecTest, EncodeFloat_ABCD_Arbitrary) {
    auto result = RegisterCodec::encodeFloat(13.25f, POLICY_ABCD);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x4154);
    EXPECT_EQ(result[1], 0x0000);
}

TEST(RegisterCodecTest, EncodeFloat_CDAB_One) {
    auto result = RegisterCodec::encodeFloat(1.0f, POLICY_CDAB);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x3F80);
}

TEST(RegisterCodecTest, EncodeFloat_DCBA_One) {
    auto result = RegisterCodec::encodeFloat(1.0f, POLICY_DCBA);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x803F);
}

TEST(RegisterCodecTest, EncodeFloat_BADC_One) {
    auto result = RegisterCodec::encodeFloat(1.0f, POLICY_BADC);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x803F);
    EXPECT_EQ(result[1], 0x0000);
}


// --- 测试 Float 解码 ---
TEST(RegisterCodecTest, DecodeFloat_ABCD) {
    float result = RegisterCodec::decodeFloat({0x3F80, 0x0000}, POLICY_ABCD);
    EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(RegisterCodecTest, DecodeFloat_CDAB) {
    float result = RegisterCodec::decodeFloat({0x0000, 0x3F80}, POLICY_CDAB);
    EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(RegisterCodecTest, DecodeFloat_DCBA) {
    float result = RegisterCodec::decodeFloat({0x0000, 0x803F}, POLICY_DCBA);
    EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(RegisterCodecTest, DecodeFloat_BADC) {
    float result = RegisterCodec::decodeFloat({0x803F, 0x0000}, POLICY_BADC);
    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 编解码回环 ---
TEST(RegisterCodecTest, EncodeThenDecodeFloat_ABCD_ShouldRestoreOriginal) {
    float original = 13.25f;
    auto encoded = RegisterCodec::encodeFloat(original, POLICY_ABCD);
    float decoded = RegisterCodec::decodeFloat(encoded, POLICY_ABCD);
    EXPECT_FLOAT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeFloat_CDAB_ShouldRestoreOriginal) {
    float original = 13.25f;
    auto encoded = RegisterCodec::encodeFloat(original, POLICY_CDAB);
    float decoded = RegisterCodec::decodeFloat(encoded, POLICY_CDAB);
    EXPECT_FLOAT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeFloat_DCBA_ShouldRestoreOriginal) {
    float original = 13.25f;
    auto encoded = RegisterCodec::encodeFloat(original, POLICY_DCBA);
    float decoded = RegisterCodec::decodeFloat(encoded, POLICY_DCBA);
    EXPECT_FLOAT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeFloat_BADC_ShouldRestoreOriginal) {
    float original = 13.25f;
    auto encoded = RegisterCodec::encodeFloat(original, POLICY_BADC);
    float decoded = RegisterCodec::decodeFloat(encoded, POLICY_BADC);
    EXPECT_FLOAT_EQ(decoded, original);
}


// ============================================================================
// 第 4 步：负数浮点数测试 (验证高位符号位处理)
// ============================================================================

TEST(RegisterCodecTest, EncodeFloatNegative_ABCD) {
    auto result = RegisterCodec::encodeFloat(-1.0f, POLICY_ABCD);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0xBF80);
    EXPECT_EQ(result[1], 0x0000);
}

TEST(RegisterCodecTest, EncodeFloatNegative_CDAB) {
    auto result = RegisterCodec::encodeFloat(-1.0f, POLICY_CDAB);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0xBF80);
}

TEST(RegisterCodecTest, EncodeFloatNegative_DCBA) {
    auto result = RegisterCodec::encodeFloat(-1.0f, POLICY_DCBA);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x80BF);
}

TEST(RegisterCodecTest, EncodeFloatNegative_BADC) {
    auto result = RegisterCodec::encodeFloat(-1.0f, POLICY_BADC);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x80BF);
    EXPECT_EQ(result[1], 0x0000);
}


TEST(RegisterCodecTest, DecodeFloatNegative_ABCD) {
    float result = RegisterCodec::decodeFloat({0xBF80, 0x0000}, POLICY_ABCD);
    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(RegisterCodecTest, DecodeFloatNegative_CDAB) {
    float result = RegisterCodec::decodeFloat({0x0000, 0xBF80}, POLICY_CDAB);
    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(RegisterCodecTest, DecodeFloatNegative_DCBA) {
    float result = RegisterCodec::decodeFloat({0x0000, 0x80BF}, POLICY_DCBA);
    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(RegisterCodecTest, DecodeFloatNegative_BADC) {
    float result = RegisterCodec::decodeFloat({0x80BF, 0x0000}, POLICY_BADC);
    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(RegisterCodecTest, EncodeThenDecodeNegativeFloat_ShouldRestoreOriginal) {
    float original = -13.25f;
    auto encoded = RegisterCodec::encodeFloat(original, POLICY_ABCD);
    float decoded = RegisterCodec::decodeFloat(encoded, POLICY_ABCD);
    EXPECT_FLOAT_EQ(decoded, original);
}



// ============================================================================
// 准备测试数据：Profile 与 Metadata
// ============================================================================

// 1. 定义汇川 (INOVANCE) PLC Profile: CDAB 序 (BigEndian + LowWordFirst)
constexpr ProtocolProfile TEST_INOVANCE_PROFILE {
    "Inovance_H5U&Easy_Test",
    { ByteOrder::BigEndian, WordOrder::LowWordFirst }, // 核心：CDAB 序
    120, 120, true, true
};

// 2. 普通寄存器：Y轴目标位置 (不指定 endianOverride，预期自动继承汇川的 CDAB)
constexpr RegisterInfo REG_Y_POS = { 
    RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Command, "mm", "Y轴绝对定位目标距离", 0, 
    std::nullopt // 没有 Override
};

// 3. 特殊寄存器：假设总线上挂了一个标准 Modbus 传感器，必须使用严格的 ABCD 序
constexpr RegisterInfo REG_STANDARD_SENSOR = { 
    RegisterArea::HoldingReg, 200, RegisterType::Float32, RegisterAccess::ReadWrite, 
    RegisterBehavior::Level, RegisterGroup::Parameter, "", "标准 Modbus 传感器", 0, 
    EndianPolicy{ ByteOrder::BigEndian, WordOrder::HighWordFirst } // 显式 Override 为 ABCD
};


// ============================================================================
// 第 1 步：测试策略决议 (Policy Resolution)
// ============================================================================

TEST(RegisterCodecTest, ResolvePolicy_ShouldInheritInovanceCDAB) {
    auto policy = RegisterCodec::resolvePolicy(REG_Y_POS, TEST_INOVANCE_PROFILE);
    EXPECT_EQ(policy.byteOrder, ByteOrder::BigEndian);
    EXPECT_EQ(policy.wordOrder, WordOrder::LowWordFirst);
}

TEST(RegisterCodecTest, ResolvePolicy_ShouldUseSensorOverrideABCD) {
    auto policy = RegisterCodec::resolvePolicy(REG_STANDARD_SENSOR, TEST_INOVANCE_PROFILE);
    EXPECT_EQ(policy.byteOrder, ByteOrder::BigEndian);
    EXPECT_EQ(policy.wordOrder, WordOrder::HighWordFirst);
}


// ============================================================================
// 第 2 步：测试 32位整数 (Int32) 编码与解码
// 目标值：0x12345678 (A=12, B=34, C=56, D=78)
// ============================================================================

TEST(RegisterCodecTest, EncodeInt32_InovanceCDAB) {
    // 预期 CDAB: CD=0x5678, AB=0x1234
    auto encoded = RegisterCodec::encode(0x12345678, REG_Y_POS, TEST_INOVANCE_PROFILE);
    ASSERT_EQ(encoded.size(), 2);
    EXPECT_EQ(encoded[0], 0x5678);
    EXPECT_EQ(encoded[1], 0x1234);
}

TEST(RegisterCodecTest, DecodeInt32_InovanceCDAB) {
    int32_t decoded = RegisterCodec::decodeInt32({0x5678, 0x1234}, REG_Y_POS, TEST_INOVANCE_PROFILE);
    EXPECT_EQ(decoded, 0x12345678);
}


// ============================================================================
// 第 3 步：测试浮点数 (Float32) 编码与解码
// 目标值：13.25f 对应的 IEEE 754 内存为 0x41540000 
// (A=41, B=54, C=00, D=00)
// ============================================================================

TEST(RegisterCodecTest, EncodeFloat32_InovanceCDAB_ShouldPutLowWordFirst) {
    // 预期 CDAB: CD=0x0000, AB=0x4154
    auto encoded = RegisterCodec::encode(13.25f, REG_Y_POS, TEST_INOVANCE_PROFILE);
    ASSERT_EQ(encoded.size(), 2);
    EXPECT_EQ(encoded[0], 0x0000); // C=00, D=00
    EXPECT_EQ(encoded[1], 0x4154); // A=41, B=54
}

TEST(RegisterCodecTest, DecodeFloat32_InovanceCDAB) {
    float decoded = RegisterCodec::decodeFloat({0x0000, 0x4154}, REG_Y_POS, TEST_INOVANCE_PROFILE);
    EXPECT_FLOAT_EQ(decoded, 13.25f);
}

TEST(RegisterCodecTest, EncodeFloat32_SensorABCD_ShouldPutHighWordFirst) {
    // 对于特殊传感器，使用了 Override，预期 ABCD: AB=0x4154, CD=0x0000
    auto encoded = RegisterCodec::encode(13.25f, REG_STANDARD_SENSOR, TEST_INOVANCE_PROFILE);
    ASSERT_EQ(encoded.size(), 2);
    EXPECT_EQ(encoded[0], 0x4154); // A=41, B=54
    EXPECT_EQ(encoded[1], 0x0000); // C=00, D=00
}


// ============================================================================
// 第 4 步：负浮点数回环测试 (验证符号位处理)
// 目标值：-13.25f (0xC1540000)
// ============================================================================

TEST(RegisterCodecTest, EncodeThenDecodeNegativeFloat_InovanceCDAB_ShouldRestore) {
    float original = -13.25f;
    auto encoded = RegisterCodec::encode(original, REG_Y_POS, TEST_INOVANCE_PROFILE);
    
    // 断言中间状态: 预期 CDAB (0x0000, 0xC154)
    ASSERT_EQ(encoded[0], 0x0000);
    ASSERT_EQ(encoded[1], 0xC154);

    float decoded = RegisterCodec::decodeFloat(encoded, REG_Y_POS, TEST_INOVANCE_PROFILE);
    EXPECT_FLOAT_EQ(decoded, original);
}