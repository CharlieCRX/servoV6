#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"
#include "MotorRegisterAccessor.h"

class P100SMotor : public IMotor {
public:
    explicit P100SMotor(int motorID, MotorRegisterAccessor* regAccessor);
    ~P100SMotor() override;

    // 点动控制
    bool setJogRPM(int rpm) override;
    int getJogRPM() const override;
    bool startPositiveRPMJog() override;
    bool startNegativeRPMJog() override;
    bool stopRPMJog() override;

    // 位置移动控制
    bool setMoveRPM(int rpm) override;
    int getMoveRPM() const override;
    bool setAbsoluteTargetRevolutions(double rev) override;
    bool setRelativeTargetRevolutions(double deltaRev) override;
    bool triggerMove() override;

    // 状态监测
    bool waitMoveDone(int timeoutMs = 3000) override;
    bool isMoveDone() const override;
    bool isInPosition() const override;

    // 位置操作
    bool goHome() override;
    void wait(int ms) override;
    bool setCurrentPositionAsZero() override;
    double getCurrentRevolutions() const override;

    // ID
    int motorID() const { return motorID_; }

private:
    const int motorID_;
    MotorRegisterAccessor* regAccessor_;

    int jogRPM_ = 0;
    int moveRPM_ = 0;
    double targetRevolutions_ = 0.0;

    enum P100SRegisterAddr : uint32_t {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
        CurrentPositionPulse = 0x1018,
        PA95_EncoderResolutionExp = 95,
        P4_4_MoveRPM = 0x204,
        PA21_JogRPM = 21,
        DO_STATUS_WORD = 0x1010 // Bit0=CMDOK, Bit5=COIN
    };

    bool writeUInt32(uint32_t regAddr, uint32_t value);
    bool writeUInt16(uint32_t regAddr, uint16_t value);
    bool readUInt64(uint32_t regAddr, uint64_t& outVal) const;
    bool readUInt16(uint32_t regAddr, uint16_t& outVal) const;

    bool checkRPMRange(int rpm) const {
        return rpm >= 0 && rpm <= 6000;
    }
};

#endif // P100S_MOTOR_H
