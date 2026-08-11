// ============================================================================
// test_axis_slot_coil_layout.cpp —— Step 8 layout: 槽位 0..15 的 M 区线圈命令地址
// ============================================================================
// 地址公式与《PLC变量协议_Modbus最终地址表.md》§3.2 完全一致（0 基址）：
//   M 区线圈地址 == M 编号；每种命令一组连续 16 个线圈（i=0..15），步长 1。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/layout/AxisSlotCoilLayout.h"

namespace plc_vnext::layout {
namespace {

TEST(AxisSlotCoilLayoutTest, EnableAxis_Base0_Stride1) {
    EXPECT_EQ(enableAxis(0).value(), 0);       // M0
    EXPECT_EQ(enableAxis(1).value(), 1);
    EXPECT_EQ(enableAxis(15).value(), 15);     // M15
}

TEST(AxisSlotCoilLayoutTest, JogAndEnableMotor_Stride1) {
    EXPECT_EQ(jogForward(0).value(), 80);      // M80
    EXPECT_EQ(jogForward(15).value(), 95);
    EXPECT_EQ(jogBackward(0).value(), 96);     // M96
    EXPECT_EQ(jogBackward(15).value(), 111);
    EXPECT_EQ(enableMotor(0).value(), 128);    // M128
    EXPECT_EQ(enableMotor(15).value(), 143);
    EXPECT_EQ(jogHeartbeat(0).value(), 192);   // M192
    EXPECT_EQ(jogHeartbeat(15).value(), 207);
}

TEST(AxisSlotCoilLayoutTest, Triggers_Stride1) {
    EXPECT_EQ(triggerAbsMove(0).value(), 48);  // M48
    EXPECT_EQ(triggerAbsMove(15).value(), 63);
    EXPECT_EQ(triggerRelMove(0).value(), 64);  // M64
    EXPECT_EQ(triggerRelMove(15).value(), 79);
    EXPECT_EQ(stopRelMove(0).value(), 144);    // M144
    EXPECT_EQ(stopRelMove(15).value(), 159);
    EXPECT_EQ(stopAbsMove(0).value(), 160);    // M160
    EXPECT_EQ(stopAbsMove(15).value(), 175);
}

TEST(AxisSlotCoilLayoutTest, SelfReset_Stride1) {
    EXPECT_EQ(clearRelZero(0).value(), 16);    // M16
    EXPECT_EQ(clearRelZero(15).value(), 31);
    EXPECT_EQ(clearAbsPosition(0).value(), 32);// M32
    EXPECT_EQ(clearAbsPosition(15).value(), 47);
    EXPECT_EQ(setRelZero(0).value(), 176);     // M176
    EXPECT_EQ(setRelZero(15).value(), 191);
}

TEST(AxisSlotCoilLayoutTest, SYN0_Slot13_Addresses) {
    // §9.3 SYN0（轴下标 13）公共地址：绝对定位触发 M61、相对定位触发 M77、
    // 点动正转 M93、点动反转 M109、点动心跳 M205。
    EXPECT_EQ(triggerAbsMove(13).value(), 61);
    EXPECT_EQ(triggerRelMove(13).value(), 77);
    EXPECT_EQ(jogForward(13).value(), 93);
    EXPECT_EQ(jogBackward(13).value(), 109);
    EXPECT_EQ(jogHeartbeat(13).value(), 205);
}

TEST(AxisSlotCoilLayoutTest, BlocksDoNotOverlap) {
    // 各组 16 线圈连续排布且不重叠。
    EXPECT_LT(enableAxis(15).value(), clearRelZero(0).value());
    EXPECT_LT(clearRelZero(15).value(), clearAbsPosition(0).value());
    EXPECT_LT(clearAbsPosition(15).value(), triggerAbsMove(0).value());
    EXPECT_LT(triggerAbsMove(15).value(), triggerRelMove(0).value());
    EXPECT_LT(triggerRelMove(15).value(), jogForward(0).value());
    EXPECT_LT(jogForward(15).value(), jogBackward(0).value());
    EXPECT_LT(jogBackward(15).value(), enableMotor(0).value());
    EXPECT_LT(enableMotor(15).value(), stopRelMove(0).value());
    EXPECT_LT(stopRelMove(15).value(), stopAbsMove(0).value());
    EXPECT_LT(stopAbsMove(15).value(), setRelZero(0).value());
    EXPECT_LT(setRelZero(15).value(), jogHeartbeat(0).value());
}

}  // namespace
}  // namespace plc_vnext::layout
