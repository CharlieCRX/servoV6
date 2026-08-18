// ============================================================================
// test_axis_snapshot_decoder.cpp —— Step 7 telemetry: AxisSnapshotDecoder
// ============================================================================
// 红：引用 infrastructure/plc_vnext/telemetry/AxisSnapshotDecoder.h 与
//     contracts/AxisRuntimeSnapshot.h；对应 7.1 红用例：
//       DecodesSingleSlotFeedback / RealLowWordFirst_Decode /
//       MissingBlock_MarksUntrusted / InvalidEnum_KeepsRawValue
// 字段偏移来自 layout::AxisSlotRegisterLayout，字序 CDAB（低字在前）。
// ============================================================================
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"
#include "infrastructure/plc_vnext/telemetry/AxisSnapshotDecoder.h"
#include "tests/infrastructure/plc_vnext/support/TelemetryFixture.h"

namespace plc_vnext::telemetry {
namespace {

codec::RawRegisterBlock toAxisBlock(std::vector<uint16_t> words) {
    return codec::RawRegisterBlock(0, std::move(words), 0, {}, 0);
}

TEST(AxisSnapshotDecoderTest, DecodesSingleSlotFeedback) {
    const int slot = 3;
    auto regs = test::makeAxisBlock();
    test::writeFloat(regs, layout::manualSpeed(slot).value(), 100.5f);
    test::writeFloat(regs, layout::positioningSpeed(slot).value(), 200.25f);
    test::writeFloat(regs, layout::absPosition(slot).value(), 1234.5f);
    test::writeFloat(regs, layout::relPosition(slot).value(), 56.75f);
    test::writeInt16(regs, layout::motionState(slot).value(), 4);    // 绝对定位
    test::writeInt16(regs, layout::motionLimit(slot).value(), 1);    // 正软限位
    test::writeWord(regs, layout::alarmWord(slot).value(), 0x0021);  // bit0 + bit5

    auto s = AxisSnapshotDecoder::decode(toAxisBlock(std::move(regs)), slot);
    EXPECT_TRUE(s.trusted);
    EXPECT_EQ(s.slot, 3);
    EXPECT_FLOAT_EQ(s.manualSpeed, 100.5f);
    EXPECT_FLOAT_EQ(s.positioningSpeed, 200.25f);
    EXPECT_FLOAT_EQ(s.absPosition, 1234.5f);
    EXPECT_FLOAT_EQ(s.relPosition, 56.75f);
    EXPECT_EQ(s.motionState, 4);
    EXPECT_EQ(s.motionLimit, 1);
    EXPECT_EQ(s.alarmWord, 0x0021);
}

TEST(AxisSnapshotDecoderTest, RealLowWordFirst_Decode) {
    const int slot = 0;
    auto regs = test::makeAxisBlock();
    // 1.0f = 0x3F800000 -> D0=0x0000, D1=0x3F80（低字在低地址）
    test::writeWord(regs, layout::manualSpeed(slot).value(), 0x0000);
    test::writeWord(regs, layout::manualSpeed(slot).value() + 1, 0x3F80);

    auto s = AxisSnapshotDecoder::decode(toAxisBlock(std::move(regs)), slot);
    ASSERT_TRUE(s.trusted);
    EXPECT_FLOAT_EQ(s.manualSpeed, 1.0f);
}

TEST(AxisSnapshotDecoderTest, MissingBlock_MarksUntrusted) {
    // 缺数据：任一字段缺失 → trusted=false，不得把默认 0 冒充正常。
    auto s = AxisSnapshotDecoder::decode(toAxisBlock({}), 5);
    EXPECT_FALSE(s.trusted);
    EXPECT_EQ(s.slot, 5);
}

TEST(AxisSnapshotDecoderTest, InvalidEnum_KeepsRawValue) {
    const int slot = 2;
    auto regs = test::makeAxisBlock();
    test::writeInt16(regs, layout::motionState(slot).value(), 99);  // 未知状态

    auto s = AxisSnapshotDecoder::decode(toAxisBlock(std::move(regs)), slot);
    ASSERT_TRUE(s.trusted);
    EXPECT_EQ(s.motionState, 99);  // 原样保留，不拒绝、不篡改
}

// 参数区（D1064..D1243）解码：软负 D1160 / 软正 D1192 / 控制字 D1228，低字在前 CDAB。
codec::RawRegisterBlock toParamBlock(std::vector<uint16_t> words) {
    return codec::RawRegisterBlock(layout::relZeroRecord(0).value(),
                                   std::move(words), 0, {}, 0);
}

TEST(AxisSnapshotDecoderTest, DecodesSoftLimitParams) {
    const int slot = 2;
    const int base = layout::relZeroRecord(0).value();   // D1064
    auto regs = std::vector<uint16_t>(180, 0);           // D1064..D1243，下标0 == D1064

    test::writeFloat(regs, layout::softNegLimit(slot).value() - base, -100.0f);
    test::writeFloat(regs, layout::softPosLimit(slot).value() - base, 200.0f);
    test::writeWord(regs, layout::softLimitControl(slot).value() - base, 0x0003);  // bit0正 bit1负

    auto s = AxisSnapshotDecoder::decodeParams(toParamBlock(std::move(regs)), slot);
    EXPECT_TRUE(s.trusted);
    EXPECT_FLOAT_EQ(s.softNegLimit, -100.0f);
    EXPECT_FLOAT_EQ(s.softPosLimit, 200.0f);
    EXPECT_EQ(s.softLimitControl, 0x0003u);
}

TEST(AxisSnapshotDecoderTest, MissingParamBlock_MarksParamsUntrusted) {
    // 参数区缺数据 → params.trusted=false（不把 0 冒充正常）。
    auto s = AxisSnapshotDecoder::decodeParams(toParamBlock({}), 5);
    EXPECT_FALSE(s.trusted);
    EXPECT_EQ(s.slot, 5);
}

}  // namespace
}  // namespace plc_vnext::telemetry
