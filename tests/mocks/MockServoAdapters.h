// tests/mocks/MockServoAdapters.h (新的适配器 Mock，包含线性、旋转)
#ifndef MOCK_SERVO_ADAPTERS_H
#define MOCK_SERVO_ADAPTERS_H

#include <gmock/gmock.h>
#include "IServoAdapter.h" // 包含适配器接口

class MockLinearServoAdapter : public ILinearServoAdapter {
public:
    MOCK_METHOD(bool, goHome, (), (override));
    MOCK_METHOD(void, wait, (int ms), (override));
    MOCK_METHOD(bool, stopJog, (), (override));
    MOCK_METHOD(IMotor*, getMotor, (), (override)); // Mock for getMotor

    MOCK_METHOD(bool, setPositionSpeed, (double mmPerSec), (override));
    MOCK_METHOD(bool, setJogSpeed, (double mmPerSec), (override));
    MOCK_METHOD(bool, relativeMove, (double mm), (override));
    MOCK_METHOD(bool, absoluteMove, (double targetMm), (override));
    MOCK_METHOD(bool, startPositiveJog, (), (override));
    MOCK_METHOD(bool, startNegativeJog, (), (override));
};

class MockRotaryServoAdapter : public IRotaryServoAdapter {
public:
    MOCK_METHOD(bool, goHome, (), (override));
    MOCK_METHOD(void, wait, (int ms), (override));
    MOCK_METHOD(bool, stopJog, (), (override));
    MOCK_METHOD(IMotor*, getMotor, (), (override)); // Mock for getMotor

    MOCK_METHOD(bool, setAngularPositionSpeed, (double degreesPerSec), (override));
    MOCK_METHOD(bool, setAngularJogSpeed, (double degreesPerSec), (override));
    MOCK_METHOD(bool, angularMove, (double degrees), (override));
    MOCK_METHOD(bool, startPositiveAngularJog, (), (override));
    MOCK_METHOD(bool, startNegativeAngularJog, (), (override));
};

#endif // MOCK_SERVO_ADAPTERS_H
