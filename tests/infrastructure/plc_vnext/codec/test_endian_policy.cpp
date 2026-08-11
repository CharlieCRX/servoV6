// ============================================================================
// test_endian_policy.cpp —— Step 2 codec: ByteOrder/WordOrder 组合枚举值语义
// ============================================================================
// 红：本文件引用的 codec/EndianPolicy.h 与 RegisterCodec 尚不存在，预期编译失败；
//     实现后应全绿。
// 验证四种 (ByteOrder, WordOrder) 组合对应的标准寄存器布局，与旧 RegisterCodec
// 测试中的 ABCD / CDAB / DCBA / BADC 语义完全一致。
// ============================================================================
#include <array>
#include <cstdint>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"

namespace plc_vnext::codec {
namespace {

// 四种经典组合（与旧 tests/infrastructure/protocol/test_register_codec.cpp 同名）
constexpr EndianPolicy kABCD{ByteOrder::BigEndian, WordOrder::HighWordFirst};
constexpr EndianPolicy kCDAB{ByteOrder::BigEndian, WordOrder::LowWordFirst};  // 汇川
constexpr EndianPolicy kDCBA{ByteOrder::LittleEndian, WordOrder::LowWordFirst};
constexpr EndianPolicy kBADC{ByteOrder::LittleEndian, WordOrder::HighWordFirst};

TEST(EndianPolicyTest, Abcd_BigEndianHighWordFirst) {
    // 0x12345678 -> 高字 0x1234、低字 0x5678，且高字在前
    auto r = RegisterCodec::encodeInt32(static_cast<int32_t>(0x12345678u), kABCD);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 0x1234);
    EXPECT_EQ(r[1], 0x5678);
    // 1.0f = 0x3F800000 -> {0x3F80, 0x0000}
    auto f = RegisterCodec::encodeFloat(1.0f, kABCD);
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0], 0x3F80);
    EXPECT_EQ(f[1], 0x0000);
}

TEST(EndianPolicyTest, Cdab_BigEndianLowWordFirst) {
    // 0x12345678 -> 低字 0x5678 在前
    auto r = RegisterCodec::encodeInt32(static_cast<int32_t>(0x12345678u), kCDAB);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 0x5678);
    EXPECT_EQ(r[1], 0x1234);
    // 1.0f -> {0x0000, 0x3F80}
    auto f = RegisterCodec::encodeFloat(1.0f, kCDAB);
    ASSERT_EQ(f.size(), 2u);
    EXPECT_EQ(f[0], 0x0000);
    EXPECT_EQ(f[1], 0x3F80);
}

TEST(EndianPolicyTest, Dcba_LittleEndianLowWordFirst) {
    auto r = RegisterCodec::encodeInt32(static_cast<int32_t>(0x12345678u), kDCBA);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 0x7856);
    EXPECT_EQ(r[1], 0x3412);
}

TEST(EndianPolicyTest, Badc_LittleEndianHighWordFirst) {
    auto r = RegisterCodec::encodeInt32(static_cast<int32_t>(0x12345678u), kBADC);
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[0], 0x3412);
    EXPECT_EQ(r[1], 0x7856);
}

TEST(EndianPolicyTest, Dcba_DecodeKnownVector) {
    // DCBA = LittleEndian + LowWordFirst
    const std::array<uint16_t, 2> regs{0x7856, 0x3412};
    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kDCBA, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, static_cast<int32_t>(0x12345678u));
}

TEST(EndianPolicyTest, Badc_DecodeKnownVector) {
    // BADC = LittleEndian + HighWordFirst
    const std::array<uint16_t, 2> regs{0x3412, 0x7856};
    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kBADC, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, static_cast<int32_t>(0x12345678u));
}

TEST(EndianPolicyTest, Dcba_RoundTrip) {
    constexpr int32_t kValue = static_cast<int32_t>(0x12345678u);
    auto regs = RegisterCodec::encodeInt32(kValue, kDCBA);
    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kDCBA, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(EndianPolicyTest, Badc_RoundTrip) {
    constexpr int32_t kValue = static_cast<int32_t>(0x12345678u);
    auto regs = RegisterCodec::encodeInt32(kValue, kBADC);
    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kBADC, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(EndianPolicyTest, PolicyEquality_ValueSemantics) {
    // EndianPolicy 是简单值聚合：同组合相等
    // （外层圆括号避免宏把 EndianPolicy{...} 内逗号当作第二个参数）
    EXPECT_TRUE((kCDAB == EndianPolicy{ByteOrder::BigEndian, WordOrder::LowWordFirst}));
    EXPECT_FALSE(kABCD == kCDAB);
}

}  // namespace
}  // namespace plc_vnext::codec
