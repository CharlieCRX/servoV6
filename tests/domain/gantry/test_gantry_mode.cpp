/**
 * @file test_gantry_mode.cpp
 * @brief GantryMode 值对象单元测试
 *
 * 覆盖设计文档约束：
 *   约束1 (模式定义)
 *   约束2 (模式互斥)
 *
 * 测试用例：
 *   TC-1.1 ~ TC-1.4 (值对象部分)
 */

#include <gtest/gtest.h>
#include "value/GantryMode.h"

// ═══════════════════════════════════════════════════════════
// 约束1: 模式定义 — 仅存在两种模式
// ═══════════════════════════════════════════════════════════

TEST(GantryModeTest, ShouldHaveTwoModesOnly) {
    // TC-2.1 补充: 确认只有 Coupled 和 Decoupled 两个枚举值
    // 无法在 C++ 中直接测试枚举基数，但可以通过语义验证
    auto coupled = GantryMode::Coupled;
    auto decoupled = GantryMode::Decoupled;

    // 验证两个模式互不相同
    EXPECT_NE(coupled, decoupled);
}

TEST(GantryModeTest, CoupledMode_RepresentsCoupledSemantics) {
    // 约束1: Coupled 表示联动模式
    GantryMode mode = GantryMode::Coupled;

    EXPECT_TRUE(isCoupled(mode));
    EXPECT_FALSE(isDecoupled(mode));
}

TEST(GantryModeTest, DecoupledMode_RepresentsDecoupledSemantics) {
    // 约束1: Decoupled 表示分动模式
    GantryMode mode = GantryMode::Decoupled;

    EXPECT_TRUE(isDecoupled(mode));
    EXPECT_FALSE(isCoupled(mode));
}

// ═══════════════════════════════════════════════════════════
// 约束2: 模式互斥 — Coupled ⊕ Decoupled
// ═══════════════════════════════════════════════════════════

TEST(GantryModeTest, MutuallyExclusive_CoupledVsDecoupled) {
    // 约束2: 任意时刻只能处于一种模式
    // Coupled 和 Decoupled 互斥
    EXPECT_TRUE(areMutuallyExclusive(GantryMode::Coupled, GantryMode::Decoupled));
    EXPECT_TRUE(areMutuallyExclusive(GantryMode::Decoupled, GantryMode::Coupled));
}

TEST(GantryModeTest, MutuallyExclusive_SameModeNotExclusive) {
    // 同一种模式与自身不互斥（这是 areMutuallyExclusive 的定义：
    // "两个模式不同时 → 互斥成立"）
    EXPECT_FALSE(areMutuallyExclusive(GantryMode::Coupled, GantryMode::Coupled));
    EXPECT_FALSE(areMutuallyExclusive(GantryMode::Decoupled, GantryMode::Decoupled));
}

TEST(GantryModeTest, MutuallyExclusive_IsSymmetric) {
    // 互斥关系是对称的
    bool coupledWithDecoupled = areMutuallyExclusive(GantryMode::Coupled, GantryMode::Decoupled);
    bool decoupledWithCoupled = areMutuallyExclusive(GantryMode::Decoupled, GantryMode::Coupled);

    EXPECT_EQ(coupledWithDecoupled, decoupledWithCoupled);
}

// ═══════════════════════════════════════════════════════════
// 辅助函数: isCoupled / isDecoupled 边界测试
// ═══════════════════════════════════════════════════════════

TEST(GantryModeTest, IsCoupled_ShouldBeTrueOnlyForCoupled) {
    EXPECT_TRUE(isCoupled(GantryMode::Coupled));
    EXPECT_FALSE(isCoupled(GantryMode::Decoupled));
}

TEST(GantryModeTest, IsDecoupled_ShouldBeTrueOnlyForDecoupled) {
    EXPECT_TRUE(isDecoupled(GantryMode::Decoupled));
    EXPECT_FALSE(isDecoupled(GantryMode::Coupled));
}

// ═══════════════════════════════════════════════════════════
// 值对象性质: 枚举可直接比较（编译期类型安全）
// ═══════════════════════════════════════════════════════════

TEST(GantryModeTest, ValueEquality_CompareByValue) {
    // 枚举值对象：相同枚举值即相等
    GantryMode a = GantryMode::Coupled;
    GantryMode b = GantryMode::Coupled;
    GantryMode c = GantryMode::Decoupled;

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(GantryModeTest, CopySemantics_PreservesValue) {
    GantryMode original = GantryMode::Coupled;
    GantryMode copy = original;  // 拷贝

    EXPECT_EQ(original, copy);
    EXPECT_TRUE(isCoupled(copy));
}
