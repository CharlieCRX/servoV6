// core/Motor.h (假设这是你的真实电机实现)
#ifndef MOTOR_H
#define MOTOR_H

#include "IMotor.h"
// 可能需要引入实际的硬件驱动库
// #include "HardwareDriver.h"

class Motor : public IMotor {
public:
    Motor(int id); // 示例：构造函数接收一个ID
    ~Motor() override = default;

    bool setRPM(double rpm) override;
    bool relativeMoveRevolutions(double revolutions) override;
    bool absoluteMoveRevolutions(double targetRevolutions) override;
    bool startPositiveRPMJog() override;
    bool startNegativeRPMJog() override;
    bool stopRPMJog() override;
    bool goHome() override;
    void wait(int ms) override;

private:
    int motorId;
    double currentRPM; // 内部状态
    double currentRevolutions; // 内部状态
    // HardwareDriver* driver; // 真实场景可能需要驱动器实例
};

#endif // MOTOR_H
