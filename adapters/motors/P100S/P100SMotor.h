#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"

class P100SMotor : public IMotor {
public:
    P100SMotor() = default;
    ~P100SMotor() override = default;

    // 实现 IMotor 接口
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
    int motorID_ = 0;

    // P100S 专属寄存器编号
    enum P100SRegisterAddr {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
        CurrentPositionPulse = 0x1018,
        PA95_EncoderResolutionExp = 95,  // 编码器分辨率的指数部分，存放在寄存器95
    };

};

#endif // P100S_MOTOR_H
