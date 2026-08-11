// ============================================================================
// test_plc_axis_slot.cpp —— Step 1 contracts: 强类型槽位 PlcAxisSlot
// ============================================================================
#include <gtest/gtest.h>
#include <optional>

#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace plc_vnext::contracts {
namespace {

TEST(PlcAxisSlotTest, ValidSlotRanges_0To15) {
    EXPECT_TRUE(PlcAxisSlot::tryCreate(0).has_value());
    EXPECT_TRUE(PlcAxisSlot::tryCreate(15).has_value());
}

TEST(PlcAxisSlotTest, OutOfRangeSlot_ReturnsEmpty) {
    // 越界不抛异常，返回空值；由调用方作为可诊断的解码/拓扑问题处理
    EXPECT_FALSE(PlcAxisSlot::tryCreate(-1).has_value());
    EXPECT_FALSE(PlcAxisSlot::tryCreate(16).has_value());
}

TEST(PlcAxisSlotTest, SlotEqualityAndOrdering) {
    auto a = PlcAxisSlot::tryCreate(3);
    auto b = PlcAxisSlot::tryCreate(3);
    auto c = PlcAxisSlot::tryCreate(5);
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    ASSERT_TRUE(c.has_value());

    EXPECT_TRUE(*a == *b);          // 值语义
    EXPECT_TRUE(*a < *c);           // 全序
    EXPECT_TRUE(*c < PlcAxisSlot::tryCreate(15).value());
    // 反对称
    EXPECT_FALSE(*a < *b);
    EXPECT_FALSE(*c < *a);
}

TEST(PlcAxisSlotTest, SlotValueReturnsIndex) {
    auto s = PlcAxisSlot::tryCreate(3);
    ASSERT_TRUE(s.has_value());
    EXPECT_EQ(s->value(), 3);
}

}  // namespace
}  // namespace plc_vnext::contracts
