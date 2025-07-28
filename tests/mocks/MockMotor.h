// tests/mocks/MockMotor.h
#ifndef MOCK_MOTOR_H
#define MOCK_MOTOR_H

#include <gmock/gmock.h>
#include "IMotor.h" // 包含真实的 IMotor 接口

class MockMotor : public IMotor {
public:
    // 使用 MOCK_METHOD 定义虚函数的 Mock
    MOCK_METHOD(bool, SetPositionSpeed, (double mmPerSecond), (override));
    MOCK_METHOD(bool, setJogSpeed, (double mmPerSecond), (override));
    MOCK_METHOD(bool, relativeMove, (double mm), (override));
    MOCK_METHOD(void, wait, (int ms), (override));
    MOCK_METHOD(bool, goHome, (), (override));
    MOCK_METHOD(bool, absoluteMove, (double targetMm), (override));
    MOCK_METHOD(bool, startPositiveJog, (), (override));
    MOCK_METHOD(bool, startNegativeJog, (), (override));
    MOCK_METHOD(bool, stopJog, (), (override));
};

#endif // MOCK_MOTOR_H
