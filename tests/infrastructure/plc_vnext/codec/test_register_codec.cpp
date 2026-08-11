// ============================================================================
// test_register_codec.cpp —— Step 2 codec: BOOL/INT16/DINT/REAL 编解码
// ============================================================================
// 红：本文件引用的 infrastructure/plc_vnext/codec/RegisterCodec.h 等尚不存在，
//     预期编译失败；实现后应全绿。
// 字序断言与 tools/plc_read_validate.py --selftest 完全一致：
//   1.0f = {0x0000, 0x3F80}（低字在前）  25.8f = {0x6666, 0x41CE}
//   DINT 0x12345678 ⇄ {0x5678, 0x1234}   -65536 ⇄ {0x0000, 0xFFFF}
// ============================================================================
#include <array>
#include <cstdint>
#include <limits>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/codec/DecodeError.h"
#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"

namespace plc_vnext::codec {
namespace {

// 汇川 H5U 默认：CDAB 序（BigEndian + LowWordFirst）
constexpr EndianPolicy kCDAB{ByteOrder::BigEndian, WordOrder::LowWordFirst};

TEST(RegisterCodecTest, Real_LowWordFirst_1p0) {
    const std::array<uint16_t, 2> regs{0x0000, 0x3F80};
    float out = 0.0f;
    auto err = RegisterCodec::decodeFloat(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_FLOAT_EQ(out, 1.0f);
}

TEST(RegisterCodecTest, Real_LowWordFirst_25p8) {
    const std::array<uint16_t, 2> regs{0x6666, 0x41CE};
    float out = 0.0f;
    auto err = RegisterCodec::decodeFloat(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_NEAR(out, 25.8f, 1e-3f);
}

TEST(RegisterCodecTest, Real_RoundTrip_LowWordFirst) {
    auto regs = RegisterCodec::encodeFloat(1.0f, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0x0000);
    EXPECT_EQ(regs[1], 0x3F80);

    float out = 0.0f;
    auto err = RegisterCodec::decodeFloat(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_FLOAT_EQ(out, 1.0f);
}

TEST(RegisterCodecTest, Dint_LowWordFirst_RoundTrip) {
    constexpr int32_t kValue = static_cast<int32_t>(0x12345678u);
    auto regs = RegisterCodec::encodeInt32(kValue, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0x5678);  // 低字在前
    EXPECT_EQ(regs[1], 0x1234);

    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(RegisterCodecTest, Dint_Negative_LowWordFirst) {
    constexpr int32_t kValue = -65536;  // 0xFFFF0000
    auto regs = RegisterCodec::encodeInt32(kValue, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0x0000);
    EXPECT_EQ(regs[1], 0xFFFF);

    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(RegisterCodecTest, Int16_Signed_RoundTrip) {
    constexpr int16_t kValue = -32768;
    auto regs = RegisterCodec::encodeInt16(kValue);
    ASSERT_EQ(regs.size(), 1u);
    EXPECT_EQ(regs[0], 0x8000);

    int16_t out = 0;
    auto err = RegisterCodec::decodeInt16(regs, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(RegisterCodecTest, Dint_Min_RoundTrip) {
    // INT32_MIN = 0x80000000：A=0x80，最高位为 1，是验证"无符号拼装"的关键用例。
    // 若实现对 int32_t 做 (A<<24) 会触发有符号左移溢出（UB）。
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    auto regs = RegisterCodec::encodeInt32(kMin, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0x0000);  // 低字在前：低 16 位 0x0000
    EXPECT_EQ(regs[1], 0x8000);  // 高 16 位 0x8000

    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kMin);
}

TEST(RegisterCodecTest, Dint_NegativeOne_RoundTrip) {
    // -1 = 0xFFFFFFFF
    constexpr int32_t kValue = -1;
    auto regs = RegisterCodec::encodeInt32(kValue, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0xFFFF);
    EXPECT_EQ(regs[1], 0xFFFF);

    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, kValue);
}

TEST(RegisterCodecTest, Real_NegativeOne_RoundTrip) {
    // -1.0f = 0xBF800000，位模式含符号位，覆盖负 REAL 的端序路径
    auto regs = RegisterCodec::encodeFloat(-1.0f, kCDAB);
    ASSERT_EQ(regs.size(), 2u);
    EXPECT_EQ(regs[0], 0x0000);
    EXPECT_EQ(regs[1], 0xBF80);

    float out = 0.0f;
    auto err = RegisterCodec::decodeFloat(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_FLOAT_EQ(out, -1.0f);
}

TEST(RegisterCodecTest, Bool_TrueIsNonZero) {
    auto t = RegisterCodec::encodeBool(true);
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0], 0x0001);
    auto f = RegisterCodec::encodeBool(false);
    ASSERT_EQ(f.size(), 1u);
    EXPECT_EQ(f[0], 0x0000);

    bool out = false;
    EXPECT_FALSE(RegisterCodec::decodeBool(f, out).has_value());
    EXPECT_FALSE(out);
    EXPECT_FALSE(RegisterCodec::decodeBool(t, out).has_value());
    EXPECT_TRUE(out);
    // 非 0 即 true
    const std::array<uint16_t, 1> any{0xFFFF};
    EXPECT_FALSE(RegisterCodec::decodeBool(any, out).has_value());
    EXPECT_TRUE(out);
}

TEST(RegisterCodecTest, ByteOrder_BigEndian_Int32) {
    // HighWordFirst + BigEndian（ABCD）：高字在低地址、字内高字节在前
    constexpr EndianPolicy kABCD{ByteOrder::BigEndian, WordOrder::HighWordFirst};
    const std::array<uint16_t, 2> regs{0x1234, 0x5678};
    int32_t out = 0;
    auto err = RegisterCodec::decodeInt32(regs, kABCD, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_EQ(out, static_cast<int32_t>(0x12345678u));
}

TEST(RegisterCodecTest, DecodeError_TooFewRegisters) {
    int32_t iout = 0;
    float fout = 0.0f;
    bool bout = false;
    int16_t sout = 0;

    // 空 / 不足 2 字：Int32 返回 DecodeError（非异常外抛）
    auto err = RegisterCodec::decodeInt32(std::span<const uint16_t>{}, kCDAB, iout);
    ASSERT_TRUE(err.has_value());
    EXPECT_EQ(err->kind, DecodeErrorKind::TooFewRegisters);
    EXPECT_GE(err->index, 0);

    // 仅 1 字对 Int32 同样不足
    const std::array<uint16_t, 1> one{0x0000};
    EXPECT_TRUE(RegisterCodec::decodeInt32(one, kCDAB, iout).has_value());

    // 空输入对 Float/Int16/Bool 同样返回 DecodeError
    EXPECT_TRUE(RegisterCodec::decodeFloat(std::span<const uint16_t>{}, kCDAB, fout).has_value());
    EXPECT_TRUE(RegisterCodec::decodeInt16(std::span<const uint16_t>{}, sout).has_value());
    EXPECT_TRUE(RegisterCodec::decodeBool(std::span<const uint16_t>{}, bout).has_value());
}

TEST(RegisterCodecTest, Real_Negative_RoundTrip) {
    auto regs = RegisterCodec::encodeFloat(-3.5f, kCDAB);
    float out = 0.0f;
    auto err = RegisterCodec::decodeFloat(regs, kCDAB, out);
    EXPECT_FALSE(err.has_value());
    EXPECT_FLOAT_EQ(out, -3.5f);
}

}  // namespace
}  // namespace plc_vnext::codec
