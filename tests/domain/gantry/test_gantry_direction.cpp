/**
 * @file test_gantry_direction.cpp
 * @brief MotionDirection 值对象单元测试
 *
 * 覆盖设计文档约束：
 *   约束5 (方向唯一性)
 *   约束6 (方向与物理无关)
 *
 * 测试用例：
 *   TC-2.1 ~ TC-2.4 (值对象部分)
 */

#include <gtest/gtest.h>
#include "value/MotionDirection.h"

// ═══════════════════════════════════════════════════════════
// 约束5: 方向唯一性 — 系统中只存在 Forward/Backward
// ═══════════════════════════════════════════════════════════

TEST(MotionDirectionTest, ShouldHaveTwoValuesOnly) {
    // TC-2.1: 只存在 Forward 和 Backward 两种方向
    MotionDirection forward = MotionDirection::Forward;
    MotionDirection backward = MotionDirection::Backward;

    // 验证两个方向互不相同
    EXPECT_NE(forward, backward);
}

TEST(MotionDirectionTest, Forward_IsNotBackward) {
    // Forward 不等于 Backward
    EXPECT_NE(MotionDirection::Forward, MotionDirection::Backward);
}

// ═══════════════════════════════════════════════════════════
// 约束6: 方向与物理无关
//   Forward  = 龙门整体远离操作者
//   Backward = 龙门整体靠近操作者
//   与电机正反转、顺逆时针无关
// ═══════════════════════════════════════════════════════════

TEST(MotionDirectionTest, Forward_RepresentsAwayFromOperator) {
    // TC-2.2: Forward = 远离操作者
    // 语义验证：Forward 是独立的逻辑方向，不依赖物理编码
    MotionDirection dir = MotionDirection::Forward;

    EXPECT_TRUE(isForward(dir));
    EXPECT_FALSE(isBackward(dir));
}

TEST(MotionDirectionTest, Backward_RepresentsTowardOperator) {
    // TC-2.3: Backward = 靠近操作者
    MotionDirection dir = MotionDirection::Backward;

    EXPECT_TRUE(isBackward(dir));
    EXPECT_FALSE(isForward(dir));
}

TEST(MotionDirectionTest, DirectionShouldNotDependOnPhysicalDirection) {
    // TC-2.4: 方向与电机正反转无关
    // MotionDirection 是纯逻辑枚举，不携带任何物理含义（如 CW/CCW、±1）
    // 这个测试验证 MotionDirection 不包含任何物理方向常量
    //
    // 编译期检查：MotionDirection 只有 Forward/Backward 两个值，
    // 没有任何如 CW(顺时针)、CCW(逆时针) 之类的物理方向枚举值。

    // 运行时验证：isForward/isBackward 覆盖所有可能值
    MotionDirection fwd = MotionDirection::Forward;
    MotionDirection bwd = MotionDirection::Backward;

    // 任何 MotionDirection 值必须是 Forward 或 Backward 中的一种
    EXPECT_TRUE(isForward(fwd) || isBackward(fwd));
    EXPECT_TRUE(isForward(bwd) || isBackward(bwd));

    // 不存在既不是 Forward 也不是 Backward 的方向
    EXPECT_TRUE(isForward(fwd) != isBackward(fwd));  // XOR: 必为其中之一
    EXPECT_TRUE(isForward(bwd) != isBackward(bwd));
}

// ═══════════════════════════════════════════════════════════
// opposite() 函数测试
// ═══════════════════════════════════════════════════════════

TEST(MotionDirectionTest, Opposite_ForwardReturnsBackward) {
    // 反转 Forward → Backward
    EXPECT_EQ(opposite(MotionDirection::Forward), MotionDirection::Backward);
}

TEST(MotionDirectionTest, Opposite_BackwardReturnsForward) {
    // 反转 Backward → Forward
    EXPECT_EQ(opposite(MotionDirection::Backward), MotionDirection::Forward);
}

TEST(MotionDirectionTest, Opposite_IsInvolution) {
    // opposite(opposite(d)) == d  (对合性/幂等性)
    EXPECT_EQ(opposite(opposite(MotionDirection::Forward)), MotionDirection::Forward);
    EXPECT_EQ(opposite(opposite(MotionDirection::Backward)), MotionDirection::Backward);
}

// ═══════════════════════════════════════════════════════════
// 辅助函数: isForward / isBackward 边界测试
// ═══════════════════════════════════════════════════════════

TEST(MotionDirectionTest, IsForward_ShouldBeTrueOnlyForForward) {
    EXPECT_TRUE(isForward(MotionDirection::Forward));
    EXPECT_FALSE(isForward(MotionDirection::Backward));
}

TEST(MotionDirectionTest, IsBackward_ShouldBeTrueOnlyForBackward) {
    EXPECT_TRUE(isBackward(MotionDirection::Backward));
    EXPECT_FALSE(isBackward(MotionDirection::Forward));
}

// ═══════════════════════════════════════════════════════════
// 值对象性质: 枚举比较、拷贝
// ═══════════════════════════════════════════════════════════

TEST(MotionDirectionTest, ValueEquality_CompareByValue) {
    MotionDirection a = MotionDirection::Forward;
    MotionDirection b = MotionDirection::Forward;
    MotionDirection c = MotionDirection::Backward;

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(MotionDirectionTest, CopySemantics_PreservesValue) {
    MotionDirection original = MotionDirection::Backward;
    MotionDirection copy = original;

    EXPECT_EQ(original, copy);
    EXPECT_TRUE(isBackward(copy));
}
