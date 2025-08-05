// test_gear_rotary_adapter.cpp
#include "test_gear_rotary_adapter.h"
#include "GearRotaryAdapter.h"

void GearRotaryAdapterTest::testAbsoluteAngularMove()
{
    // 准备
    MockMotor mockMotor;
    double gearDiameter = 100.0;
    double reductionRatio = 10.0;
    GearRotaryAdapter adapter(&mockMotor, gearDiameter, reductionRatio);

    double targetDegrees = 360.0;
    double expectedRevolutions = (targetDegrees / 360.0) * reductionRatio;

    // 执行
    bool result = adapter.absoluteAngularMove(targetDegrees);

    // 断言
    QVERIFY(result);
    QCOMPARE(mockMotor.receivedAbsoluteRevolutions, expectedRevolutions);
    QVERIFY(mockMotor.triggerMoveCalled);
}
