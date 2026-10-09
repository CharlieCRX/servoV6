// ============================================================================
// test_ui_control_command_adapter.cpp —— UI-1：UiControlCommandAdapter 测试
// ============================================================================
// 依据《UI 新链路控制验证实施清单》UI-1：
//   - 正确生成 source=Ui 命令并返回 operationId；
//   - 非法/非正速度、未知角色、未注入 service 时不提交（返回 "" + lastError）；
//   - 急停 / 解除急停 / 停止始终可提交。
// 依赖：presentation（UiControlCommandAdapter）+ application_vnext + FakeControlRuntime。
// ============================================================================
#include <memory>

#include <QCoreApplication>
#include <QString>

#include <gtest/gtest.h>

#include "presentation/viewmodel/UiControlCommandAdapter.h"
#include "presentation/viewmodel/AxisSpeedLimit.h"   // ★ 界面速度上限（kMaxAxisSpeed/clampAxisSpeed）

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "fake/FakeControlRuntime.h"

#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"

namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::control::FakeControlRuntime;
using application_vnext::control::MotionControlService;
using application_vnext::control::ControlSource;
using plc_vnext::fake::FakePlcRuntimeGateway;

/// gtest 不提供 main；QObject 需要 QCoreApplication。local-static 保证只创建一次。
QCoreApplication* ensureApp() {
    static int argc = 1;
    static char a0[] = "presentation_tests";
    static char* argv[] = { a0, nullptr };
    static QCoreApplication app(argc, argv);
    return &app;
}

/// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。
/// 与 test_udp_command_dispatcher 的六轴拓扑一致，用于让 A.Y(slot2) 可执行写入。
plc_vnext::contracts::TopologySnapshot makeSixAxisTopology() {
    auto snap = plc_vnext::fake::makeValidTopologySnapshot();
    plc_vnext::contracts::TopologyGroup ga;
    ga.valid = true;
    ga.hmiVisible = true;
    ga.groupCode = 0;
    ga.roles = {
        plc_vnext::fake::makeRole(true, 0, 1, 0, 0, 1),    // X1
        plc_vnext::fake::makeRole(true, 1, 2, 0, 0, 2),    // X2
        plc_vnext::fake::makeRole(true, 2, 3, 0, 0, 3),    // Y
        plc_vnext::fake::makeRole(true, 3, 4, 0, 0, 3),    // Z
        plc_vnext::fake::makeRole(true, 4, 5, 0, 0, 4),    // R
        plc_vnext::fake::makeRole(true, 13, 0, 2, 0, 5),   // X（龙门逻辑轴）
        plc_vnext::fake::makeRole(false, -1, 0, 0, 0, 0),
        plc_vnext::fake::makeRole(false, -1, 0, 0, 0, 0),
    };
    snap.groups[0] = ga;
    return snap;
}

/// 读取 Fake 网关中 Y(slot2) 指定命令类别实际写入 PLC 的数值（未写入返回 0）。
float writtenYSpeed(const FakePlcRuntimeGateway& gw,
                    plc_vnext::contracts::PlcAxisCommandKind kind) {
    for (const auto& w : gw.writtenAxis()) {
        if (w.slot.value() == 2 && w.cmd.kind == kind) return w.cmd.realValue;
    }
    return 0.0f;
}

class UiControlCommandAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensureApp();
        gw_.setTopologySnapshot(plc_vnext::fake::makeValidTopologySnapshot());
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
        adapter_ = std::make_unique<UiControlCommandAdapter>(svc_.get());
    }

    /// ★ 让链路进入可执行状态：六轴拓扑 + 可信反馈 + 安全可信 + 连接
    /// → 首 tick 完成 boot 并解除全局锁定（之后的 Set* / Move 才会真正落地写 PLC）。
    /// 必须在首次 tick 之前调用（SetUp 不做 tick，因此测试体内先调用即可）。
    void bootRunnableService() {
        gw_.setTopologySnapshot(makeSixAxisTopology());
        runtime_.setRuntimeSnapshot(plc_vnext::fake::makeTrustedRuntimeSnapshot());
        plc_vnext::contracts::SafetySnapshot safety;
        safety.trusted = true;
        safety.emergencyStop = false;
        runtime_.setSafetySnapshot(safety);
        runtime_.setConnected(true);
        svc_->tick();
    }

    FakePlcRuntimeGateway gw_;
    std::unique_ptr<PlcRuntimeDriverAdapter> driver_;
    FakeControlRuntime runtime_;
    std::unique_ptr<MotionControlService> svc_;
    std::unique_ptr<UiControlCommandAdapter> adapter_;
};

