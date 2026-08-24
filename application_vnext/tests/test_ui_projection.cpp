// ============================================================================
// test_ui_projection.cpp -- Phase 2: UiProjection 状态快照 -> UI 只读投影测试
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 验收：
//   - 快照可信度字段（trusted）与全局锁定影响 UI 的可用/锁定表现；
//   - 快照自带 group/role/hmiVisible，UI 无需自行推导轴映射；
//   - 位置 / motionState / 报警 / 限位 / 急停 / 龙门许可改读快照。
// 本文件只测纯 C++ 投影逻辑（无 Qt），UiControlAdapter 为薄 Qt 转发层。
// ============================================================================
#include <string>

#include <gtest/gtest.h>

#include "application_vnext/control/ControlStateStore.h"
#include "application_vnext/control/UiProjection.h"

namespace application_vnext::control {
namespace {

using plc_vnext::contracts::PlcGroupIndex;

// ---------- 夹具：构造一份可信、正常的快照 ----------

ControlStateSnapshot makeBaselineSnapshot() {
    ControlStateSnapshot snap;
    snap.connection = plc_vnext::contracts::ConnectionState::connectedState("up");
    snap.safety.trusted = true;
    snap.safety.emergencyStop = false;

    auto& ax = snap.axes[0];
    ax.slot = 0;
    ax.group = PlcGroupIndex(0);
    ax.role = domain_vnext::model::AxisFunction::Y;
    ax.hmiVisible = true;
    ax.bound = true;
    ax.trusted = true;
    ax.absPosition = 12.5f;
    ax.relPosition = 3.25f;
    ax.relZeroRecord = 0.0f;
    ax.relZeroTrusted = true;
    ax.manualSpeed = 40.f;
    ax.positioningSpeed = 80.f;
    ax.motionState = 2;          // MotorIdle
    ax.motionLimit = 0;
    ax.alarmWord = 0;
    return snap;
}

TEST(UiProjection, AxisCarriesGroupRoleHmiVisibleWithoutUiDerivation) {
    const auto snap = makeBaselineSnapshot();
    const auto proj = UiProjection::project(snap, /*globallyLocked=*/false);

    ASSERT_EQ(proj.axes.size(), snap.axes.size());
    const auto& v = proj.axes[0];
    // 快照自带映射，UI 无需自行推导轴映射（Phase 2 验收）。
    EXPECT_EQ(v.groupLetter, "A");
    EXPECT_EQ(v.roleName, "Y");
    EXPECT_EQ(v.displayName, "A.Y");
    EXPECT_TRUE(v.hmiVisible);
    EXPECT_TRUE(v.bound);
    EXPECT_TRUE(v.trusted);
    EXPECT_FALSE(v.locked);          // 可信 + 未锁定 + 未急停 + 安全可信 -> 可用
    EXPECT_FLOAT_EQ(v.absPosition, 12.5f);
    EXPECT_FLOAT_EQ(v.relPosition, 3.25f);
    EXPECT_FLOAT_EQ(v.relZeroRecord, 0.0f);
    EXPECT_TRUE(v.relZeroTrusted);
    EXPECT_EQ(v.motionState, 2);
    EXPECT_EQ(v.motionStateName, "MotorIdle(2)");
}

TEST(UiProjection, GlobalLockLocksAllAxes) {
    const auto snap = makeBaselineSnapshot();
    const auto proj = UiProjection::project(snap, /*globallyLocked=*/true);

    EXPECT_TRUE(proj.globallyLocked);
    for (const auto& v : proj.axes) EXPECT_TRUE(v.locked);
}

TEST(UiProjection, EmergencyStopLocksAxesEvenWhenTrusted) {
    auto snap = makeBaselineSnapshot();
    snap.safety.emergencyStop = true;
    const auto proj = UiProjection::project(snap, /*globallyLocked=*/false);

    EXPECT_TRUE(proj.emergencyStop);
    EXPECT_TRUE(proj.axes[0].locked);
}

TEST(UiProjection, UntrustedSafetySnapshotLocksAxes) {
    auto snap = makeBaselineSnapshot();
    snap.safety.trusted = false;
    const auto proj = UiProjection::project(snap, /*globallyLocked=*/false);

    EXPECT_FALSE(proj.safetyTrusted);
    EXPECT_TRUE(proj.axes[0].locked);
}

TEST(UiProjection, UntrustedAxisFeedbackLocksThatAxis) {
    auto snap = makeBaselineSnapshot();
    snap.axes[0].trusted = false;
    // 显式构造另一个可信、绑定的轴，验证它不受影响。
    auto& a1 = snap.axes[1];
    a1.slot = 1;
    a1.group = PlcGroupIndex(0);
    a1.role = domain_vnext::model::AxisFunction::X1;
    a1.bound = true;
    a1.trusted = true;
    a1.motionState = 2;

    const auto proj = UiProjection::project(snap, /*globallyLocked=*/false);
    EXPECT_FALSE(proj.axes[0].trusted);
    EXPECT_TRUE(proj.axes[0].locked);
    // 其他可信、绑定的轴保持可用。
    EXPECT_TRUE(proj.axes[1].trusted);
    EXPECT_FALSE(proj.axes[1].locked);
}

TEST(UiProjection, GantryProjectionCarriesStateAndPermissions) {
    auto snap = makeBaselineSnapshot();
    auto& g = snap.gantries[0];
    g.trusted = true;
    g.state = 3;                     // 已联动
    g.logicalControlAllowed = true;
    g.memberControlAllowed = true;
    g.readyToDecouple = true;
    g.logicalPosition = 55.5f;
    g.skew = -0.2f;

    const auto proj = UiProjection::project(snap, false);
    ASSERT_GE(proj.gantries.size(), 1u);
    const auto& v = proj.gantries[0];
    EXPECT_EQ(v.groupLetter, "A");   // 组索引 0 -> A
    EXPECT_EQ(v.state, 3);
    EXPECT_EQ(v.stateName, "已联动");
    EXPECT_TRUE(v.logicalControlAllowed);
    EXPECT_TRUE(v.memberControlAllowed);
    EXPECT_TRUE(v.readyToDecouple);
    EXPECT_FLOAT_EQ(v.logicalPosition, 55.5f);
    EXPECT_FLOAT_EQ(v.skew, -0.2f);
}

TEST(UiProjection, OperationProjectionCarriesStateNames) {
    OperationEntry op;
    op.operationId = "udp-42";
    op.source = ControlSource::Udp;
    op.axis = "A.Y";
    op.kind = OperationKind::Positioning;
    op.state = OperationState::Running;
    op.position = 10.0f;
    op.motionState = 5;

    const auto v = UiProjection::projectOperation(op);
    EXPECT_EQ(v.operationId, "udp-42");
    EXPECT_EQ(v.sourceName, "UDP");
    EXPECT_EQ(v.axis, "A.Y");
    EXPECT_EQ(v.kindName, "Positioning");
    EXPECT_EQ(v.stateName, "执行中");
    EXPECT_FLOAT_EQ(v.position, 10.0f);
    EXPECT_EQ(v.motionState, 5);
}

TEST(UiProjection, EmptySnapshotDefaultsToLockedOffline) {
    // 未发布任何快照 / 无 service 时的安全默认：离线 + 全局锁定 + 轴锁定。
    const auto proj = UiProjection::project(ControlStateSnapshot{}, /*globallyLocked=*/true);
    EXPECT_FALSE(proj.connected);
    EXPECT_TRUE(proj.globallyLocked);
    EXPECT_FALSE(proj.safetyTrusted);
    EXPECT_FALSE(proj.emergencyStop);
    ASSERT_EQ(proj.axes.size(), plc_vnext::contracts::kRuntimeAxisCount);
    ASSERT_EQ(proj.gantries.size(), plc_vnext::contracts::kRuntimeGroupCount);
    for (const auto& v : proj.axes) EXPECT_TRUE(v.locked);
}

TEST(UiProjection, AxisCarriesMotionLimitName) {
    auto snap = makeBaselineSnapshot();
    // 限位编码直接映射到可读名（Phase 2 验收：位置/motionState/报警/限位均读快照）。
    snap.axes[0].motionLimit = 2;                // 负软限位
    auto proj = UiProjection::project(snap, false);
    EXPECT_EQ(proj.axes[0].motionLimit, 2);
    EXPECT_EQ(proj.axes[0].motionLimitName, "负软限位");

    // 0 = 无限位。
    snap.axes[0].motionLimit = 0;
    proj = UiProjection::project(snap, false);
    EXPECT_EQ(proj.axes[0].motionLimitName, "无限位");
}

TEST(UiProjection, AxisCarriesSoftLimits) {
    auto snap = makeBaselineSnapshot();
    auto& ax = snap.axes[0];
    ax.relZeroRecord = 42.0f;
    ax.relZeroTrusted = true;
    ax.softNegLimit = -100.0f;
    ax.softPosLimit = 200.0f;
    ax.softLimitControl = 0x03u;
    ax.softLimitTrusted = true;

    const auto proj = UiProjection::project(snap, false);
    EXPECT_FLOAT_EQ(proj.axes[0].relZeroRecord, 42.0f);
    EXPECT_TRUE(proj.axes[0].relZeroTrusted);
    EXPECT_FLOAT_EQ(proj.axes[0].softNegLimit, -100.0f);
    EXPECT_FLOAT_EQ(proj.axes[0].softPosLimit, 200.0f);
    EXPECT_EQ(proj.axes[0].softLimitControl, 0x03u);
    EXPECT_TRUE(proj.axes[0].softLimitTrusted);

    // 参数区未可信时 softLimitTrusted 透传为 false。
    snap.axes[0].softLimitTrusted = false;
    const auto proj2 = UiProjection::project(snap, false);
    EXPECT_FALSE(proj2.axes[0].softLimitTrusted);
}

TEST(UiProjection, NameMappersCoverKnownValues) {
    EXPECT_EQ(UiProjection::groupLetter(PlcGroupIndex(0)), "A");
    EXPECT_EQ(UiProjection::groupLetter(PlcGroupIndex(1)), "B");
    EXPECT_EQ(UiProjection::motionStateName(6), "RelMove(6)");
    EXPECT_EQ(UiProjection::motionLimitName(0), "无限位");
    EXPECT_EQ(UiProjection::motionLimitName(1), "正软限位");
    EXPECT_EQ(UiProjection::motionLimitName(2), "负软限位");
    EXPECT_EQ(UiProjection::motionLimitName(3), "正硬限位");
    EXPECT_EQ(UiProjection::motionLimitName(4), "负硬限位");
    EXPECT_EQ(UiProjection::gantryStateName(5), "故障");
    EXPECT_EQ(UiProjection::operationStateName(OperationState::Accepted), "等待 PLC");
    EXPECT_EQ(UiProjection::operationKindName(OperationKind::Jog), "Jog");
}

}  // namespace
}  // namespace application_vnext::control
