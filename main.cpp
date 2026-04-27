#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>

// 引入你所有的架构头文件
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
#include "infrastructure/logger/Logger.h"

int main(int argc, char *argv[])
{
    // ==========================================
    // 0. 初始化全局可观测性基础设施 (Logger)
    // ==========================================
    LoggerConfig logCfg;
    logCfg.enableConsole = true;              // 开发阶段打开控制台
    logCfg.enableFile = false;                 // 开启落盘
    logCfg.logDirectory = "D:/servoV6_logs";  // 你可以改成任意你想要的绝对或相对路径
    
    Logger::init(logCfg);

    LOG_INFO(LogLayer::APP, "System", "========================================");
    LOG_INFO(LogLayer::APP, "System", "servoV6 Application Starting...");
    LOG_INFO(LogLayer::APP, "System", "========================================");

    QQuickStyle::setStyle("Basic");
    QGuiApplication app(argc, argv);

    // ==========================================
    // 1. 实例化底层物理与业务逻辑 (Composition Root)
    // ==========================================
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    Axis axis;
    EnableUseCase enableUc(driver);
    JogAxisUseCase jogUc(driver);
    MoveAbsoluteUseCase moveAbsUc(driver);
    MoveRelativeUseCase moveRelUc(driver);
    StopAxisUseCase stopUc(driver);

    JogOrchestrator jogOrch(enableUc, jogUc);
    AutoAbsMoveOrchestrator absOrch(enableUc, moveAbsUc);
    AutoRelMoveOrchestrator relOrch{enableUc, moveRelUc};

    // 实例化 Core ViewModel
    AxisViewModelCore vmCore(axis, jogOrch, absOrch, relOrch, stopUc);
    
    // 实例化 Qt Wrapper ViewModel
    QtAxisViewModel qtVM(&vmCore);

    // 初始化物理世界状态
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedJogVelocity(20.0);
    plc.setSimulatedMoveVelocity(50.0);
    plc.setLimits(1000.0, -1000.0);
    syncService.sync(axis, plc.getFeedback());

    // ==========================================
    // 2. QML 引擎初始化与依赖注入
    // ==========================================
    QQmlApplicationEngine engine;
    
    // ⭐ 核心依赖注入：将 C++ 的 qtVM 注入到 QML 上下文中，命名为 "axisX1VM"
    // Phase 7 多轴时，你可以在这里再注入一个 axisX2VM
    engine.rootContext()->setContextProperty("axisX1VM", &qtVM);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    
    // 加载移动到新目录的 Main.qml
    engine.loadFromModule("servoV6", "Main");

    // ==========================================
    // 3. 启动系统物理时钟 (Tick Loop)
    // ==========================================
    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        qtVM.tick();                               // 1. 驱动 UI 与状态机
        plc.tick(10);                              // 2. 推进物理世界 10ms
        syncService.sync(axis, plc.getFeedback()); // 3. 传感器回流
    });
    systemClock.start(10); // 10ms 物理心跳

    int result = app.exec();  

    Logger::shutdown(); // 确保日志系统安全关闭，写完最后的日志

    return result;
}