TEST_F(UiControlCommandAdapterTest, SetManualSpeed_SubmitsUiCommand) {
    const QString opId = adapter_->setManualSpeed("A", "Y", 5.0);
    EXPECT_FALSE(opId.isEmpty());
    const auto op = svc_->queryOperation(opId.toStdString());
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->source, ControlSource::Ui);
    EXPECT_EQ(op->axis, "A.Y");
    EXPECT_TRUE(adapter_->lastError().isEmpty());
}

TEST_F(UiControlCommandAdapterTest, StartAbsMove_ZeroSpeed_RejectedByAdapter) {
    const QString opId = adapter_->startAbsMove("A", "Y", 100.0, 0.0);
    EXPECT_TRUE(opId.isEmpty());
    EXPECT_FALSE(adapter_->lastError().isEmpty());
    // 未提交 -> 无排队命令。
    EXPECT_EQ(svc_->queuedCount(), 0u);
}

TEST_F(UiControlCommandAdapterTest, SetPositioningSpeed_NonPositive_RejectedByAdapter) {
    const QString zero = adapter_->setPositioningSpeed("A", "Y", 0.0);
    EXPECT_TRUE(zero.isEmpty());
    EXPECT_FALSE(adapter_->lastError().isEmpty());
    const QString neg = adapter_->setPositioningSpeed("A", "Y", -1.0);
    EXPECT_TRUE(neg.isEmpty());
    EXPECT_FALSE(adapter_->lastError().isEmpty());
    EXPECT_EQ(svc_->queuedCount(), 0u);
}

TEST_F(UiControlCommandAdapterTest, SetPositioningSpeed_Positive_Submits) {
    const QString opId = adapter_->setPositioningSpeed("A", "Y", 12.0);
    EXPECT_FALSE(opId.isEmpty());
    EXPECT_TRUE(adapter_->lastError().isEmpty());
}

TEST_F(UiControlCommandAdapterTest, ClearRelZero_SubmitsUiCommand) {
    const QString opId = adapter_->clearRelZero("A", "Y");
    EXPECT_FALSE(opId.isEmpty());
    const auto op = svc_->queryOperation(opId.toStdString());
    ASSERT_TRUE(op.has_value());
    EXPECT_EQ(op->source, ControlSource::Ui);
    EXPECT_EQ(op->axis, "A.Y");
    EXPECT_TRUE(adapter_->lastError().isEmpty());
}

TEST_F(UiControlCommandAdapterTest, StartRelMove_CarriesMotion) {
    const QString opId = adapter_->startRelMove("A", "Z", 10.0, 2.0);
    EXPECT_FALSE(opId.isEmpty());
    EXPECT_EQ(svc_->queryOperation(opId.toStdString())->axis, "A.Z");
}

TEST_F(UiControlCommandAdapterTest, UnknownRole_ReturnsEmpty) {
    const QString opId = adapter_->startJogForward("A", "Q9");
    EXPECT_TRUE(opId.isEmpty());
    EXPECT_FALSE(adapter_->lastError().isEmpty());
    EXPECT_EQ(svc_->queuedCount(), 0u);
}

TEST_F(UiControlCommandAdapterTest, NullService_ReturnsEmpty) {
    UiControlCommandAdapter noSvc(nullptr);
    EXPECT_TRUE(noSvc.enableMotor("A", "Y", true).isEmpty());
    EXPECT_FALSE(noSvc.lastError().isEmpty());
}

TEST_F(UiControlCommandAdapterTest, EmergencyStop_AlwaysSubmittable) {
    const QString opId = adapter_->triggerEmergencyStop();
    EXPECT_FALSE(opId.isEmpty());
    EXPECT_EQ(svc_->queryOperation(opId.toStdString())->source, ControlSource::Ui);
    EXPECT_FALSE(adapter_->requestEmergencyStopRelease().isEmpty());
}

TEST_F(UiControlCommandAdapterTest, StopMotion_Submits) {
    const QString opId = adapter_->stopMotion("A", "Y");
    EXPECT_FALSE(opId.isEmpty());
}

