// ============================================================================
// test_gantry_layout.cpp —— Step 3 layout: 龙门 Command/Status/Param 组级布局
// ============================================================================
// 与 tools/plc_read_validate.py read_gantry_command/status/param 及地址表 §6/7/8 一致：
//   GantryCommand 基址 D180、步长 4；GantryStatus 基址 D190、步长 18；
//   GantryParam 基址 D1600、步长 22。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/layout/GantryLayout.h"

namespace plc_vnext::layout {
namespace {

TEST(GantryLayoutTest, Command_Base180_Stride4) {
    auto c0 = gantryCommand(0);
    EXPECT_EQ(c0.command.value(), 180);
    EXPECT_EQ(c0.requestSeq.value(), 181);       // RequestSeq = D181..D182
    EXPECT_EQ(c0.requestSeq.value() + 1, 182);
    auto c1 = gantryCommand(1);
    EXPECT_EQ(c1.command.value(), 184);
    EXPECT_EQ(c1.requestSeq.value(), 185);
}

TEST(GantryLayoutTest, Status_Base190_Stride18) {
    EXPECT_EQ(gantryStatusBase(0).value(), 190);
    EXPECT_EQ(gantryStatusBase(0).value() + 17, 207);  // A 组 D190..D207
    EXPECT_EQ(gantryStatusBase(1).value(), 208);       // B 组 D208..
    EXPECT_EQ(gantryStatusBase(1).value() + 17, 225);  // ..D225
}

TEST(GantryLayoutTest, Param_Base1600_Stride22) {
    EXPECT_EQ(gantryParamBase(0).value(), 1600);
    EXPECT_EQ(gantryParamBase(0).value() + 21, 1621);  // A 组 D1600..D1621
    EXPECT_EQ(gantryParamBase(1).value(), 1622);
    EXPECT_EQ(gantryParamBase(1).value() + 21, 1643);  // B 组 D1622..D1643
}

}  // namespace
}  // namespace plc_vnext::layout
