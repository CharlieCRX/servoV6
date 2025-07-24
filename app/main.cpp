#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDir>
#include <QStandardPaths> // For QStandardPaths::writableLocation
#include <Logger.h>  // For your custom Logger
#include <spdlog/spdlog.h> // For the full spdlog definition

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

    // --- Choose ONE QML loading method ---
    // Option 1: Load from QML module (usually preferred with qt_add_qml_module in CMake)
    engine.loadFromModule("servoV6", "Main");

    // Option 2: Load from specific QRC URL (if you're not using modules, or for specific cases)
    // const QUrl url(u"qrc:/app/qml/Main.qml"_qs);
    // engine.load(url);

    // If you use loadFromModule, make sure your CMakeLists.txt for 'app' is set up like this:
    /*
    qt_add_qml_module(servoV6_app
        URI servoV6 # This matches "servoV6" in loadFromModule
        VERSION 1.0
        QML_FILES app/qml/Main.qml # List your QML files here
    )
    */

    LOG_INFO("QML engine loaded.");

    int exitCode = app.exec(); // This starts the Qt event loop
    LOG_INFO("Application exiting with code: {}", exitCode);
    return exitCode; // This is the correct return for app.exec()
}
