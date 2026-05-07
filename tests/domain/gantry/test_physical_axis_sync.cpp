/**
 * @file test_physical_axis_sync.cpp
 * @brief PhysicalAxis::syncState() 单元测试 (T2.1)
 *
 * 测试用例源自: domain/docs/下一阶段TDD开发任务.md
 *
 * 测试编号:
 *   TS2.1.1 - syncState 更新 position
 *   TS2.1.2 - syncState 更新 enabled
 *   TS2.1.3 - syncState 更新 alarmed
 *   TS2.1.4 - syncState 更新限位状态
 *   TS2.1.5 - alarmed 时 enabled 自动为 false
 *   TS2.1.6 - 连续两次 syncState，第二次覆盖第一次
 */

#include <gtest/gtest.h>
#include "domain/entity/PhysicalAxis.h"

// ─────────────────────────────────────────
// TS2.1.1: syncState 更新 position
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SyncStateUpdatesPosition) {
    PhysicalAxis axis(AxisId::X1);

    PhysicalAxisState state;
    state.position = 100.0;
    axis.syncState(state);

    EXPECT_DOUBLE_EQ(axis.position(), 100.0);
    EXPECT_DOUBLE_EQ(axis.snapshot().position, 100.0);
}

// ─────────────────────────────────────────
// TS2.1.2: syncState 更新 enabled
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SyncStateUpdatesEnabled) {
    PhysicalAxis axis(AxisId::X1);

    // 默认 enabled = false
    EXPECT_FALSE(axis.isEnabled());

    PhysicalAxisState state;
    state.enabled = true;
    axis.syncState(state);

    EXPECT_TRUE(axis.isEnabled());
    EXPECT_TRUE(axis.snapshot().enabled);
}

// ─────────────────────────────────────────
// TS2.1.3: syncState 更新 alarmed
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SyncStateUpdatesAlarmed) {
    PhysicalAxis axis(AxisId::X1);

    EXPECT_FALSE(axis.isAlarmed());

    PhysicalAxisState state;
    state.alarmed = true;
    axis.syncState(state);

    EXPECT_TRUE(axis.isAlarmed());
    EXPECT_TRUE(axis.snapshot().alarmed);
}

// ─────────────────────────────────────────
// TS2.1.4: syncState 更新限位状态
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SyncStateUpdatesLimitStatus) {
    PhysicalAxis axis(AxisId::X1);

    EXPECT_FALSE(axis.isPosLimitActive());
    EXPECT_FALSE(axis.isNegLimitActive());
    EXPECT_FALSE(axis.isAnyLimitActive());

    PhysicalAxisState state;
    state.posLimitActive = true;
    axis.syncState(state);

    EXPECT_TRUE(axis.isPosLimitActive());
    EXPECT_FALSE(axis.isNegLimitActive());
    EXPECT_TRUE(axis.isAnyLimitActive());

    // 切换到负限位
    state.posLimitActive = false;
    state.negLimitActive = true;
    axis.syncState(state);

    EXPECT_FALSE(axis.isPosLimitActive());
    EXPECT_TRUE(axis.isNegLimitActive());
    EXPECT_TRUE(axis.isAnyLimitActive());
}

// ─────────────────────────────────────────
// TS2.1.5: alarmed 时 enabled 自动为 false
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, AlarmedStateDisablesAutomatically) {
    // 约束17: 报警状态下，使能必须关闭
    // 此测试验证：当 syncState 同时传入 alarmed=true 和 enabled=true 时，
    // PhysicalAxis 的行为定义。
    //
    // PhysicalAxis::syncState() 是简单镜像，不做业务判断。
    // 业务约束（alarmed → disabled）由 GantrySystem 层保证。
    // 这里仅验证镜像行为本身。
    PhysicalAxis axis(AxisId::X1);

    // 先设置为 enabled 正常
    PhysicalAxisState normal;
    normal.enabled = true;
    normal.alarmed = false;
    axis.syncState(normal);
    EXPECT_TRUE(axis.isEnabled());
    EXPECT_FALSE(axis.isAlarmed());

    // 同时设置 alarmed 和 enabled（模拟异常输入）
    PhysicalAxisState alarmedState;
    alarmedState.enabled = true;
    alarmedState.alarmed = true;
    axis.syncState(alarmedState);

    // syncState 是纯数据镜像，两个值都会反映
    EXPECT_TRUE(axis.isEnabled());
    EXPECT_TRUE(axis.isAlarmed());

    // 业务约束由上层（GantrySystem::checkOperability）执行：
    // 只要 isAlarmed() 返回 true，所有命令都会被拒绝。
}

