// ============================================================================
// test_axis_function.cpp —— P1 model: AxisFunction
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/AxisFunction.h"

namespace domain_vnext::model {
namespace {

TEST(AxisFunctionTest, RoleIndexMapping_MatchesPlcConvention) {
    EXPECT_EQ(axisFunctionFromRoleIndex(0), AxisFunction::X1);
    EXPECT_EQ(axisFunctionFromRoleIndex(1), AxisFunction::X2);
    EXPECT_EQ(axisFunctionFromRoleIndex(2), AxisFunction::Y);
    EXPECT_EQ(axisFunctionFromRoleIndex(3), AxisFunction::Z);
    EXPECT_EQ(axisFunctionFromRoleIndex(4), AxisFunction::R);
    EXPECT_EQ(axisFunctionFromRoleIndex(5), AxisFunction::X);
}

TEST(AxisFunctionTest, RoleIndexOutOfRange_ReturnsEmpty) {
    EXPECT_FALSE(axisFunctionFromRoleIndex(-1).has_value());
    EXPECT_FALSE(axisFunctionFromRoleIndex(6).has_value());
}

TEST(AxisFunctionTest, Name_IsStable) {
    EXPECT_EQ(axisFunctionName(AxisFunction::X), "X");
    EXPECT_EQ(axisFunctionName(AxisFunction::X1), "X1");
    EXPECT_EQ(axisFunctionName(AxisFunction::X2), "X2");
    EXPECT_EQ(axisFunctionName(AxisFunction::Y), "Y");
    EXPECT_EQ(axisFunctionName(AxisFunction::Z), "Z");
    EXPECT_EQ(axisFunctionName(AxisFunction::R), "R");
}

}  // namespace
}  // namespace domain_vnext::model
