#include "test_p100smotor.h"

#include <QTest>
#include <QDebug>

#include "MotorRegisterAccessor.h"
#include "SerialCommProtocol.h"
#include "P100S/P100SMotor.h"

void P100SMotorTest::positionAndReset_shouldWork()
{
    // ✅ 初始化串口协议
    SerialCommProtocol protocol;
    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

    // ✅ 创建寄存器访问器和电机对象
    MotorRegisterAccessor accessor(&protocol);
    const int motorID = 5;
    P100SMotor motor(motorID, &accessor);

    // ✅ 获取当前圈数
    double revBeforeReset = motor.getCurrentRevolutions();
    qDebug() << "归零前圈数:" << revBeforeReset;

    // ✅ 设置当前位置为 0
    QVERIFY2(motor.setCurrentPositionAsZero(), "设置当前位置为零失败");

    // ✅ 再次读取圈数
    double revAfterReset = motor.getCurrentRevolutions();
    qDebug() << "归零后圈数:" << revAfterReset;

    // ✅ 断言圈数接近 0（允许浮点误差）
    QVERIFY2(qAbs(revAfterReset) < 0.01, "归零后圈数不为 0");

    protocol.close(); // 主动关闭串口
}
