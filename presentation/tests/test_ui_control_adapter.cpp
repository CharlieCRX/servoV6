// ============================================================================
// test_ui_control_adapter.cpp -- Phase 2: UiControlAdapter 与真实服务集成测试
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 补验：
//   1) UiControlAdapter(service) 调用 refresh() 后，Q_PROPERTY / QVariantMap 字段
//      正确反映真实 MotionControlService 快照（connected / axis 位置 / locked /
//      gantry 状态）；
//   2) 真实 service 运行多次 tick 后，适配器可见状态实时变化（QML 绑定于这些属性，
//      故此处验证即"QML 侧可见"）；
//   3) QQmlEngine 真实 QML 绑定（adapter 属性绑定）读取一致；
//   4) nullptr 构造的安全默认离线/锁定态。
//
// 依赖：presentation（UiControlAdapter）+ application_vnext（MotionControlService/
// PlcRuntimeDriverAdapter）+ FakeControlRuntime（纯头）+ FakePlcRuntimeGateway（库）。
// ============================================================================
#include <memory>

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <QUrl>

#include <gtest/gtest.h>

#include "presentation/viewmodel/UiControlAdapter.h"

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "fake/FakeControlRuntime.h"

#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::control::FakeControlRuntime;
using application_vnext::control::MotionControlService;
using plc_vnext::contracts::RuntimeSnapshot;
using plc_vnext::contracts::SafetySnapshot;
using plc_vnext::contracts::SnapshotQuality;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeAxisSnapshot;
using plc_vnext::fake::makeGantryStatusSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

/// gtest 不提供 main；QQmlEngine 需要 QCoreApplication。local-static 保证只创建一次。
QCoreApplication* ensureApp() {
    static int argc = 1;
    static char a0[] = "presentation_tests";
    static char* argv[] = { a0, nullptr };
    static QCoreApplication app(argc, argv);
    return &app;
}

/// 可信基线运行快照：A 组 slot0(拓扑 Role[0]=X1) 有辨识位置/运动态；龙门 A 已联动。
RuntimeSnapshot makeBaselineRuntime() {
    RuntimeSnapshot snap;
    snap.quality = SnapshotQuality::Trusted;
    snap.axes[0] = makeAxisSnapshot(0, /*manual=*/40, /*positioning=*/80,
                                    /*abs=*/12.5f, /*rel=*/3.25f,
                                    /*motionState=*/2, /*motionLimit=*/0, /*alarm=*/0);
    snap.axes[1] = makeAxisSnapshot(1);
    snap.gantry[0] = makeGantryStatusSnapshot(0, /*state=*/3, /*ack=*/9,
                                              /*cmdResult=*/2,
                                              /*x1InGear=*/true, /*x2InGear=*/true);
    snap.gantry[0].logicalControlAllowed = true;
    snap.gantry[0].memberControlAllowed = true;
    snap.gantry[1] = makeGantryStatusSnapshot(1);
    return snap;
}

class UiControlAdapterIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensureApp();
        gw_.setTopologySnapshot(makeValidTopologySnapshot());
        runtime_.setRuntimeSnapshot(makeBaselineRuntime());
        SafetySnapshot s;
        s.trusted = true;
        s.emergencyStop = false;
        runtime_.setSafetySnapshot(s);
        runtime_.setConnected(true);
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
    }

    FakePlcRuntimeGateway          gw_;
    FakeControlRuntime             runtime_;
    std::unique_ptr<PlcRuntimeDriverAdapter> driver_;
    std::unique_ptr<MotionControlService>   svc_;
};

// ---------- 测试 1：真实 service 快照投影到 UiControlAdapter ----------

TEST_F(UiControlAdapterIntegrationTest, ProjectsRealServiceSnapshot) {
    svc_->tick();  // boot + 单次读反馈 + 发布快照
    UiControlAdapter adapter(svc_.get());
    adapter.refresh();

    EXPECT_TRUE(adapter.connected());
    EXPECT_FALSE(adapter.emergencyStop());
    EXPECT_TRUE(adapter.safetyTrusted());
    EXPECT_FALSE(adapter.globallyLocked());

    auto ax = adapter.axisFor("A", "X1");
    ASSERT_FALSE(ax.isEmpty());
    EXPECT_EQ(ax["group"].toString(), "A");
    EXPECT_EQ(ax["role"].toString(), "X1");
    EXPECT_EQ(ax["displayName"].toString(), "A.X1");
    EXPECT_TRUE(ax["bound"].toBool());
    EXPECT_TRUE(ax["hmiVisible"].toBool());
    EXPECT_TRUE(ax["trusted"].toBool());
    EXPECT_FALSE(ax["locked"].toBool());
    EXPECT_DOUBLE_EQ(ax["absPosition"].toDouble(), 12.5);
    EXPECT_DOUBLE_EQ(ax["relPosition"].toDouble(), 3.25);
    EXPECT_EQ(ax["motionState"].toInt(), 2);
    EXPECT_EQ(ax["motionStateName"].toString(), "MotorIdle(2)");

    auto g = adapter.gantry(0);
    ASSERT_FALSE(g.isEmpty());
    EXPECT_EQ(g["group"].toString(), "A");
    EXPECT_EQ(g["state"].toInt(), 3);
    EXPECT_EQ(g["stateName"].toString(), "已联动");
    EXPECT_TRUE(g["logicalControlAllowed"].toBool());
    EXPECT_TRUE(g["memberControlAllowed"].toBool());

    EXPECT_EQ(adapter.axes().size(), plc_vnext::contracts::kRuntimeAxisCount);
    EXPECT_EQ(adapter.gantries().size(), plc_vnext::contracts::kRuntimeGroupCount);
}

