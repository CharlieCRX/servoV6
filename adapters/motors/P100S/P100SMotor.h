#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"
#include "IRegisterAccessor.h"  // 通信协议抽象接口

class P100SMotor : public IMotor {
public:
    P100SMotor() = default;
    P100SMotor(IRegisterAccessor* accessor, int motorID);
    ~P100SMotor() override = default;

    // 可随时绑定/切换协议与电机编号
    void bind(IRegisterAccessor* accessor, int motorID);

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
    IRegisterAccessor* reg_ = nullptr;
    int motorID_ = 0;

    // P100S 专属寄存器编号
    enum HoldingReg {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
    };

    enum InputReg {
        CurrentPositionPulse = 0x1018,
    };

    // 寄存器类型
    static constexpr int REG_TYPE_INPUT = 1;
    static constexpr int REG_TYPE_HOLDING = 2;

    static constexpr int pulsesPerRevolution = 5000;  // 1圈对应脉冲数
};

#endif // P100S_MOTOR_H
