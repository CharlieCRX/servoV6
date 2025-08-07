#include <QCoreApplication>
#include <QTest>
#include "Logger.h"

#include "test_motorregisteraccessor.h"
// 其他测试可选包含
#include "test_serialcommprotocol.h"
#include "test_p100smotor.h"
#include "test_gear_rotary_adapter.h"
#include "RelayIOModuleTest.h"

#define RUN_MOTOR_REGISTER_ACCESSOR_TEST 0
#define RUN_SERIAL_COMM_PROTOCOL_TEST    0
#define RUN_P100S_MOTOR_TEST             0
#define RUN_GEAR_ROTARY_ADAPTER_TEST     0
#define RUN_RELAY_IO_MODULE_TEST         1

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Logger::getInstance().init("test.log", "debug", false);

    int result = 0;

#if RUN_MOTOR_REGISTER_ACCESSOR_TEST
    {
        MotorRegisterAccessorTest test;
        result = QTest::qExec(&test, argc, argv);
    }
#elif RUN_SERIAL_COMM_PROTOCOL_TEST
    {
        SerialCommProtocolTest test;
        result = QTest::qExec(&test, argc, argv);
    }
#elif RUN_P100S_MOTOR_TEST
    {
        P100SMotorTest test;
        result = QTest::qExec(&test, argc, argv);
    }
#elif RUN_GEAR_ROTARY_ADAPTER_TEST
    {
        GearRotaryAdapterTest test;
        result = QTest::qExec(&test, argc, argv);
    }
#elif RUN_RELAY_IO_MODULE_TEST
    {
        RelayIOModuleTest test;
        result = QTest::qExec(&test, argc, argv);
    }
#else
    qWarning("未选择任何测试用例运行！");
    result = -1;
#endif

    spdlog::drop_all();
    return result;
}
