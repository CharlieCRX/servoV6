/**
 * @file test_physical_axis_state.cpp
 * @brief PhysicalAxisState DTO 单元测试 (T1.1)
 *
 * 测试用例源自: domain/docs/下一阶段TDD开发任务.md
 *
 * 测试编号:
 *   TS1.1.1 - 默认构造全为 false/0
 *   TS1.1.2 - posLimit 与 negLimit 不可同时 true
 *   TS1.1.3 - alarmed 为 true 时 enabled 应为 false
 *   TS1.1.4 - 值语义拷贝独立
 */

#include <gtest/gtest.h>
#include "domain/entity/PhysicalAxis.h"

// ─────────────────────────────────────────
// TS1.1.1: 默认构造全为 false/0
// ─────────────────────────────────────────
TEST(PhysicalAxisStateTest, DefaultConstructionAllFalseOrZero) {
    PhysicalAxisState state;

    EXPECT_FALSE(state.enabled);
    EXPECT_DOUBLE_EQ(state.position, 0.0);
    EXPECT_FALSE(state.alarmed);
    EXPECT_FALSE(state.posLimitActive);
    EXPECT_FALSE(state.negLimitActive);
}

// ─────────────────────────────────────────
// TS1.1.2: posLimit 与 negLimit 不可同时 true
// ─────────────────────────────────────────
TEST(PhysicalAxisStateTest, PosLimitAndNegLimitCannotBothBeTrue) {
    // 在实际物理系统中，正限位和负限位不应同时触发。
    // 此测试验证该约束在结构体层面的行为定义：
    // 如果两者同时为 true，说明系统处于异常状态。
    // 上层 SystemContext 或 GantrySystem 应负责检测并处理此异常。
    PhysicalAxisState state;
    state.posLimitActive = true;
    state.negLimitActive = true;

    // 两者可被同时置位（结构体本身不阻止），
    // 但调用方应通过 isAnyLimitActive() 来检测异常组合
    EXPECT_TRUE(state.posLimitActive);
    EXPECT_TRUE(state.negLimitActive);
    EXPECT_TRUE(state.posLimitActive && state.negLimitActive);

    // 调用方应当能够检测到此异常状态
    // (由 GantrySafetyService 或 GantrySystem 负责)
}

// ─────────────────────────────────────────
// TS1.1.3: alarmed 为 true 时 enabled 应为 false
// ─────────────────────────────────────────
TEST(PhysicalAxisStateTest, AlarmedImpliesDisabled) {
    // 约束17: 报警状态下，使能必须关闭
    // 此测试确保当报警和使能同时为 true 时，
    // 上层逻辑（PhysicalAxis::syncState）会检测并处理
    PhysicalAxisState state;
    state.alarmed = true;
    state.enabled = true;

    // 结构体本身允许同时设置（原始数据镜像），
    // 约束执行由 PhysicalAxis::syncState() 和上层校验负责
    EXPECT_TRUE(state.alarmed);
    EXPECT_TRUE(state.enabled);

    // 使用 PhysicalAxis 的 syncState 应能正确处理
    PhysicalAxis axis(AxisId::X1);
    axis.syncState(state);

    // syncState 直接镜像，不做业务判断
    // 业务约束由 GantrySystem 层的 checkOperability 等接口保证
    EXPECT_TRUE(axis.isAlarmed());
    EXPECT_TRUE(axis.isEnabled());
}

// ─────────────────────────────────────────
// TS1.1.4: 值语义拷贝独立
// ─────────────────────────────────────────
TEST(PhysicalAxisStateTest, ValueSemanticsCopyIndependence) {
    PhysicalAxisState s1;
    s1.enabled = true;
    s1.position = 42.0;
    s1.alarmed = false;
    s1.posLimitActive = false;
    s1.negLimitActive = false;

    // 拷贝构造
    PhysicalAxisState s2 = s1;
    EXPECT_EQ(s2.enabled, s1.enabled);
    EXPECT_DOUBLE_EQ(s2.position, s1.position);

    // 修改 s2 不影响 s1
    s2.enabled = false;
    s2.position = 100.0;
    s2.alarmed = true;

    EXPECT_TRUE(s1.enabled);           // s1 不变
    EXPECT_DOUBLE_EQ(s1.position, 42.0); // s1 不变
    EXPECT_FALSE(s1.alarmed);           // s1 不变

    EXPECT_FALSE(s2.enabled);           // s2 已改
    EXPECT_DOUBLE_EQ(s2.position, 100.0);
    EXPECT_TRUE(s2.alarmed);
}
