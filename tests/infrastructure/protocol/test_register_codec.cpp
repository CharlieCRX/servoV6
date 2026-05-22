#include <gtest/gtest.h>
#include "infrastructure/plc/protocol/RegisterCodec.h"

// 第 1 步：单寄存器类型 (bool 和 uint16_t)
// --- 测试 Bool 编码/解码 ---
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

// --- 测试 Uint16 编码/解码 ---
TEST(RegisterCodecTest, EncodeUint16_ReturnsDirectly) {
    auto result = RegisterCodec::encodeUint16(0x1234);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0], 0x1234);
}

TEST(RegisterCodecTest, DecodeUint16_ReturnsCorrectValue) {
    EXPECT_EQ(RegisterCodec::decodeUint16({0x5678}), 0x5678);
}


// 第 2 步：双寄存器整型 (int32_t) 与基础字节序

// 我们把：0x12345678 拆成 4 个字节：
// - A字节: 0x12
// - B字节: 0x34
// - C字节: 0x56
// - D字节: 0x78
// Modbus 的 32 位数据 => 2 个寄存器 = 4 个字节

// --- 测试 Int32 编码 (BigEndian ABCD) ---
TEST(RegisterCodecTest, EncodeInt32_BigEndian_ABCD) {
    // 0x12345678 -> 寄存器0: 0x1234 (AB), 寄存器1: 0x5678 (CD)
    auto result = RegisterCodec::encodeInt32(0x12345678, Endianness::BigEndian);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x1234);
    EXPECT_EQ(result[1], 0x5678);
}

// --- 测试 Int32 编码 (BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, EncodeInt32_BigEndianSwap_CDAB) {
    // 0x12345678 -> 寄存器0: 0x5678 (CD), 寄存器1: 0x1234 (AB)
    auto result = RegisterCodec::encodeInt32(0x12345678, Endianness::BigEndianSwap);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x5678);
    EXPECT_EQ(result[1], 0x1234);
}

// --- 测试 Int32 编码 (LittleEndian DCBA) ---
TEST(RegisterCodecTest, EncodeInt32_LittleEndian_DCBA) {
    // 0x12345678 -> 寄存器0: 0x7856 (DC), 寄存器1: 0x3412 (BA) -> 0x78563412
    auto result = RegisterCodec::encodeInt32(0x12345678, Endianness::LittleEndian);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x7856);
    EXPECT_EQ(result[1], 0x3412);
}

// --- 测试 Int32 编码 (LittleEndianSwap BADC)
TEST(RegisterCodecTest, EncodeInt32_LittleEndianSwap_BADC) {
    // 0x12345678 -> 寄存器0: 0x3412 (BA), 寄存器1: 0x7856 (DC)
    auto result = RegisterCodec::encodeInt32(0x12345678, Endianness::LittleEndianSwap);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x3412);
    EXPECT_EQ(result[1], 0x7856);
}

// --- 测试 Int32 解码 (BigEndian ABCD) ---
TEST(RegisterCodecTest, DecodeInt32_BigEndian_ABCD) {
    int32_t result = RegisterCodec::decodeInt32({0x1234, 0x5678}, Endianness::BigEndian);
    EXPECT_EQ(result, 0x12345678);
}

// --- 测试 Int32 解码 (BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, DecodeInt32_BigEndianSwap_CDAB) {
    int32_t result = RegisterCodec::decodeInt32({0x5678, 0x1234}, Endianness::BigEndianSwap);
    EXPECT_EQ(result, 0x12345678);
}


// --- 测试 Int32 解码 (LittleEndian DCBA) ---
TEST(RegisterCodecTest, DecodeInt32_LittleEndian_DCBA) {
    // 寄存器0: 0x7856 (DC)
    // 寄存器1: 0x3412 (BA)
    // => DCBA -> 0x12345678

    int32_t result = RegisterCodec::decodeInt32({0x7856, 0x3412}, Endianness::LittleEndian);
    EXPECT_EQ(result, 0x12345678);
}

