#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths> // For QStandardPaths::writableLocation
#include <Logger.h>  // For your custom Logger
#include <spdlog/spdlog.h> // For the full spdlog definition
#include "RelayIO/RelayController.h"


// 可选：根据配置来选择使用哪种协议和QML界面
// 设为 1 使用蓝牙协议和 Bluetooth.qml
// 设为 0 使用串口协议和 Main.qml
#define USE_BLUETOOTH_PROTOCOL 1
int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // --- Declare QQmlApplicationEngine ONCE ---
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    // --- 日志系统初始化 ---
    QString logDir;

    // 使用预处理宏，根据平台设置日志目录
#ifdef Q_OS_ANDROID
    logDir = logDir = "/storage/emulated/0/Documents/servoV6";

#else
    // 对于非Android平台（如 Windows, Linux, macOS），使用应用本地数据目录
    logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/servoV6";
#endif
    QDir dir(logDir);
    if (!dir.mkpath(".")) {
        qWarning() << "Failed to create log directory:" << logDir;
        // 如果创建失败，回退到应用私有目录以确保日志功能可用
        logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/log";
        dir.mkpath(logDir);
    }

    QString logFilePath = logDir + "/application.log";
    Logger::getInstance().init(logFilePath.toStdString(), "debug");
    LOG_INFO("------------------------------------------------------------");
    LOG_INFO("ServoV6 Application Started: {}", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString());
    LOG_INFO("------------------------------------------------------------");

    LOG_INFO("Application started."); // Now you can log from the very beginning

    // 创建 RelayController 实例
    RelayController relayController;

    // 绑定 RelayController 到 QML，QML里用对象名 "relayCtrl" 访问
    engine.rootContext()->setContextProperty("relayCtrl", &relayController);

    // 加载 QML
    engine.loadFromModule("servoV6", "Main");
    LOG_INFO("QML engine loaded.");

    int exitCode = app.exec();

    LOG_INFO("Application exiting with code: {}", exitCode);
    return exitCode;
}
