// ============================================================================
// test_axis_slot_register_layout.cpp —— Step 3 layout: 槽位 0..15 地址公式
// ============================================================================
// 红：本文件引用的 infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h
//     尚不存在，预期编译失败；实现后应全绿。
// 地址公式与《PLC变量协议_Modbus最终地址表.md》§3.1 及
// tools/plc_read_validate.py STANDARD_AXIS_BLOCKS 完全一致：
//   REAL 每项占 2 D，INT/WORD 每项占 1 D；0 基址。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"

namespace plc_vnext::layout {
namespace {

TEST(AxisSlotRegisterLayoutTest, ManualSpeed_Base0_Stride2) {
    EXPECT_EQ(manualSpeed(0).value(), 0);
    EXPECT_EQ(manualSpeed(1).value(), 2);
    EXPECT_EQ(manualSpeed(15).value(), 30);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(manualSpeed(i).value(), 0 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, PositioningSpeed_Base32_Stride2) {
    EXPECT_EQ(positioningSpeed(0).value(), 32);
    EXPECT_EQ(positioningSpeed(15).value(), 62);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(positioningSpeed(i).value(), 32 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, AbsPosition_Base64_Stride2) {
    EXPECT_EQ(absPosition(0).value(), 64);
    EXPECT_EQ(absPosition(15).value(), 94);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(absPosition(i).value(), 64 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, MotionState_Base128_Stride1) {
    EXPECT_EQ(motionState(0).value(), 128);
    EXPECT_EQ(motionState(15).value(), 143);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(motionState(i).value(), 128 + 1 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, RelPosition_Base96_Stride2) {
    EXPECT_EQ(relPosition(0).value(), 96);
    EXPECT_EQ(relPosition(15).value(), 126);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(relPosition(i).value(), 96 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, MotionLimit_Base144_Stride1) {
    EXPECT_EQ(motionLimit(0).value(), 144);
    EXPECT_EQ(motionLimit(15).value(), 159);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(motionLimit(i).value(), 144 + 1 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, AlarmWord_Base160_Stride1) {
    EXPECT_EQ(alarmWord(0).value(), 160);
    EXPECT_EQ(alarmWord(15).value(), 175);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(alarmWord(i).value(), 160 + 1 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, AbsPosTarget_Base1096_Stride2) {
    EXPECT_EQ(absPosTarget(0).value(), 1096);
    EXPECT_EQ(absPosTarget(15).value(), 1126);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(absPosTarget(i).value(), 1096 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, RelPosTarget_Base1128_Stride2) {
    EXPECT_EQ(relPosTarget(0).value(), 1128);
    EXPECT_EQ(relPosTarget(15).value(), 1158);
    for (int i = 0; i <= 15; ++i) {
        EXPECT_EQ(relPosTarget(i).value(), 1128 + 2 * i);
    }
}

TEST(AxisSlotRegisterLayoutTest, Slot0AndSlot15_Boundary) {
    // 槽位 0 与槽位 15 的各字段首地址互不重叠（字段连续排布）
    EXPECT_LT(manualSpeed(15).value(), positioningSpeed(0).value());
    EXPECT_LT(positioningSpeed(15).value(), absPosition(0).value());
    EXPECT_LT(absPosition(15).value(), relPosition(0).value());
    EXPECT_LT(relPosition(15).value(), motionState(0).value());
    EXPECT_LT(motionState(15).value(), motionLimit(0).value());
    EXPECT_LT(motionLimit(15).value(), alarmWord(0).value());
    EXPECT_LT(alarmWord(15).value(), absPosTarget(0).value());
    EXPECT_LT(absPosTarget(15).value(), relPosTarget(0).value());
}

TEST(AxisSlotRegisterLayoutTest, NoOverlap_AllFields) {
    // 相邻数组字段：前一个字段末尾 + 1 不得越过下一个字段首地址
    EXPECT_LE(manualSpeed(15).value() + 1, positioningSpeed(0).value());
    EXPECT_LE(positioningSpeed(15).value() + 1, absPosition(0).value());
    EXPECT_LE(absPosition(15).value() + 1, relPosition(0).value());
    EXPECT_LE(relPosition(15).value() + 1, motionState(0).value());
    EXPECT_LE(motionState(15).value() + 1, motionLimit(0).value());
    EXPECT_LE(motionLimit(15).value() + 1, alarmWord(0).value());
    EXPECT_LE(alarmWord(15).value() + 1, absPosTarget(0).value());
    EXPECT_LE(absPosTarget(15).value() + 1, relPosTarget(0).value());
}

TEST(AxisSlotRegisterLayoutTest, RealFieldsSpanTwoWords) {
    // REAL 字段每项占 2 个 D：相邻槽位地址差为 2
    EXPECT_EQ(manualSpeed(1).value() - manualSpeed(0).value(), 2);
    EXPECT_EQ(absPosition(1).value() - absPosition(0).value(), 2);
    EXPECT_EQ(relPosTarget(1).value() - relPosTarget(0).value(), 2);
}

TEST(AxisSlotRegisterLayoutTest, AbsPosTargetSlot13) {
    // §9.3 SYN0（轴下标 13）公共地址：绝对目标 D1122..D1123
    EXPECT_EQ(absPosTarget(13).value(), 1122);
    EXPECT_EQ(absPosTarget(13).value() + 1, 1123);
}

}  // namespace
}  // namespace plc_vnext::layout
