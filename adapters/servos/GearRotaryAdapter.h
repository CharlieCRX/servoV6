#ifndef GEAR_ROTARY_ADAPTER_H
#define GEAR_ROTARY_ADAPTER_H

#include "IServoAdapter.h"
#include "IMotor.h"

class GearRotaryAdapter : public IRotaryServoAdapter {
public:
    // 构造函数
    GearRotaryAdapter(IMotor* motor, double gearDiameter, double reductionRatio);
    ~GearRotaryAdapter() override = default;

    // 仅实现测试所需的绝对旋转方法，其他方法提供最小空实现
    bool absoluteAngularMove(double targetDegrees) override;

    // --- 其他 IRotaryServoAdapter 纯虚函数（最小实现） ---
    bool setAngularPositionSpeed(double degreesPerSec) override { return true; }
    bool setAngularJogSpeed(double degreesPerSec) override { return true; }
    bool relativeAngularMove(double degrees) override { return true; }
    bool startPositiveAngularJog() override { return true; }
    bool startNegativeAngularJog() override { return true; }
    double getCurrentAngleDegrees() const override { return 0.0; }

    // --- IServoAdapter 纯虚函数（最小实现） ---
    bool goHome() override { return true; }
    void wait(int ms) override {}
    bool stopJog() override { return true; }
    bool setCurrentPositionAsZero() override { return true; }
    bool emergencyStop() override { return true; }
    bool initEnvironment() override { return true; }
    IMotor* getMotor() override { return m_motor; }

private:
    IMotor* m_motor;
    double m_reductionRatio;
    // 如果需要，可以在这里存储 gearDiameter
    // double m_gearDiameter;
};

#endif // GEAR_ROTARY_ADAPTER_H
