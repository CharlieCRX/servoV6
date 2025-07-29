#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h" // 包含 IMotor 接口

// P100S 电机型号的具体实现
class P100SMotor : public IMotor {
public:
    P100SMotor();
    ~P100SMotor() override = default;

    // 实现 IMotor 接口的所有方法，模拟电机操作成功
    bool setRPM(double rpm) override;
    bool relativeMoveRevolutions(double revolutions) override;
    bool absoluteMoveRevolutions(double targetRevolutions) override;
    bool startPositiveRPMJog() override;
    bool startNegativeRPMJog() override;
    bool stopRPMJog() override;
    bool goHome() override;
    void wait(int ms) override;
};

#endif // P100S_MOTOR_H
