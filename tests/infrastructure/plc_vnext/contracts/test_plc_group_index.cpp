// ============================================================================
// test_plc_group_index.cpp —— Step 1 contracts: 强类型组号 PlcGroupIndex
// ============================================================================
// 范围 0..1；tryCreate 同构于 PlcAxisSlot（返回 optional），
// 直接构造在越界时抛异常。
// ============================================================================
#include <gtest/gtest.h>
#include <optional>

#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace plc_vnext::contracts {
namespace {

TEST(PlcGroupIndexTest, ValidRange_0To1) {
    EXPECT_TRUE(PlcGroupIndex::tryCreate(0).has_value());
    EXPECT_TRUE(PlcGroupIndex::tryCreate(1).has_value());
}

TEST(PlcGroupIndexTest, TryCreateOutOfRange_ReturnsEmpty) {
    EXPECT_FALSE(PlcGroupIndex::tryCreate(-1).has_value());
    EXPECT_FALSE(PlcGroupIndex::tryCreate(2).has_value());
}

TEST(PlcGroupIndexTest, DirectConstructOutOfRange_Throws) {
    EXPECT_THROW(PlcGroupIndex(2), std::out_of_range);
    EXPECT_THROW(PlcGroupIndex(-1), std::out_of_range);
}

TEST(PlcGroupIndexTest, ValueReturnsIndex) {
    auto g = PlcGroupIndex::tryCreate(1);
    ASSERT_TRUE(g.has_value());
    EXPECT_EQ(g->value(), 1);
}

TEST(PlcGroupIndexTest, EqualityAndOrdering) {
    auto a = PlcGroupIndex::tryCreate(0);
    auto b = PlcGroupIndex::tryCreate(0);
    auto c = PlcGroupIndex::tryCreate(1);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    EXPECT_TRUE(*a == *b);
    EXPECT_TRUE(*a < *c);
    EXPECT_FALSE(*a < *b);
    EXPECT_FALSE(*c < *a);
}

}  // namespace
}  // namespace plc_vnext::contracts
