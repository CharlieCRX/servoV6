/**
 * @file test_gantry_position.cpp
 * @brief GantryPosition 值对象单元测试
 *
 * 覆盖设计文档约束：
 *   约束8 (统一位置定义)
 *
 * 测试用例：
 *   TC-3.1 (值对象部分)
 *
 * 同时验证 GantryPosition 作为不可变值对象的完整契约：
 *   - 构造与访问
 *   - 值相等语义
 *   - 序关系（用于区间判断）
 *   - 算术运算（不可变性）
 *   - 零值工厂
 */

#include <gtest/gtest.h>
#include "value/GantryPosition.h"

// ═══════════════════════════════════════════════════════════
// 构造与访问
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, ConstructAndAccess) {
    // TC-3.1: X.position 是龙门整体位置（逻辑坐标）
    GantryPosition pos(42.5);

    EXPECT_DOUBLE_EQ(pos.value(), 42.5);
}

TEST(GantryPositionTest, ZeroPosition) {
    GantryPosition pos = GantryPosition::zero();

    EXPECT_DOUBLE_EQ(pos.value(), 0.0);
}

TEST(GantryPositionTest, NegativePosition) {
    // 龙门可能处于负坐标（操作者后方）
    GantryPosition pos(-100.0);

    EXPECT_DOUBLE_EQ(pos.value(), -100.0);
}

// ═══════════════════════════════════════════════════════════
// 值相等语义 (Value Equality)
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, Equality_SameValueAreEqual) {
    GantryPosition a(10.0);
    GantryPosition b(10.0);

    EXPECT_EQ(a, b);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(GantryPositionTest, Equality_DifferentValuesAreNotEqual) {
    GantryPosition a(10.0);
    GantryPosition b(20.0);

    EXPECT_NE(a, b);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(GantryPositionTest, Equality_Symmetric) {
    GantryPosition a(5.0);
    GantryPosition b(5.0);

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(b == a);
}

TEST(GantryPositionTest, Equality_Transitive) {
    GantryPosition a(7.0);
    GantryPosition b(7.0);
    GantryPosition c(7.0);

    EXPECT_TRUE(a == b);
    EXPECT_TRUE(b == c);
    EXPECT_TRUE(a == c);
}

// ═══════════════════════════════════════════════════════════
// 序关系（用于区间判断，如限位检查）
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, LessThan) {
    GantryPosition a(5.0);
    GantryPosition b(10.0);

    EXPECT_TRUE(a < b);
    EXPECT_FALSE(b < a);
    EXPECT_FALSE(a < a);  // 自反性
}

TEST(GantryPositionTest, LessThanOrEqual) {
    GantryPosition a(5.0);
    GantryPosition b(10.0);
    GantryPosition c(5.0);

    EXPECT_TRUE(a <= b);
    EXPECT_FALSE(b <= a);
    EXPECT_TRUE(a <= c);  // 相等时成立
}

TEST(GantryPositionTest, GreaterThan) {
    GantryPosition a(15.0);
    GantryPosition b(10.0);

    EXPECT_TRUE(a > b);
    EXPECT_FALSE(b > a);
    EXPECT_FALSE(a > a);
}

TEST(GantryPositionTest, GreaterThanOrEqual) {
    GantryPosition a(15.0);
    GantryPosition b(10.0);
    GantryPosition c(15.0);

    EXPECT_TRUE(a >= b);
    EXPECT_FALSE(b >= a);
    EXPECT_TRUE(a >= c);
}

// ═══════════════════════════════════════════════════════════
// 算术运算（保持不可变性）
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, AddOffset_ReturnsNewValue) {
    GantryPosition original(10.0);
    GantryPosition result = original + 5.0;

    // 结果正确
    EXPECT_DOUBLE_EQ(result.value(), 15.0);

    // 原始值不改变（不可变性）
    EXPECT_DOUBLE_EQ(original.value(), 10.0);
}

TEST(GantryPositionTest, SubtractOffset_ReturnsNewValue) {
    GantryPosition original(10.0);
    GantryPosition result = original - 3.0;

    EXPECT_DOUBLE_EQ(result.value(), 7.0);

    // 原始值不改变
    EXPECT_DOUBLE_EQ(original.value(), 10.0);
}

TEST(GantryPositionTest, SubtractTwoPositions_ReturnsDifference) {
    GantryPosition a(25.0);
    GantryPosition b(10.0);

    // 两个位置的差值（裸 double）
    double diff = a - b;
    EXPECT_DOUBLE_EQ(diff, 15.0);

    // 反向差值
    double diffReversed = b - a;
    EXPECT_DOUBLE_EQ(diffReversed, -15.0);
}

TEST(GantryPositionTest, AddNegativeOffset_WorksLikeSubtraction) {
    GantryPosition pos(10.0);
    GantryPosition result = pos + (-5.0);

    EXPECT_DOUBLE_EQ(result.value(), 5.0);
}

TEST(GantryPositionTest, SubtractNegativeOffset_WorksLikeAddition) {
    GantryPosition pos(10.0);
    GantryPosition result = pos - (-5.0);

    EXPECT_DOUBLE_EQ(result.value(), 15.0);
}

// ═══════════════════════════════════════════════════════════
// 拷贝语义
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, CopyConstructor) {
    GantryPosition original(42.0);
    GantryPosition copy(original);  // NOLINT: 有意测试拷贝构造

    EXPECT_EQ(original, copy);
    EXPECT_DOUBLE_EQ(copy.value(), 42.0);
}

TEST(GantryPositionTest, CopyAssignment) {
    GantryPosition original(42.0);
    GantryPosition copy = original;

    EXPECT_EQ(original, copy);
    EXPECT_DOUBLE_EQ(copy.value(), 42.0);
}

TEST(GantryPositionTest, CopyIsDeep) {
    // 值对象拷贝后，两者独立（虽然是同一个 double 值）
    GantryPosition original(100.0);
    GantryPosition copy = original;

    // 对 copy 的算术操作不影响 original
    GantryPosition modified = copy + 50.0;
    EXPECT_DOUBLE_EQ(modified.value(), 150.0);
    EXPECT_DOUBLE_EQ(copy.value(), 100.0);
    EXPECT_DOUBLE_EQ(original.value(), 100.0);
}

// ═══════════════════════════════════════════════════════════
// 边界值测试
// ═══════════════════════════════════════════════════════════

TEST(GantryPositionTest, VeryLargeValue) {
    GantryPosition pos(1e9);  // 10 亿 mm = 1000 km (极端值)
    EXPECT_DOUBLE_EQ(pos.value(), 1e9);
}

TEST(GantryPositionTest, VerySmallValue) {
    GantryPosition pos(1e-9);  // 1 nm (极端精度)
    EXPECT_DOUBLE_EQ(pos.value(), 1e-9);
}

TEST(GantryPositionTest, NaN_EqualityBehavior) {
    // IEEE 754: NaN != NaN
    GantryPosition nanPos(std::numeric_limits<double>::quiet_NaN());
    EXPECT_NE(nanPos, nanPos);  // NaN 与自身不相等
}
