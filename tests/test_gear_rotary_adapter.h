#ifndef TEST_GEAR_ROTARY_ADAPTER_H
#define TEST_GEAR_ROTARY_ADAPTER_H

#include <QObject>
#include <QtTest>
#include "IMotor.h"

// 完整实现 IMotor 接口的 Mock 类
class MockMotor : public IMotor {
public:
    MockMotor()
        : receivedRevolutions(0.0),
        triggerMoveCalled(false),
        receivedAbsoluteRevolutions(0.0) {}

    // 实现所有纯虚函数，为不关心的函数提供默认或空实现
    bool enable() override { return true; }
    bool disable() override { return true; }
    bool isEnabled() override { return true; }
    bool setJogRPM(int rpm) override { return true; }
    int getJogRPM() const override { return 0; }
    bool startPositiveRPMJog() override { return true; }
    bool startNegativeRPMJog() override { return true; }
    bool stopRPMJog() override { return true; }
    bool setMoveRPM(int rpm) override { return true; }
    int getMoveRPM() const override { return 0; }

    bool setRelativeTargetRevolutions(double rev) override { return true; }
    bool setAbsoluteTargetRevolutions(double rev) override {
        receivedAbsoluteRevolutions = rev;
        return true;
    }

    bool triggerMove() override {
        triggerMoveCalled = true;
        return true;
    }

    bool waitMoveDone(int timeoutMs = 3000) override { return true; }
    bool isMoveDone() const override { return true; }
    bool isInPosition() const override { return true; }
    bool setCurrentPositionAsZero() override { return true; }
    double getCurrentRevolutions() const override { return 0; }
    bool initEnvironment() override { return true; }
    bool goHome() override { return true; }
    void wait(int ms) override {}
    void emergencyStop() override {}

    // 用于验证的公有成员变量
    double receivedAbsoluteRevolutions;
    double receivedRevolutions; // For relative move test
    bool triggerMoveCalled;
};

class GearRotaryAdapterTest : public QObject
{
    Q_OBJECT

private slots:
    void testAbsoluteAngularMove();
};

#endif // TEST_GEAR_ROTARY_ADAPTER_H
