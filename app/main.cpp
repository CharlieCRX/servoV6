// app/main.cpp

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QDir>
#include <QStandardPaths>
#include <Logger.h>
#include <spdlog/spdlog.h>
#include "RelayIO/RelayController.h"
#include "SerialCommProtocol.h"
#include "BluetoothCommProtocol.h"
#include <QDateTime>
#include <QPermission> // <-- 引入 QPermission 头文件
#include <QBluetoothPermission> // <-- 新增: 引入 QBluetoothPermission 头文件
#include <QLocationPermission>  // <-- 新增: 引入 QLocationPermission 头文件

#define USE_BLUETOOTH_PROTOCOL 1

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

#if defined(Q_OS_ANDROID)
    // 创建具体的权限对象实例
    QBluetoothPermission bluetoothPermission;
    QLocationPermission fineLocationPermission;

    // 检查并请求蓝牙权限
    if (qApp->checkPermission(bluetoothPermission) != Qt::PermissionStatus::Granted) {
        // 提供一个回调函数来处理权限请求的结果
        qApp->requestPermission(bluetoothPermission, [](const QPermission& permission) {
            // 可以选择在这里处理权限结果，例如打印日志
            if (permission.status() == Qt::PermissionStatus::Granted) {
                LOG_INFO("已成功获得蓝牙权限。");
            } else {
                LOG_ERROR("蓝牙权限请求被拒绝。");
            }
        });
    }

    // 检查并请求位置权限
    if (qApp->checkPermission(fineLocationPermission) != Qt::PermissionStatus::Granted) {
        // 提供一个回调函数来处理权限请求的结果
        qApp->requestPermission(fineLocationPermission, [](const QPermission& permission) {
            // 可以选择在这里处理权限结果
            if (permission.status() == Qt::PermissionStatus::Granted) {
                LOG_INFO("已成功获得位置权限。");
            } else {
                LOG_ERROR("位置权限请求被拒绝。");
            }
        });
    }
#endif

    // --- 日志系统初始化 ---
    QString logDir;
#ifdef Q_OS_ANDROID
    logDir = "/storage/emulated/0/Documents/servoV6";
#else
    logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/servoV6";
#endif
    QDir dir(logDir);
    if (!dir.mkpath(".")) {
        qWarning() << "Failed to create log directory:" << logDir;
        logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/log";
        dir.mkpath(logDir);
    }
    QString logFilePath = logDir + "/application.log";
    Logger::getInstance().init(logFilePath.toStdString(), "debug");
    LOG_INFO("------------------------------------------------------------");
    LOG_INFO("ServoV6 Application Started: {}", QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss").toStdString());
    LOG_INFO("------------------------------------------------------------");
    LOG_INFO("Application started.");

    // --- 创建通信协议和控制器实例 ---
    ICommProtocol* protocol = nullptr;
    RelayController* relayController = nullptr;

#if USE_BLUETOOTH_PROTOCOL
    LOG_INFO("使用蓝牙协议。");
    protocol = new BluetoothCommProtocol();
    relayController = new RelayController(protocol);
#else
    LOG_INFO("使用串口协议。");
    protocol = new SerialCommProtocol(9600, 8, 1, QSerialPort::NoFlowControl, QSerialPort::NoParity);
    relayController = new RelayController(protocol);
#endif

    // --- QML 相关操作 ---
    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);

    engine.rootContext()->setContextProperty("relayCtrl", relayController);

#if USE_BLUETOOTH_PROTOCOL
    LOG_INFO("加载 Bluetooth.qml 界面。");
    engine.loadFromModule("servoV6", "Bluetooth");
#else
    LOG_INFO("加载 Main.qml 界面。");
    engine.loadFromModule("servoV6", "Main");
#endif
    LOG_INFO("QML engine loaded.");

    int exitCode = app.exec();

    LOG_INFO("Application exiting with code: {}", exitCode);

    // --- 资源清理 ---
    delete relayController;
    delete protocol;

    return exitCode;
}
