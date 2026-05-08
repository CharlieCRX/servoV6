#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QVariantMap>

// ── 单轴管线头文件 ──
#include "domain/entity/Axis.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/AxisSyncService.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/JogAxisUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/StopAxisUseCase.h"
#include "application/policy/JogOrchestrator.h"
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/QtAxisViewModel.h"

// ── 龙门管线头文件 ──
#include "domain/entity/GantrySystem.h"
#include "domain/entity/PhysicalAxis.h"
#include "infrastructure/FakeGantryCommandPort.h"
#include "infrastructure/FakeGantryFeedbackPort.h"
#include "infrastructure/FakeGantryEventBus.h"
#include "presentation/viewmodel/GantryViewModelCore.h"
#include "presentation/viewmodel/QtGantryViewModel.h"
#include "presentation/viewmodel/Theme.h"

#include "infrastructure/logger/Logger.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    // ==========================================
    // 0. 初始化全局可观测性基础设施 (Logger)
    // ==========================================
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

    // ==========================================
    // 1. 共享物理层 (FakePLC — 多轴真实寄存器)
    //    FakePLC 内部以 std::unordered_map<AxisId, ...> 管理所有轴:
    //      Y, Z, R   — 独立物理轴
    //      X1, X2    — 龙门双轴 (物理)
    //      X         — 龙门逻辑轴 (GantrySystem 内部使用)
    // ==========================================
    FakePLC plc;

    // ==========================================
    // 2. 多独立单轴管线 (Composition Root)
    //    演示：为 Y/Z/R 三个独立轴分别构建完整管线
    //    每个独立轴有：Axis → Driver → UseCases → Orchestrators → ViewModel
    // ==========================================
    LOG_INFO(LogLayer::APP, "System", "Constructing independent axes (Y, Z, R)...");

    // ── AxisRepository：统一管理所有独立轴的 Axis 实例 ──
    AxisRepository axisRepo;
    axisRepo.registerAxis(AxisId::Y);
    axisRepo.registerAxis(AxisId::Z);
    axisRepo.registerAxis(AxisId::R);

    // ── 轴 Y ──
    FakeAxisDriver driverY(plc);
    Axis& axisY = axisRepo.getAxis(AxisId::Y);
    EnableUseCase enableUcY(axisRepo, driverY);
    JogAxisUseCase jogUcY(axisRepo, driverY);
    MoveAbsoluteUseCase moveAbsUcY(axisRepo, driverY);
    MoveRelativeUseCase moveRelUcY(axisRepo, driverY);
    StopAxisUseCase stopUcY(axisRepo, driverY);
    JogOrchestrator jogOrchY(enableUcY, jogUcY);
    AutoAbsMoveOrchestrator absOrchY(enableUcY, moveAbsUcY);
    AutoRelMoveOrchestrator relOrchY(enableUcY, moveRelUcY);
    AxisViewModelCore axisVmCoreY(AxisId::Y, axisY, jogOrchY, absOrchY, relOrchY, stopUcY);
    QtAxisViewModel qtAxisVM_Y(&axisVmCoreY);

    // ── 轴 Z ──
    FakeAxisDriver driverZ(plc);
    Axis& axisZ = axisRepo.getAxis(AxisId::Z);
    EnableUseCase enableUcZ(axisRepo, driverZ);
    JogAxisUseCase jogUcZ(axisRepo, driverZ);
    MoveAbsoluteUseCase moveAbsUcZ(axisRepo, driverZ);
    MoveRelativeUseCase moveRelUcZ(axisRepo, driverZ);
    StopAxisUseCase stopUcZ(axisRepo, driverZ);
    JogOrchestrator jogOrchZ(enableUcZ, jogUcZ);
    AutoAbsMoveOrchestrator absOrchZ(enableUcZ, moveAbsUcZ);
    AutoRelMoveOrchestrator relOrchZ(enableUcZ, moveRelUcZ);
    AxisViewModelCore axisVmCoreZ(AxisId::Z, axisZ, jogOrchZ, absOrchZ, relOrchZ, stopUcZ);
    QtAxisViewModel qtAxisVM_Z(&axisVmCoreZ);

    // ── 轴 R ──
    FakeAxisDriver driverR(plc);
    Axis& axisR = axisRepo.getAxis(AxisId::R);
    EnableUseCase enableUcR(axisRepo, driverR);
    JogAxisUseCase jogUcR(axisRepo, driverR);
    MoveAbsoluteUseCase moveAbsUcR(axisRepo, driverR);
    MoveRelativeUseCase moveRelUcR(axisRepo, driverR);
    StopAxisUseCase stopUcR(axisRepo, driverR);
    JogOrchestrator jogOrchR(enableUcR, jogUcR);
    AutoAbsMoveOrchestrator absOrchR(enableUcR, moveAbsUcR);
    AutoRelMoveOrchestrator relOrchR(enableUcR, moveRelUcR);
    AxisViewModelCore axisVmCoreR(AxisId::R, axisR, jogOrchR, absOrchR, relOrchR, stopUcR);
    QtAxisViewModel qtAxisVM_R(&axisVmCoreR);

    // ── 轴同步服务 (单轴) ──
    AxisSyncService syncService;

    // ── 初始化单轴物理状态 ──
    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.forceState(AxisId::Z, AxisState::Disabled);
    plc.forceState(AxisId::R, AxisState::Disabled);
    plc.setSimulatedJogVelocity(AxisId::Y, 20.0);
    plc.setSimulatedJogVelocity(AxisId::Z, 20.0);
    plc.setSimulatedJogVelocity(AxisId::R, 20.0);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    plc.setSimulatedMoveVelocity(AxisId::Z, 50.0);
    plc.setSimulatedMoveVelocity(AxisId::R, 50.0);
    plc.setLimits(AxisId::Y, 1000.0, -1000.0);
    plc.setLimits(AxisId::Z, 1000.0, -1000.0);
    plc.setLimits(AxisId::R, 360.0, -360.0);  // R 轴旋转范围
    syncService.sync(axisY, plc.getFeedback(AxisId::Y));
    syncService.sync(axisZ, plc.getFeedback(AxisId::Z));
    syncService.sync(axisR, plc.getFeedback(AxisId::R));

    LOG_INFO(LogLayer::APP, "System", "Independent axes (Y, Z, R) constructed");

    // ==========================================
    // 3. 龙门管线 — 多组 / 双轴龙门支持
    //    每个龙门组由两个物理轴 (X1+X2) 和内部的
    //    一个逻辑轴 (X) 组成聚合根 GantrySystem。
    //
    //    架构简化：
    //      GantrySystem 聚合根内部已封装所有命令处理逻辑
    //      (耦合/分动/Jog/Move/Stop/状态聚合/事件发布)。
    //      ViewModel 只需持有 GantrySystem 引用即可。
    //
    //    扩展性：
    //      构建新的 PhysicalAxis + LogicalAxis + GantrySystem
    //      即可添加新龙门组 (如 Gantry-B: X3+X4)。
    // ==========================================
    LOG_INFO(LogLayer::APP, "System", "Constructing Gantry pipeline (multi-group)...");

    // ── 龙门组 "Gantry-A" (X1 + X2 → X) — 双轴龙门 ──
    PhysicalAxis physX1(AxisId::X1);
    PhysicalAxis physX2(AxisId::X2);
    GantrySystem gantryA(physX1, physX2);

    // 龙门 Infrastructure 端口 (Fake 实现)
    FakeGantryCommandPort gantryCmdPort(plc);
    FakeGantryFeedbackPort gantryFbPort(plc);   // 从 FakePLC 读取 X1/X2 状态
    FakeGantryEventBus gantryEventBus;

    // ViewModel (仅需 GantrySystem&)
    GantryViewModelCore gantryAVmCore(gantryA);
    QtGantryViewModel qtGantryAVM(&gantryAVmCore);

    // 初始化龙门物理轴状态
    plc.forceState(AxisId::X1, AxisState::Disabled);
    plc.forceState(AxisId::X2, AxisState::Disabled);
    plc.setSimulatedJogVelocity(AxisId::X1, 15.0);
    plc.setSimulatedJogVelocity(AxisId::X2, 15.0);
    plc.setSimulatedMoveVelocity(AxisId::X1, 40.0);
    plc.setSimulatedMoveVelocity(AxisId::X2, 40.0);
    plc.setLimits(AxisId::X1, 800.0, -800.0);
    plc.setLimits(AxisId::X2, 800.0, -800.0);
    plc.setPosition(AxisId::X1, 0);
    plc.setPosition(AxisId::X2, 0);


    LOG_INFO(LogLayer::APP, "System", "Gantry-A pipeline constructed (X1+X2→X)");

    // ── 组装龙门 VM 映射表 (支持 QML 动态渲染多组龙门 Tab) ──
    QMap<QString, QObject*> gantryVMMap;
    gantryVMMap["Gantry-A"] = &qtGantryAVM;
    // gantryVMMap["Gantry-B"] = &qtGantryBVM;  // 预留

    QVariantMap gantryVMsVariant;
    for (auto it = gantryVMMap.begin(); it != gantryVMMap.end(); ++it) {
        gantryVMsVariant[it.key()] = QVariant::fromValue(it.value());
    }

    LOG_INFO(LogLayer::APP, "System",
        "Gantry VM map assembled with " + std::to_string(gantryVMMap.size()) + " group(s)");

    // ── 组装独立轴 VM 映射表 (支持 QML 动态渲染独立轴 Tab) ──
    QMap<QString, QObject*> axisVMMap;
    axisVMMap["Y"] = &qtAxisVM_Y;
    axisVMMap["Z"] = &qtAxisVM_Z;
    axisVMMap["R"] = &qtAxisVM_R;

    QVariantMap axisVMsVariant;
    for (auto it = axisVMMap.begin(); it != axisVMMap.end(); ++it) {
        axisVMsVariant[it.key()] = QVariant::fromValue(it.value());
    }

    LOG_INFO(LogLayer::APP, "System",
        "Axis VM map assembled with " + std::to_string(axisVMMap.size()) + " axis(es)");

    // ==========================================
    // 4. QML 引擎初始化与依赖注入
    // ==========================================
    QQmlApplicationEngine engine;

    // 注册 Theme 全局单例到 QML (import servoV6 → Theme)
    qmlRegisterSingletonInstance("servoV6", 1, 0, "Theme", &Theme::instance());

    // 注入独立轴 ViewModel 映射表
    engine.rootContext()->setContextProperty(
        "globalAxisVMs",
        axisVMsVariant
    );

    // 注入龙门 ViewModel 映射表
    engine.rootContext()->setContextProperty(
        "globalGantryVMs",
        gantryVMsVariant
    );

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("servoV6", "Main");

    // ==========================================
    // 5. 系统物理时钟 (Tick Loop) — 10ms 周期
    // ==========================================
    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        // ── 独立轴管线 tick ──
        qtAxisVM_Y.tick();
        qtAxisVM_Z.tick();
        qtAxisVM_R.tick();

        // ── 物理世界推进 (FakePLC 内部遍历所有轴独立更新) ──
        plc.tick(10);

        // ── 单轴传感器回流 ──
        syncService.sync(axisY, plc.getFeedback(AxisId::Y));
        syncService.sync(axisZ, plc.getFeedback(AxisId::Z));
        syncService.sync(axisR, plc.getFeedback(AxisId::R));

        // ── 龙门物理轴反馈同步 (关键: 将 FakePLC 状态回流到 GantrySystem) ──
        {
            auto x1Fb = gantryFbPort.getX1Feedback();
            auto x2Fb = gantryFbPort.getX2Feedback();
            physX1.syncState(x1Fb);
            physX2.syncState(x2Fb);

            // 根据 FakePLC 的 AxisState 映射物理轴运动类型
            auto toAggMotion = [](AxisState s) -> LogicalAxis::AggregatedMotion {
                switch (s) {
                    case AxisState::Jogging:        return LogicalAxis::AggregatedMotion::Jogging;
                    case AxisState::MovingAbsolute: return LogicalAxis::AggregatedMotion::MovingAbsolute;
                    case AxisState::MovingRelative: return LogicalAxis::AggregatedMotion::MovingRelative;
                    default:                        return LogicalAxis::AggregatedMotion::Idle;
                }
            };
            gantryA.setX1Motion(toAggMotion(plc.getFeedback(AxisId::X1).state));
            gantryA.setX2Motion(toAggMotion(plc.getFeedback(AxisId::X2).state));
        }

        // ── 龙门管线 tick (含状态聚合 + 事件日志) ──
        qtGantryAVM.tick();
        // qtGantryBVM.tick();  // 预留

        LOG_TRACE_EVERY_N(100, LogLayer::HAL, "System",
            "Tick: X1.pos=" + std::to_string(physX1.position()) +
            " X2.pos=" + std::to_string(physX2.position()) +
            " Mode=" + std::string(::isCoupled(gantryA.mode()) ? "Coupled" : "Decoupled"));
    });
    systemClock.start(10); // 10ms 物理心跳

    LOG_INFO(LogLayer::APP, "System", "System clock started (10ms tick)");
    LOG_INFO(LogLayer::APP, "System", "Application entering event loop");

    int result = app.exec();

    Logger::shutdown();
    return result;
}
