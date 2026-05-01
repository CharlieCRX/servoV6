/**
 * @file test_gantry_position.cpp
 * @brief GantryPosition 值对象单元测试 + PhysicalAxis/LogicalAxis 实体位置测试
 *
 * 覆盖设计文档约束：
 *   约束8 (统一位置定义)
 *   约束9 (镜像关系)
 *   约束10 (逻辑位置计算)
 *   约束11 (位置一致性失效判定)
 *
 * 测试用例：
 *   TC-3.1 (值对象部分) — GantryPosition 构造与访问
 *   TC-3.2 (实体部分) — PhysicalAxis 镜像关系
 *   TC-3.3 (实体部分) — LogicalAxis 位置 = X1.pos
 *   TC-3.4 (实体部分) — PhysicalAxis syncState
 *   TC-3.5 (实体部分) — PhysicalAxis 失同步检测
 *
 * 同时验证 GantryPosition 作为不可变值对象的完整契约：
 *   - 构造与访问
 *   - 值相等语义
 *   - 序关系（用于区间判断）
 *   - 算术运算（不可变性）
 *   - 零值工厂
 *   - Entity 层位置集成
 */

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "value/GantryPosition.h"
#include "entity/PhysicalAxis.h"
#include "entity/LogicalAxis.h"
#include "entity/GantrySystem.h"
#include "value/PositionConsistency.h"

// ═══════════════════════════════════════════════════════════
// 构造与访问 (值对象部分)
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

// ═══════════════════════════════════════════════════════════
// 实体部分：PhysicalAxis 与 LogicalAxis 位置测试
// ═══════════════════════════════════════════════════════════

// ============================================================
// TC-3.2: PhysicalAxis X1 与 X2 镜像关系 (约束 9)
// ============================================================
TEST(GantryPositionEntityTest, PhysicalAxis_X1AndX2AreMirrored) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    // 设置镜像位置 (X1.pos = 50.0, X2.pos = -50.0)
    PhysicalAxisState x1State;
    x1State.enabled = true;
    x1State.position = 50.0;
    x1.syncState(x1State);

    PhysicalAxisState x2State;
    x2State.enabled = true;
    x2State.position = -50.0;
    x2.syncState(x2State);

    // 验证镜像关系：X1.pos ≈ -X2.pos
    double deviation = PositionConsistency::computeDeviation(
        x1.position(), x2.position()
    );
    EXPECT_NEAR(deviation, 0.0, 1e-9) << "完美镜像时偏差应为 0";
}

// ============================================================
// TC-3.3: LogicalAxis 位置 = X1.pos (约束 8, 10)
// ============================================================
TEST(GantryPositionEntityTest, LogicalAxis_PositionEqualsX1) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    // 设置 X1 位置
    PhysicalAxisState x1State;
    x1State.enabled = true;
    x1State.position = 100.0;
    x1.syncState(x1State);

    PhysicalAxisState x2State;
    x2State.enabled = true;
    x2State.position = -100.0;
    x2.syncState(x2State);

    GantrySystem system(x1, x2);

    // 建立联动
    auto result = system.requestCoupling();
    ASSERT_TRUE(result.allowed);

    // 聚合状态 (计算 X 逻辑位置)
    system.aggregateState();

    // X.position = X1.pos (约束 10)
    EXPECT_DOUBLE_EQ(system.logical().position().value(), 100.0);
}

// ============================================================
// TC-3.4: PhysicalAxis syncState (约束 9)
// ============================================================
TEST(GantryPositionEntityTest, PhysicalAxis_SyncState) {
    PhysicalAxis ax(AxisId::X1);

    // 初始状态
    EXPECT_FALSE(ax.isEnabled());
    EXPECT_DOUBLE_EQ(ax.position(), 0.0);

    // 同步状态
    PhysicalAxisState newState;
    newState.enabled = true;
    newState.position = 25.0;
    ax.syncState(newState);

    // 验证同步成功
    EXPECT_TRUE(ax.isEnabled());
    EXPECT_DOUBLE_EQ(ax.position(), 25.0);
}

// ============================================================
// TC-3.5: PhysicalAxis 失同步检测 (约束 11)
// ============================================================
TEST(GantryPositionEntityTest, PhysicalAxis_OutOfSync_Detected) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    PhysicalAxisState x1State;
    x1State.enabled = true;
    x1State.position = 50.0;
    x1.syncState(x1State);

    PhysicalAxisState x2State;
    x2State.enabled = true;
    x2State.position = -49.95;  // 偏差 0.05
    x2.syncState(x2State);

    double deviation = PositionConsistency::computeDeviation(
        x1.position(), x2.position()
    );
    EXPECT_NEAR(deviation, 0.05, 1e-9);

    // 偏差 > epsilon (0.01) → 不一致
    bool consistent = PositionConsistency::isConsistent(
        x1.position(), x2.position()
    );
    EXPECT_FALSE(consistent) << "偏差 0.05 应超出 epsilon 0.01, 判定为不一致";
}

// ============================================================
// 附加：GantrySystem 联动建立时位置一致性检查 (约束 13)
// ============================================================
TEST(GantryPositionEntityTest, GantrySystem_CouplingRejectedWhenOutOfSync) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    // 失同步状态
    PhysicalAxisState x1State;
    x1State.enabled = true;
    x1State.position = 100.0;
    x1.syncState(x1State);

    PhysicalAxisState x2State;
    x2State.enabled = true;
    x2State.position = -99.0;  // 偏差 1.0 > epsilon
    x2.syncState(x2State);

    GantrySystem system(x1, x2);
    auto result = system.requestCoupling();

    EXPECT_FALSE(result.allowed);
    EXPECT_EQ(system.mode(), GantryMode::Decoupled);  // 未进入 Coupled
}

// ============================================================
// 附加：完整位置生命周期 (syncState → aggregate → LogicalPosition)
// ============================================================
TEST(GantryPositionEntityTest, FullPositionLifecycle) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    // 1. 同步两个物理轴的镜像位置
    PhysicalAxisState x1State;
    x1State.enabled = true;
    x1State.position = 75.5;
    x1.syncState(x1State);

    PhysicalAxisState x2State;
    x2State.enabled = true;
    x2State.position = -75.5;
    x2.syncState(x2State);

    // 2. 创建系统并建立联动
    GantrySystem system(x1, x2);
    auto result = system.requestCoupling();
    ASSERT_TRUE(result.allowed) << "联动建立失败: " << result.failReason;
    EXPECT_EQ(system.mode(), GantryMode::Coupled);

    // 3. 聚合状态 → X.position = X1.pos
    system.aggregateState();
    EXPECT_DOUBLE_EQ(system.logical().position().value(), 75.5);

    // 4. 模拟 X1 移动后重新聚合
    PhysicalAxisState newX1State;
    newX1State.enabled = true;
    newX1State.position = 200.0;
    system.x1().syncState(newX1State);
    system.aggregateState();
    EXPECT_DOUBLE_EQ(system.logical().position().value(), 200.0);
}
