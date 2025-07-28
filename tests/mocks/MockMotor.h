// tests/mocks/MockMotor.h (新的底层 MockMotor)
#ifndef MOCK_MOTOR_H
#define MOCK_MOTOR_H

#include <gmock/gmock.h>
#include "IMotor.h"

class MockMotor : public IMotor {
public:
    MOCK_METHOD(bool, setRPM, (double rpm), (override));
    MOCK_METHOD(bool, relativeMoveRevolutions, (double revolutions), (override));
    MOCK_METHOD(bool, absoluteMoveRevolutions, (double targetRevolutions), (override));
    MOCK_METHOD(bool, startPositiveRPMJog, (), (override));
    MOCK_METHOD(bool, startNegativeRPMJog, (), (override));
    MOCK_METHOD(bool, stopRPMJog, (), (override));
    MOCK_METHOD(bool, goHome, (), (override));
    MOCK_METHOD(void, wait, (int ms), (override));
};

#endif // MOCK_MOTOR_H
