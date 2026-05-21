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