#ifndef LINEAR_SERVO_ADAPTER_H
#define LINEAR_SERVO_ADAPTER_H

#include "IServoAdapter.h" // 包含接口基类
#include "IMotor.h"        // 适配器需要操作 IMotor
#include <memory>          // 用于 std::unique_ptr

class LinearServoAdapter : public ILinearServoAdapter {
public:
    // **确保这个构造函数被声明，且参数类型正确**
    LinearServoAdapter(std::unique_ptr<IMotor> motor, double leadScrewPitchMmPerRev);
    ~LinearServoAdapter() override = default;

    // **确保所有 IServoAdapter 和 ILinearServoAdapter 的方法都被声明**
    IMotor* getMotor() override;
    bool goHome() override;
    void wait(int ms) override;
    bool stopJog() override;

    bool setPositionSpeed(double mmPerSec) override;
    bool setJogSpeed(double mmPerSec) override;
    bool relativeMove(double mm) override;
    bool absoluteMove(double targetMm) override;
    bool startPositiveJog() override;
    bool startNegativeJog() override;

private:
    std::unique_ptr<IMotor> motor_;
    double leadScrewPitchMmPerRev_;
    double currentPositionSpeedRPM_;
    double currentJogSpeedRPM_;
};

#endif // LINEAR_SERVO_ADAPTER_H
