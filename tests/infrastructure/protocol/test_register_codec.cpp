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