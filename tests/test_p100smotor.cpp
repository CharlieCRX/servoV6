#include <QtTest>
#include "test_p100smotor.h"
#include "SerialCommProtocol.h"
#include "P100S/P100SMotor.h"


void P100SMotorTest::positionAndReset_shouldWork() {
    SerialCommProtocol protocol;
    bool ok = protocol.open("COM10");
    QVERIFY2(ok, "打开串口COM10失败");

    P100SMotor motor;
    motor.bind(&protocol, 5); // 绑定设备ID为5

    // 1. 读取当前位置圈数
    double posBefore = motor.getCurrentRevolutions();
    qInfo() << "当前位置（归零前）:" << posBefore;

    // 2. 执行归零
    QVERIFY(motor.setCurrentPositionAsZero());

    // 3. 读取归零后位置
    double posAfter = motor.getCurrentRevolutions();
    qInfo() << "当前位置（归零后）:" << posAfter;

    // 4. 判断归零后位置是否在±0.01圈内
    QVERIFY2(std::abs(posAfter) < 0.01, "归零后位置未接近0");

    // 5. 可选：再读一次确认稳定
    double posAfter2 = motor.getCurrentRevolutions();
    QVERIFY2(std::abs(posAfter2) < 0.01, "二次读取位置未接近0");
}
