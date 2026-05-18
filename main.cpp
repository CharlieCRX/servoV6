#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>
#include <QStandardPaths>
#include <vector>

#include "application/SystemManager.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/QtAxisViewModel.h"
#include "presentation/viewmodel/EmergencyStopViewModel.h"
#include "infrastructure/logger/Logger.h"
#include <sstream>
#include <iomanip>

// 辅助：将单个轴的摘要格式化为紧凑字符串
// 输出如 "Y: pos=+0041.4 Standstill"
static std::string formatAxisSummary(QtAxisViewModel& vm)
{
    std::string full = vm.fullName().toStdString();
    // 从 "Machine_A/Y" 提取轴短名 "Y"
    auto slash = full.rfind('/');
    std::string shortName = (slash != std::string::npos) ? full.substr(slash + 1) : full;

    std::ostringstream oss;
    oss << shortName << ": pos=" << std::fixed << std::setprecision(1) << std::showpos
        << vm.position() << std::noshowpos
        << " " << vm.stateText().toStdString();
    if (vm.errorCount() > 0)
        oss << " errs=" << vm.errorCount();
    return oss.str();
}

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // ============================
    // 0. 初始化全局可观测性基础设施 (Logger)
    // ============================
    LoggerConfig logCfg;
    logCfg.enableConsole = true;
    logCfg.enableFile = true;

    QString logBasePath;
#ifdef Q_OS_ANDROID
    logBasePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (logBasePath.isEmpty()) {
        logBasePath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    }
#else
    logBasePath = QCoreApplication::applicationDirPath();