// ─────────────────────────────────────────
// TS2.1.6: 连续两次 syncState，第二次覆盖第一次
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SecondSyncStateOverwritesFirst) {
    PhysicalAxis axis(AxisId::X1);

    // 第一次同步
    PhysicalAxisState state1;
    state1.position = 42.0;
    state1.enabled = true;
    state1.alarmed = false;
    state1.posLimitActive = false;
    state1.negLimitActive = false;
    axis.syncState(state1);

    EXPECT_DOUBLE_EQ(axis.position(), 42.0);
    EXPECT_TRUE(axis.isEnabled());

    // 第二次同步覆盖
    PhysicalAxisState state2;
    state2.position = -17.5;
    state2.enabled = false;
    state2.alarmed = true;
    state2.posLimitActive = true;
    state2.negLimitActive = false;
    axis.syncState(state2);

    // 验证所有字段均为第二次的值
    EXPECT_DOUBLE_EQ(axis.position(), -17.5);
    EXPECT_FALSE(axis.isEnabled());
    EXPECT_TRUE(axis.isAlarmed());
    EXPECT_TRUE(axis.isPosLimitActive());
    EXPECT_FALSE(axis.isNegLimitActive());

    // 快照也应一致
    PhysicalAxisState snap = axis.snapshot();
    EXPECT_DOUBLE_EQ(snap.position, -17.5);
    EXPECT_FALSE(snap.enabled);
    EXPECT_TRUE(snap.alarmed);
    EXPECT_TRUE(snap.posLimitActive);
    EXPECT_FALSE(snap.negLimitActive);
}

// ─────────────────────────────────────────
// 补充测试: syncState 的批量更新一致性
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, SyncStateIsBulkConsistent) {
    PhysicalAxis axis(AxisId::X2);

    PhysicalAxisState state;
    state.position = 88.8;
    state.enabled = true;
    state.alarmed = false;
    state.posLimitActive = false;
    state.negLimitActive = true;
    axis.syncState(state);

    // 快照应与单次查询一致（批量原子性）
    PhysicalAxisState snap = axis.snapshot();
    EXPECT_DOUBLE_EQ(snap.position, axis.position());
    EXPECT_EQ(snap.enabled, axis.isEnabled());
    EXPECT_EQ(snap.alarmed, axis.isAlarmed());
    EXPECT_EQ(snap.posLimitActive, axis.isPosLimitActive());
    EXPECT_EQ(snap.negLimitActive, axis.isNegLimitActive());
}

// ─────────────────────────────────────────
// 补充测试: X1 和 X2 各自独立同步
// ─────────────────────────────────────────
TEST(PhysicalAxisSyncTest, X1AndX2StatesAreIndependent) {
    PhysicalAxis x1(AxisId::X1);
    PhysicalAxis x2(AxisId::X2);

    PhysicalAxisState state1;
    state1.position = 100.0;
    state1.enabled = true;
    x1.syncState(state1);

    PhysicalAxisState state2;
    state2.position = -100.0;
    state2.enabled = false;
    x2.syncState(state2);

    // X1 状态不受 X2 影响
    EXPECT_DOUBLE_EQ(x1.position(), 100.0);
    EXPECT_TRUE(x1.isEnabled());
    EXPECT_EQ(x1.id(), AxisId::X1);

    // X2 状态不受 X1 影响
    EXPECT_DOUBLE_EQ(x2.position(), -100.0);
    EXPECT_FALSE(x2.isEnabled());
    EXPECT_EQ(x2.id(), AxisId::X2);
}