// --- 测试 Int32 解码 (LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, DecodeInt32_LittleEndianSwap_BADC) {
    // 寄存器0: 0x3412 (BA)
    // 寄存器1: 0x7856 (DC)
    // => BADC -> 0x12345678

    int32_t result = RegisterCodec::decodeInt32({0x3412, 0x7856}, Endianness::LittleEndianSwap);
    EXPECT_EQ(result, 0x12345678);
}


TEST(RegisterCodecTest, EncodeThenDecodeInt32_BigEndian_ShouldRestoreOriginal)
{
    int32_t original = 0x12345678;

    auto encoded =
        RegisterCodec::encodeInt32(
            original,
            Endianness::BigEndian);

    int32_t decoded =
        RegisterCodec::decodeInt32(
            encoded,
            Endianness::BigEndian);

    EXPECT_EQ(decoded, original);
}


TEST(RegisterCodecTest, EncodeThenDecodeInt32_BigEndianSwap_ShouldRestoreOriginal)
{
    int32_t original = 0x12345678;

    auto encoded =
        RegisterCodec::encodeInt32(
            original,
            Endianness::BigEndianSwap);

    int32_t decoded =
        RegisterCodec::decodeInt32(
            encoded,
            Endianness::BigEndianSwap);

    EXPECT_EQ(decoded, original);
}


TEST(RegisterCodecTest, EncodeThenDecodeInt32_LittleEndian_ShouldRestoreOriginal)
{
    int32_t original = 0x12345678;

    auto encoded =
        RegisterCodec::encodeInt32(
            original,
            Endianness::LittleEndian);

    int32_t decoded =
        RegisterCodec::decodeInt32(
            encoded,
            Endianness::LittleEndian);

    EXPECT_EQ(decoded, original);
}

TEST(RegisterCodecTest, EncodeThenDecodeInt32_LittleEndianSwap_ShouldRestoreOriginal)
{
    int32_t original = 0x12345678;

    auto encoded =
        RegisterCodec::encodeInt32(
            original,
            Endianness::LittleEndianSwap);

    int32_t decoded =
        RegisterCodec::decodeInt32(
            encoded,
            Endianness::LittleEndianSwap);

    EXPECT_EQ(decoded, original);
}

// 第 3 步：浮点数 (float) 内存拷贝转换
// 测试必须使用：IEEE754 已知稳定值做底层协议黄金数据。
// --- 测试 Float 编码 (IEEE 754) ---
TEST(RegisterCodecTest, EncodeFloat_BigEndian_ABCD) {
    // 浮点数 1.0f 的 IEEE 754 十六进制表示为 0x3F800000
    // AB = 0x3F80, CD = 0x0000
    auto result = RegisterCodec::encodeFloat(1.0f, Endianness::BigEndian);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x3F80);
    EXPECT_EQ(result[1], 0x0000);
}

// --- 测试 Float 编码 (IEEE 754) ---
TEST(RegisterCodecTest, EncodeFloat_BigEndian_ABCD2) {
    // 浮点数 13.25f 的 IEEE 754 十六进制表示为 0x41540000
    // AB = 0x4154, CD = 0x0000
    auto result = RegisterCodec::encodeFloat(13.25f, Endianness::BigEndian);
    ASSERT_EQ(result.size(), 2);
    EXPECT_EQ(result[0], 0x4154);
    EXPECT_EQ(result[1], 0x0000);
}


TEST(RegisterCodecTest, DecodeFloat_BigEndian_One)
{
    float result = RegisterCodec::decodeFloat(
            {0x3F80, 0x0000},
            Endianness::BigEndian);

    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 编码 (BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, EncodeFloat_BigEndianSwap_CDAB) {
    // 浮点数 1.0f 的 IEEE 754 十六进制表示为 0x3F800000
    // 原始字节序: [3F][80][00][00]
    // CDAB -> [00][00][3F][80]
    // 寄存器0 = 0x0000
    // 寄存器1 = 0x3F80

    auto result =
        RegisterCodec::encodeFloat(
            1.0f,
            Endianness::BigEndianSwap);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x3F80);
}


