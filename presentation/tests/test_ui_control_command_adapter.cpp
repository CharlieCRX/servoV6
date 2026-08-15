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

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "fake/FakeControlRuntime.h"

#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

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

class UiControlCommandAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensureApp();
        gw_.setTopologySnapshot(plc_vnext::fake::makeValidTopologySnapshot());
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
        adapter_ = std::make_unique<UiControlCommandAdapter>(svc_.get());
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

}  // namespace
