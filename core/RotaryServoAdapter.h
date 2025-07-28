#ifndef ROTARY_SERVO_ADAPTER_H
#define ROTARY_SERVO_ADAPTER_H

#include "IServoAdapter.h" // 包含接口基类
#include "IMotor.h"        // 适配器需要操作 IMotor
#include <memory>          // 用于 std::unique_ptr

class RotaryServoAdapter : public IRotaryServoAdapter {
public:
    // **确保这个构造函数被声明，且参数类型正确**
    RotaryServoAdapter(std::unique_ptr<IMotor> motor, double degreesPerRevolution);
    ~RotaryServoAdapter() override = default;

    // **确保所有 IServoAdapter 和 IRotaryServoAdapter 的方法都被声明**
    IMotor* getMotor() override;
    bool goHome() override;
    void wait(int ms) override;
    bool stopJog() override;

    bool setAngularPositionSpeed(double degreesPerSec) override;
    bool setAngularJogSpeed(double degreesPerSec) override;
    bool angularMove(double degrees) override;
    bool startPositiveAngularJog() override;
    bool startNegativeAngularJog() override;

private:
    std::unique_ptr<IMotor> motor_;
    double degreesPerRevolution_;
    double currentAngularPositionSpeedRPM_;
    double currentAngularJogSpeedRPM_;
};

#endif // ROTARY_SERVO_ADAPTER_H