// --- 测试 Float 编码 (LittleEndian DCBA) ---
TEST(RegisterCodecTest, EncodeFloat_LittleEndian_DCBA) {
    // 浮点数 1.0f 的 IEEE 754 十六进制表示为 0x3F800000
    // 原始字节序: [3F][80][00][00]
    // DCBA -> [00][00][80][3F]
    // 寄存器0 = 0x0000
    // 寄存器1 = 0x803F

    auto result =
        RegisterCodec::encodeFloat(
            1.0f,
            Endianness::LittleEndian);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x803F);
}


// --- 测试 Float 编码 (LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, EncodeFloat_LittleEndianSwap_BADC) {
    // 浮点数 1.0f 的 IEEE 754 十六进制表示为 0x3F800000
    // 原始字节序: [3F][80][00][00]
    // BADC -> [80][3F][00][00]
    // 寄存器0 = 0x803F
    // 寄存器1 = 0x0000

    auto result =
        RegisterCodec::encodeFloat(
            1.0f,
            Endianness::LittleEndianSwap);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x803F);
    EXPECT_EQ(result[1], 0x0000);
}


// --- 测试 Float 解码 (BigEndian ABCD) ---
TEST(RegisterCodecTest, DecodeFloat_BigEndian_ABCD) {
    // 0x3F800000 -> 1.0f
    // AB = 0x3F80
    // CD = 0x0000

    float result =
        RegisterCodec::decodeFloat(
            {0x3F80, 0x0000},
            Endianness::BigEndian);

    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 解码 (BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, DecodeFloat_BigEndianSwap_CDAB) {
    // CDAB -> [00][00][3F][80]
    // 寄存器0 = 0x0000
    // 寄存器1 = 0x3F80
    // => 1.0f

    float result =
        RegisterCodec::decodeFloat(
            {0x0000, 0x3F80},
            Endianness::BigEndianSwap);

    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 解码 (LittleEndian DCBA) ---
TEST(RegisterCodecTest, DecodeFloat_LittleEndian_DCBA) {
    // DCBA -> [00][00][80][3F]
    // 寄存器0 = 0x0000
    // 寄存器1 = 0x803F
    // => 1.0f

    float result =
        RegisterCodec::decodeFloat(
            {0x0000, 0x803F},
            Endianness::LittleEndian);

    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 解码 (LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, DecodeFloat_LittleEndianSwap_BADC) {
    // BADC -> [80][3F][00][00]
    // 寄存器0 = 0x803F
    // 寄存器1 = 0x0000
    // => 1.0f

    float result =
        RegisterCodec::decodeFloat(
            {0x803F, 0x0000},
            Endianness::LittleEndianSwap);

    EXPECT_FLOAT_EQ(result, 1.0f);
}


// --- 测试 Float 编解码回环 (BigEndian ABCD) ---
TEST(RegisterCodecTest, EncodeThenDecodeFloat_BigEndian_ShouldRestoreOriginal)
{
    float original = 13.25f;

    auto encoded =
        RegisterCodec::encodeFloat(
            original,
            Endianness::BigEndian);

    float decoded =
        RegisterCodec::decodeFloat(
            encoded,
            Endianness::BigEndian);

    EXPECT_FLOAT_EQ(decoded, original);
}


// --- 测试 Float 编解码回环 (BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, EncodeThenDecodeFloat_BigEndianSwap_ShouldRestoreOriginal)
{
    float original = 13.25f;

    auto encoded =
        RegisterCodec::encodeFloat(
            original,
            Endianness::BigEndianSwap);

    float decoded =
        RegisterCodec::decodeFloat(
            encoded,
            Endianness::BigEndianSwap);

    EXPECT_FLOAT_EQ(decoded, original);
}


// --- 测试 Float 编解码回环 (LittleEndian DCBA) ---
TEST(RegisterCodecTest, EncodeThenDecodeFloat_LittleEndian_ShouldRestoreOriginal)
{
    float original = 13.25f;

    auto encoded =
        RegisterCodec::encodeFloat(
            original,
            Endianness::LittleEndian);

    float decoded =
        RegisterCodec::decodeFloat(
            encoded,
            Endianness::LittleEndian);

    EXPECT_FLOAT_EQ(decoded, original);
}


// --- 测试 Float 编解码回环 (LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, EncodeThenDecodeFloat_LittleEndianSwap_ShouldRestoreOriginal)
{
    float original = 13.25f;

    auto encoded =
        RegisterCodec::encodeFloat(
            original,
            Endianness::LittleEndianSwap);

    float decoded =
        RegisterCodec::decodeFloat(
            encoded,
            Endianness::LittleEndianSwap);

    EXPECT_FLOAT_EQ(decoded, original);
}


// --- 测试 Float 编码 (-1.0f BigEndian ABCD) ---
TEST(RegisterCodecTest, EncodeFloatNegative_BigEndian_ABCD) {
    // -1.0f -> IEEE754 = 0xBF800000
    // AB = 0xBF80
    // CD = 0x0000

    auto result =
        RegisterCodec::encodeFloat(
            -1.0f,
            Endianness::BigEndian);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0xBF80);
    EXPECT_EQ(result[1], 0x0000);
}

// --- 测试 Float 编码 (-1.0f BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, EncodeFloatNegative_BigEndianSwap_CDAB) {
    // -1.0f -> 0xBF800000
    // CDAB -> [00][00][BF][80]

    auto result =
        RegisterCodec::encodeFloat(
            -1.0f,
            Endianness::BigEndianSwap);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0xBF80);
}