// ---------- 测试 2：多次 tick 后适配器可见状态实时变化 ----------

TEST_F(UiControlAdapterIntegrationTest, ReflectsTickChangesAcrossSamples) {
    svc_->tick();
    UiControlAdapter adapter(svc_.get());
    adapter.refresh();
    EXPECT_DOUBLE_EQ(adapter.axisFor("A", "X1")["absPosition"].toDouble(), 12.5);
    EXPECT_EQ(adapter.axisFor("A", "X1")["motionStateName"].toString(), "MotorIdle(2)");

    // 第二次 tick：位置/运动态/龙门状态变化
    auto rt = makeBaselineRuntime();
    rt.axes[0].absPosition = 25.0f;
    rt.axes[0].motionState = 5;              // AbsMove
    rt.gantry[0].state = 5;                  // 故障
    rt.gantry[0].logicalControlAllowed = false;
    runtime_.setRuntimeSnapshot(rt);
    svc_->tick();
    adapter.refresh();
    EXPECT_DOUBLE_EQ(adapter.axisFor("A", "X1")["absPosition"].toDouble(), 25.0);
    EXPECT_EQ(adapter.axisFor("A", "X1")["motionStateName"].toString(), "AbsMove(5)");
    EXPECT_EQ(adapter.gantry(0)["stateName"].toString(), "故障");
    EXPECT_FALSE(adapter.gantry(0)["logicalControlAllowed"].toBool());

    // 断线 → 全局锁定 → 轴 locked（连接/可信度影响可用性）
    runtime_.setConnected(false);
    svc_->tick();
    adapter.refresh();
    EXPECT_FALSE(adapter.connected());
    EXPECT_TRUE(adapter.globallyLocked());
    EXPECT_TRUE(adapter.axisFor("A", "X1")["locked"].toBool());
}

// ---------- 测试 3：QQmlEngine 真实 QML 绑定读到一致状态（"QML 侧可见"）----------

TEST_F(UiControlAdapterIntegrationTest, QmlBindingReflectsRealServiceSnapshot) {
    svc_->tick();
    UiControlAdapter adapter(svc_.get());
    adapter.refresh();

    QQmlEngine engine;
    engine.rootContext()->setContextProperty("adapter", &adapter);
    QQmlComponent comp(&engine);
    comp.setData(
        "import QtQml\n"
        "QtObject {\n"
        "    property bool conn: adapter.connected\n"
        "    property bool estop: adapter.emergencyStop\n"
        "    property bool gl: adapter.globallyLocked\n"
        "    property var ax: adapter.axisFor(\"A\",\"X1\")\n"
        "    property var gn: adapter.gantry(0)\n"
        "}\n",
        QUrl());
    ASSERT_EQ(comp.status(), QQmlComponent::Ready);

    QScopedPointer<QObject> root(comp.create());
    ASSERT_NE(root.data(), nullptr);
    QCoreApplication::processEvents();  // 让绑定求值/刷新

    EXPECT_TRUE(QQmlProperty(root.data(), "conn").read().toBool());
    EXPECT_FALSE(QQmlProperty(root.data(), "estop").read().toBool());
    EXPECT_FALSE(QQmlProperty(root.data(), "gl").read().toBool());

    const auto ax = QQmlProperty(root.data(), "ax").read().toMap();
    EXPECT_DOUBLE_EQ(ax["absPosition"].toDouble(), 12.5);
    EXPECT_EQ(ax["motionStateName"].toString(), "MotorIdle(2)");

    const auto gn = QQmlProperty(root.data(), "gn").read().toMap();
    EXPECT_EQ(gn["stateName"].toString(), "已联动");
    EXPECT_TRUE(gn["logicalControlAllowed"].toBool());
}

// ---------- 测试 4：nullptr 构造的安全默认离线/锁定态 ----------

TEST(UiControlAdapterNullTest, NullptrAdapterIsSafeOfflineDefault) {
    ensureApp();
    UiControlAdapter adapter(nullptr);
    adapter.refresh();
    EXPECT_FALSE(adapter.connected());
    EXPECT_TRUE(adapter.globallyLocked());
    EXPECT_FALSE(adapter.safetyTrusted());
    EXPECT_TRUE(adapter.axisFor("A", "Y").isEmpty());  // 无快照 → 空 map
    EXPECT_EQ(adapter.operations().size(), 0);
}

}  // namespace
