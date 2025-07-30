// main.cpp
#include <QCoreApplication>
#include <QTest>
#include "Logger.h"
#include "test_serialcommprotocol.h" // 确保这里是 .h 文件
//#include "test_p100smotor.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Logger::getInstance().init("test.log", "debug", false); // 不向控制台输出日志
    SerialCommProtocolTest test;
//    P100SMotorTest test;
    return QTest::qExec(&test, argc, argv);
}
