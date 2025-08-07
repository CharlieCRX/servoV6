#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths> // For QStandardPaths::writableLocation
#include <Logger.h>  // For your custom Logger
#include <spdlog/spdlog.h> // For the full spdlog definition
#include "RelayIO/RelayController.h"

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

    // --- LOGGING SYSTEM INITIALIZATION (moved up for early logging) ---
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir dir;
    if (!dir.mkpath(logDir)) {
        qWarning() << "Failed to create log directory:" << logDir;
        logDir = QDir::currentPath(); // Fallback to current path if creation fails
    }
    QString logFilePath = logDir + "/application.log";
    Logger::getInstance().init(logFilePath.toStdString(), "debug");
    // --- 在这里添加一个启动分隔符日志 ---
    // 打印一个包含当前时间的分隔符，用于区分每次应用启动的日志
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