#endif

    logCfg.logDirectory = QString("%1/logs").arg(logBasePath).toStdString();
    Logger::init(logCfg);

    LOG_INFO(LogLayer::APP, "System", "========================================");
    LOG_INFO(LogLayer::APP, "System", "servoV6 Application Starting...");
    LOG_INFO(LogLayer::APP, "System", "Log Directory: " + logCfg.logDirectory);
    LOG_INFO(LogLayer::APP, "System", "========================================");

    QQuickStyle::setStyle("Basic");

    // ============================
    // 1. 硬件仿真层（每个分组独立的 FakePLC + FakeAxisDriver）
    // ============================
    FakePLC plcA, plcB;
    FakeAxisDriver driverA(plcA), driverB(plcB);

    // ============================
    // 2. 系统分组管理
    // ============================
    SystemManager manager;
    ContextRejection reason;

    manager.createGroup("Machine_A", reason);   // Y, Z, R 轴
    manager.createGroup("Machine_B", reason);   // X1, X2 轴（龙门）

    SystemContext* ctxA = nullptr;
    SystemContext* ctxB = nullptr;
    manager.tryGetGroup("Machine_A", ctxA, reason);
    manager.tryGetGroup("Machine_B", ctxB, reason);
    ctxA->setDriver(&driverA);
    ctxB->setDriver(&driverB);

    // ============================
    // 3. 初始化物理世界默认状态
    // ============================
    // --- Group A (Machine_A): 6 轴初始状态 ---
    constexpr double DEFAULT_JOG_VEL  = 20.0;
    constexpr double DEFAULT_MOVE_VEL = 50.0;
    constexpr double DEFAULT_LIMIT_POS = 1000.0;
    constexpr double DEFAULT_LIMIT_NEG = -1000.0;
    constexpr std::array<AxisId, 6> ALL_AXES = {
        AxisId::X, AxisId::X1, AxisId::X2, AxisId::Y, AxisId::Z, AxisId::R
    };
    for (auto id : ALL_AXES) {
        plcA.forceState(id, AxisState::Disabled);
        plcA.setSimulatedJogVelocity(id, DEFAULT_JOG_VEL);
        plcA.setSimulatedMoveVelocity(id, DEFAULT_MOVE_VEL);
        plcA.setLimits(id, DEFAULT_LIMIT_POS, DEFAULT_LIMIT_NEG);
    }
    for (auto id : ALL_AXES) {
        plcB.forceState(id, AxisState::Disabled);
        plcB.setSimulatedJogVelocity(id, DEFAULT_JOG_VEL);
        plcB.setSimulatedMoveVelocity(id, DEFAULT_MOVE_VEL);
        plcB.setLimits(id, DEFAULT_LIMIT_POS, DEFAULT_LIMIT_NEG);
    }

    // 首次同步（将 plc 默认状态注入 SystemContext）
    driverA.pollFeedback(*ctxA);
    driverB.pollFeedback(*ctxB);

    // ============================
    // 4. ViewModels（按 分组+轴 维度，两组各含6轴）
    // ============================
    // Machine_A 的全部轴
    auto vmCore_A_Y  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Y);
    auto vmCore_A_Z  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Z);
    auto vmCore_A_R  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::R);
    auto vmCore_A_X  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X);
    auto vmCore_A_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X1);
    auto vmCore_A_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::X2);

    // Machine_B 的全部轴
    auto vmCore_B_Y  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::Y);
    auto vmCore_B_Z  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::Z);
    auto vmCore_B_R  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::R);
    auto vmCore_B_X  = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X);
    auto vmCore_B_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X1);
    auto vmCore_B_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X2);

    // Qt 包装
    QtAxisViewModel qtVM_A_Y(vmCore_A_Y.get());
    QtAxisViewModel qtVM_A_Z(vmCore_A_Z.get());
    QtAxisViewModel qtVM_A_R(vmCore_A_R.get());
    QtAxisViewModel qtVM_A_X(vmCore_A_X.get());
    QtAxisViewModel qtVM_A_X1(vmCore_A_X1.get());
    QtAxisViewModel qtVM_A_X2(vmCore_A_X2.get());

    QtAxisViewModel qtVM_B_Y(vmCore_B_Y.get());
    QtAxisViewModel qtVM_B_Z(vmCore_B_Z.get());
    QtAxisViewModel qtVM_B_R(vmCore_B_R.get());
    QtAxisViewModel qtVM_B_X(vmCore_B_X.get());
    QtAxisViewModel qtVM_B_X1(vmCore_B_X1.get());
    QtAxisViewModel qtVM_B_X2(vmCore_B_X2.get());

    // ─────────────── 4b. 急停安全 ViewModel ───────────────
    // 每个分组一个 EmergencyStopViewModel，在 tick loop 中读取紧急急停状态
    EmergencyStopViewModel emergencyVM_A(manager, "Machine_A");
    EmergencyStopViewModel emergencyVM_B(manager, "Machine_B");

    // ============================
    // 5. QML 引擎初始化与依赖注入
    // ============================
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("group_A_Y",  &qtVM_A_Y);
    engine.rootContext()->setContextProperty("group_A_Z",  &qtVM_A_Z);
    engine.rootContext()->setContextProperty("group_A_R",  &qtVM_A_R);
    engine.rootContext()->setContextProperty("group_A_X",  &qtVM_A_X);
    engine.rootContext()->setContextProperty("group_A_X1", &qtVM_A_X1);
    engine.rootContext()->setContextProperty("group_A_X2", &qtVM_A_X2);

    engine.rootContext()->setContextProperty("group_B_Y",  &qtVM_B_Y);
    engine.rootContext()->setContextProperty("group_B_Z",  &qtVM_B_Z);
    engine.rootContext()->setContextProperty("group_B_R",  &qtVM_B_R);
    engine.rootContext()->setContextProperty("group_B_X",  &qtVM_B_X);
    engine.rootContext()->setContextProperty("group_B_X1", &qtVM_B_X1);
    engine.rootContext()->setContextProperty("group_B_X2", &qtVM_B_X2);

    // 急停安全 ViewModel
    engine.rootContext()->setContextProperty("emergencyVM_A", &emergencyVM_A);
    engine.rootContext()->setContextProperty("emergencyVM_B", &emergencyVM_B);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("servoV6", "Main");

    // ============================
    // 6. 全局 Tick Loop（统一 pollFeedback）
    // ============================
    std::vector<QtAxisViewModel*> allViewModels = {
        &qtVM_A_Y, &qtVM_A_Z, &qtVM_A_R, &qtVM_A_X, &qtVM_A_X1, &qtVM_A_X2,
        &qtVM_B_Y, &qtVM_B_Z, &qtVM_B_R, &qtVM_B_X, &qtVM_B_X1, &qtVM_B_X2
    };

    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        // 6a. 所有分组推进物理引擎 + 反馈注入
        for (const auto& groupName : manager.groupNames()) {
            SystemContext* ctx = nullptr;
            ContextRejection r;
            if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
                auto* drv = ctx->driver();
                if (!drv) continue;

                // 6a-1. 反馈注入（轴 + 龙门 + 急停）
                drv->pollFeedback(*ctx);

                // 6a-2. 消费 EmergencyStopController 产生的 pending command
                auto& estopCtrl = ctx->emergencyStopController();
                if (estopCtrl.hasPendingCommand()) {
                    auto commResult = drv->send(estopCtrl.popPendingCommand());
                    if (!commResult.ok()) {
                        LOG_WARN(LogLayer::APP, "System",
                            "[" + groupName + "] EmergencyStop command delivery failed: " + commResult.diagnostic);
                    }
                }
            }
        }

        // 6b. 所有 ViewModel 推进状态机
        for (auto* vm : allViewModels) {
            vm->tick();
        }

        // 6c. 急停安全 ViewModel 推进（每帧同步急停控制器状态）
        emergencyVM_A.tick();
        emergencyVM_B.tick();
    });
    systemClock.start(10);  // 10ms 物理心跳

    // 7. 周期性状态摘要（每秒输出一次，按分组分行）
    QTimer summaryClock;
    QObject::connect(&summaryClock, &QTimer::timeout, [&]() {
        LOG_SUMMARY(LogLayer::UI, "Telemetry",
            "=== Machine_A === "
            + formatAxisSummary(qtVM_A_Y) + "  "
            + formatAxisSummary(qtVM_A_Z) + "  "
            + formatAxisSummary(qtVM_A_R) + "  "
            + formatAxisSummary(qtVM_A_X) + "  "
            + formatAxisSummary(qtVM_A_X1) + "  "
            + formatAxisSummary(qtVM_A_X2));
        LOG_SUMMARY(LogLayer::UI, "Telemetry",
            "=== Machine_B === "
            + formatAxisSummary(qtVM_B_Y) + "  "
            + formatAxisSummary(qtVM_B_Z) + "  "
            + formatAxisSummary(qtVM_B_R) + "  "
            + formatAxisSummary(qtVM_B_X) + "  "
            + formatAxisSummary(qtVM_B_X1) + "  "
            + formatAxisSummary(qtVM_B_X2));
    });
    summaryClock.start(1000);  // 1s 周期

    int result = app.exec();

    Logger::shutdown();
    return result;
}
