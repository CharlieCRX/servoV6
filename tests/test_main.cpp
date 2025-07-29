// main.cpp
#include <QCoreApplication>
#include <QTest>
#include "Logger.h"
#include "test_serialcommprotocol.h" // 确保这里是 .h 文件

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Logger::getInstance().init("test.log", "debug", false); // 不向控制台输出日志
    SerialCommProtocolTest test;
    return QTest::qExec(&test, argc, argv);
}
