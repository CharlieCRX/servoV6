#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"
#include "MotorRegisterAccessor.h"

class P100SMotor : public IMotor {
public:
    explicit P100SMotor(int motorID, MotorRegisterAccessor* regAccessor);
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

    // 获取电机ID
    int motorID() const { return motorID_; }

private:
    const int motorID_;  // 电机唯一ID
    MotorRegisterAccessor* regAccessor_; // 寄存器访问器

    // P100S 专属寄存器编号
    enum P100SRegisterAddr : uint32_t {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
        CurrentPositionPulse = 0x1018,
        PA95_EncoderResolutionExp = 95,  // 编码器分辨率的指数部分
    };

    // 辅助函数
    bool writeUInt32(uint32_t regAddr, uint32_t value);
    bool writeUInt16(uint32_t regAddr, uint16_t value);
    bool readUInt64(uint32_t regAddr, uint64_t& outVal) const;
    bool readUInt16(uint32_t regAddr, uint16_t& outVal) const;
};

#endif // P100S_MOTOR_H