// --- 测试 Float 编码 (-1.0f LittleEndian DCBA) ---
TEST(RegisterCodecTest, EncodeFloatNegative_LittleEndian_DCBA) {
    // -1.0f -> 0xBF800000
    // DCBA -> [00][00][80][BF]

    auto result =
        RegisterCodec::encodeFloat(
            -1.0f,
            Endianness::LittleEndian);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x0000);
    EXPECT_EQ(result[1], 0x80BF);
}

// --- 测试 Float 编码 (-1.0f LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, EncodeFloatNegative_LittleEndianSwap_BADC) {
    // -1.0f -> 0xBF800000
    // BADC -> [80][BF][00][00]

    auto result =
        RegisterCodec::encodeFloat(
            -1.0f,
            Endianness::LittleEndianSwap);

    ASSERT_EQ(result.size(), 2);

    EXPECT_EQ(result[0], 0x80BF);
    EXPECT_EQ(result[1], 0x0000);
}


// --- 测试 Float 解码 (-1.0f BigEndian ABCD) ---
TEST(RegisterCodecTest, DecodeFloatNegative_BigEndian_ABCD) {

    float result =
        RegisterCodec::decodeFloat(
            {0xBF80, 0x0000},
            Endianness::BigEndian);

    EXPECT_FLOAT_EQ(result, -1.0f);
}


// --- 测试 Float 解码 (-1.0f BigEndianSwap CDAB) ---
TEST(RegisterCodecTest, DecodeFloatNegative_BigEndianSwap_CDAB) {

    float result =
        RegisterCodec::decodeFloat(
            {0x0000, 0xBF80},
            Endianness::BigEndianSwap);

    EXPECT_FLOAT_EQ(result, -1.0f);
}

// --- 测试 Float 解码 (-1.0f LittleEndian DCBA) ---
TEST(RegisterCodecTest, DecodeFloatNegative_LittleEndian_DCBA) {

    float result =
        RegisterCodec::decodeFloat(
            {0x0000, 0x80BF},
            Endianness::LittleEndian);

    EXPECT_FLOAT_EQ(result, -1.0f);
}

// --- 测试 Float 解码 (-1.0f LittleEndianSwap BADC) ---
TEST(RegisterCodecTest, DecodeFloatNegative_LittleEndianSwap_BADC) {

    float result =
        RegisterCodec::decodeFloat(
            {0x80BF, 0x0000},
            Endianness::LittleEndianSwap);

    EXPECT_FLOAT_EQ(result, -1.0f);
}

TEST(RegisterCodecTest, EncodeThenDecodeNegativeFloat_ShouldRestoreOriginal)
{
    float original = -13.25f;

    auto encoded =
        RegisterCodec::encodeFloat(
            original,
            Endianness::BigEndian);

    float decoded =
        RegisterCodec::decodeFloat(
            encoded,
            Endianness::BigEndian);

    EXPECT_FLOAT_EQ(decoded, original);
}