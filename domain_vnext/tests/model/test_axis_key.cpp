// ============================================================================
// test_axis_key.cpp —— P1 model: AxisKey
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/AxisKey.h"

namespace domain_vnext::model {
namespace {

TEST(AxisKeyTest, Equality_ComparesGroupAndFunction) {
    const auto g0 = plc_vnext::contracts::PlcGroupIndex(0);
    AxisKey a{g0, AxisFunction::X1};
    AxisKey b{g0, AxisFunction::X1};
    AxisKey c{g0, AxisFunction::X2};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(AxisKeyTest, Ordering_GroupsFirstThenFunction) {
    const auto g0 = plc_vnext::contracts::PlcGroupIndex(0);
    const auto g1 = plc_vnext::contracts::PlcGroupIndex(1);
    AxisKey a{g0, AxisFunction::R};
    AxisKey b{g1, AxisFunction::X1};
    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
}

TEST(AxisKeyTest, DefaultConstructibleAggregate) {
    const auto g0 = plc_vnext::contracts::PlcGroupIndex(0);
    AxisKey k{g0, AxisFunction::Y};
    EXPECT_EQ(k.group, g0);
    EXPECT_EQ(k.function, AxisFunction::Y);
}

}  // namespace
}  // namespace domain_vnext::model
