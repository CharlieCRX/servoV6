#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h" // 包含 IMotor 接口
#include "ICommProtocol.h"

// P100S 电机型号的具体实现
class P100SMotor : public IMotor {
public:
    P100SMotor();
    P100SMotor(ICommProtocol* comm, int motorID);
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
    bool setCurrentPositionAsZero() override;
    double getCurrentRevolutions() const override;

private:
    ICommProtocol* comm_ = nullptr;
    int motorID_ = 0;

    // P100S 私有寄存器地址
    enum HoldingReg {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
    };

    enum InputReg {
        CurrentPositionPulse = 0x1018,
    };
};

#endif // P100S_MOTOR_H
