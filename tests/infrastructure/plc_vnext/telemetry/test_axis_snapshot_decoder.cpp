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

}  // namespace
}  // namespace plc_vnext::telemetry