TEST_F(UiControlCommandAdapterTest, GroupB_ParsesToIndex1) {
    const QString opId = adapter_->setManualSpeed("B", "Y", 3.0);
    EXPECT_FALSE(opId.isEmpty());
    EXPECT_EQ(svc_->queryOperation(opId.toStdString())->axis, "B.Y");
}

// ============================================================================
// ★ 界面速度上限：点动（手动）速度 / 定位速度，所有轴统一最大 30（mm/s 或 °/s）
// ============================================================================
// 需求：界面上设置的速度超过上限时一律按上限（30）设置。
// 上限的唯一事实来源：presentation::kMaxAxisSpeed（AxisSpeedLimit.h），
// QML 侧对应 Theme.maxAxisSpeed。本组测试同时覆盖：
//   - 纯函数钳制语义（含边界与非正数保留）；
//   - UI 唯一可写入口是否把超限速度钳制后才写 PLC（经 FakePlcRuntimeGateway 回读）。
// ============================================================================

TEST_F(UiControlCommandAdapterTest, SpeedLimitHelperClampsAboveMax) {
    EXPECT_DOUBLE_EQ(presentation::kMaxAxisSpeed, 30.0);
    EXPECT_DOUBLE_EQ(presentation::clampAxisSpeed(45.0), 30.0);   // 超过上限 → 按上限
    EXPECT_DOUBLE_EQ(presentation::clampAxisSpeed(30.0), 30.0);   // 恰好等于上限 → 保持
    EXPECT_DOUBLE_EQ(presentation::clampAxisSpeed(12.5), 12.5);   // 上限内 → 原值
    EXPECT_DOUBLE_EQ(presentation::clampAxisSpeed(0.0), 0.0);     // 非正数保持原值
    EXPECT_DOUBLE_EQ(presentation::clampAxisSpeed(-5.0), -5.0);   // → 交由既有校验拒绝
}

TEST_F(UiControlCommandAdapterTest, SetPositioningSpeedAboveMaxIsClampedToThirty) {
    bootRunnableService();

    // 界面（QML 定位速度）输入 45 mm/s → 必须按上限 30 写入 PLC
    const QString opId = adapter_->setPositioningSpeed("A", "Y", 45.0);
    ASSERT_FALSE(opId.isEmpty());
    EXPECT_TRUE(adapter_->lastError().isEmpty());
    svc_->tick();   // 执行一次性写入：Y(slot2) 定位速度寄存器

    EXPECT_FLOAT_EQ(
        writtenYSpeed(gw_, plc_vnext::contracts::PlcAxisCommandKind::SetPositioningSpeed),
        30.0f) << "定位速度超限必须按上限 30 下发";
}

TEST_F(UiControlCommandAdapterTest, SetManualSpeedAboveMaxIsClampedToThirty) {
    bootRunnableService();

    // 界面（QML 点动速度）输入 60 mm/s → 必须按上限 30 写入 PLC
    const QString opId = adapter_->setManualSpeed("A", "Y", 60.0);
    ASSERT_FALSE(opId.isEmpty());
    EXPECT_TRUE(adapter_->lastError().isEmpty());
    svc_->tick();   // 执行一次性写入：Y(slot2) 手动（点动）速度寄存器

    EXPECT_FLOAT_EQ(
        writtenYSpeed(gw_, plc_vnext::contracts::PlcAxisCommandKind::SetManualSpeed),
        30.0f) << "点动（手动）速度超限必须按上限 30 下发";
}

TEST_F(UiControlCommandAdapterTest, StartAbsMoveAboveMaxSpeedIsClampedToThirty) {
    bootRunnableService();

    // 界面（QML 定位触发）携带 40 mm/s → 必须按上限 30 写入 PLC 定位速度寄存器
    const QString opId = adapter_->startAbsMove("A", "Y", /*target=*/12.0, /*speed=*/40.0);
    ASSERT_FALSE(opId.isEmpty());
    svc_->tick();   // 执行：定位速度 + 绝对目标一次性预置

    EXPECT_FLOAT_EQ(
        writtenYSpeed(gw_, plc_vnext::contracts::PlcAxisCommandKind::SetPositioningSpeed),
        30.0f) << "定位速度超限必须按上限 30 下发";
}

}  // namespace
