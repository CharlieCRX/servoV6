#include <QCoreApplication>
#include <QTest>
#include "Logger.h"

#include "test_serialcommprotocol.h"
#include "test_p100smotor.h"

// 定义想要运行的测试用例
#define RUN_SERIAL_COMM_PROTOCOL_TEST  0
#define RUN_P100S_MOTOR_TEST           1

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    Logger::getInstance().init("test.log", "debug", false); // 初始化日志系统（默认不输出到控制台）

    int result = 0;  // 保存测试执行结果

#if RUN_SERIAL_COMM_PROTOCOL_TEST
    {
        SerialCommProtocolTest serialTest;
        result = QTest::qExec(&serialTest, argc, argv);
    }
#elif RUN_P100S_MOTOR_TEST
    {
        P100SMotorTest motorTest;
        result = QTest::qExec(&motorTest, argc, argv);
    }
#else
    qWarning("未选择任何测试用例运行！");
    result = -1;
#endif

    // ✅ 主动销毁所有 spdlog logger，避免程序退出时崩溃
    spdlog::drop_all();;

    return result;
}
