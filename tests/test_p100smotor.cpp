#include <QtTest>
#include "test_p100smotor.h"
#include "P100S/P100SMotor.h"
#include "SerialCommProtocol.h"

void P100SMotorTest::setZero_shouldResetRevolutionsToZero()
{
    SerialCommProtocol protocol;
    QVERIFY(protocol.open("COM10")); // 替换为实际串口

    P100SMotor motor(&protocol, 5); // motor ID 5

    // Step 1: 获取归零前圈数
    double revBefore = motor.getCurrentRevolutions();

    // Step 2: 设置当前位置为零
    QVERIFY(motor.setCurrentPositionAsZero());

    // Step 3: 获取归零后圈数
    double revAfter = motor.getCurrentRevolutions();

    // Step 4: 验证变化，确保归零后值接近 0
    QVERIFY(qAbs(revAfter) < 0.01);

    // （可选）验证归零前后差值 ≈ 原来的 revBefore
    QVERIFY(qAbs(revBefore - revAfter) > 0.01);
}
