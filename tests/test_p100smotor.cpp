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


void P100SMotorTest::setAndGetJogRPM_shouldWork()
{
    // 初始化串口协议
    SerialCommProtocol protocol;
    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

    // 创建寄存器访问器和电机对象
    MotorRegisterAccessor accessor(&protocol);
    const int motorID = 5;
    P100SMotor motor(motorID, &accessor);

    // 设置点动转速
    const int testJogRPM = 3000;
    QVERIFY2(motor.setJogRPM(testJogRPM), "设置点动转速失败");

    // 获取点动转速，断言正确
    int readJogRPM = motor.getJogRPM();
    qDebug() << "设置后点动转速:" << readJogRPM;
    QCOMPARE(readJogRPM, testJogRPM);

    protocol.close(); // 主动关闭串口
}

void P100SMotorTest::setAndGetMoveRPM_shouldWork()
{
    // 初始化串口协议
    SerialCommProtocol protocol;
    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

    // 创建寄存器访问器和电机对象
    MotorRegisterAccessor accessor(&protocol);
    const int motorID = 5;
    P100SMotor motor(motorID, &accessor);

    // 设置位置移动转速
    const int testMoveRPM = 150;
    QVERIFY2(motor.setMoveRPM(testMoveRPM), "设置位置移动转速失败");

    // 获取位置移动转速，断言正确
    int readMoveRPM = motor.getMoveRPM();
    qDebug() << "设置后位置移动转速:" << readMoveRPM;
    QCOMPARE(readMoveRPM, testMoveRPM);

    protocol.close(); // 主动关闭串口
}

void P100SMotorTest::setRPM_outOfRange_shouldFail()
{
    SerialCommProtocol protocol;
    QVERIFY2(protocol.open("COM10", true), "串口打开失败");

    MotorRegisterAccessor accessor(&protocol);
    const int motorID = 5;
    P100SMotor motor(motorID, &accessor);

    QVERIFY(!motor.setJogRPM(-1));
    QVERIFY(!motor.setJogRPM(7000));
    QVERIFY(!motor.setMoveRPM(-10));
    QVERIFY(!motor.setMoveRPM(7000));

    protocol.close();
}
