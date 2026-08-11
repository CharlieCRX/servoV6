// ============================================================================
// test_axis_topology_layout.cpp —— Step 3 layout: AxisTopology 头部/组/角色地址
// ============================================================================
// 红：本文件引用的 infrastructure/plc_vnext/layout/AxisTopologyLayout.h 尚不存在，
//     预期编译失败；实现后应全绿。
// 与 tools/plc_read_validate.py 的 TOPOLOGY_BASE / GROUP_BASE / GROUP_STRIDE /
// ROLE_BASE_IN_GROUP / ROLE_STRIDE 及地址表 §5 完全一致。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"

namespace plc_vnext::layout {
namespace {

TEST(AxisTopologyLayoutTest, GroupBase_1408_Stride83) {
    EXPECT_EQ(groupBase(0).value(), 1408);
    EXPECT_EQ(groupBase(1).value(), 1491);
}

TEST(AxisTopologyLayoutTest, RoleBase_Formula) {
    // roleBase(g, r) = groupBase(g) + 3 + 10 * r
    EXPECT_EQ(roleBase(0, 0).value(), 1411);
    EXPECT_EQ(roleBase(0, 1).value(), 1421);
    EXPECT_EQ(roleBase(0, 5).value(), 1461);  // A 组 Role[5] = SYN0（X 逻辑轴）
    EXPECT_EQ(roleBase(1, 0).value(), 1494);
    for (int g = 0; g <= 1; ++g) {
        for (int r = 0; r <= 7; ++r) {
            EXPECT_EQ(roleBase(g, r).value(), groupBase(g).value() + 3 + 10 * r);
        }
    }
}

TEST(AxisTopologyLayoutTest, HeadFields_Offsets) {
    EXPECT_EQ(topologyMagic().value(), 1400);
    EXPECT_EQ(topologySchemaVersion().value(), 1402);
    EXPECT_EQ(topologyConfigValid().value(), 1576);
    EXPECT_EQ(topologyConfigErrorCode().value(), 1577);
}

TEST(AxisTopologyLayoutTest, ConfigValid_IsBoolBit0) {
    // ConfigValid 是 D1576 的 bit0（PLC 写、只读）
    EXPECT_EQ(topologyConfigValidBit(), 0);
}

}  // namespace
}  // namespace plc_vnext::layout
