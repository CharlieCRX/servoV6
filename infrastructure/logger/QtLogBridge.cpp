#include "infrastructure/logger/QtLogBridge.h"

#include <QByteArray>
#include <QMessageLogContext>
#include <QString>
#include <QtGlobal>

#include <sstream>
#include <string>
#include <cstdlib>

#include "infrastructure/logger/Logger.h"

namespace logger {
namespace {

QtMessageHandler previousHandler = nullptr;

LogLevel qtLevel(QtMsgType type) {
    switch (type) {
        case QtDebugMsg:    return LogLevel::DEBUG;
        case QtInfoMsg:     return LogLevel::INFO;
        case QtWarningMsg:  return LogLevel::WARN;
        case QtCriticalMsg: return LogLevel::ERROR;
        case QtFatalMsg:    return LogLevel::ERROR;
    }
    return LogLevel::INFO;
}

std::string qtMessageText(const QMessageLogContext& context, const QString& msg) {
    std::ostringstream oss;
    oss << msg.toStdString();
    if (context.file && context.line > 0) {
        oss << " (" << context.file << ":" << context.line;
        if (context.function) oss << " " << context.function;
        oss << ")";
    }
    return oss.str();
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& msg) {
    if (!Logger::isInitialized()) {
        if (previousHandler) previousHandler(type, context, msg);
        return;
    }

    const LogContext logContext{"UI", "Qt", "qt"};
    Logger::logWithContext(qtLevel(type), LogLayer::UI, "Qt",
                           logContext, qtMessageText(context, msg));

    if (type == QtFatalMsg) {
        std::abort();
    }
}

}  // namespace

void installQtMessageHandler() {
    previousHandler = qInstallMessageHandler(handler);
}

void uninstallQtMessageHandler() {
    qInstallMessageHandler(previousHandler);
    previousHandler = nullptr;
}

}  // namespace logger
