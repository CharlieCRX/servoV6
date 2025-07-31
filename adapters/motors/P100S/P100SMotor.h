#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"
#include "MotorRegisterAccessor.h"

class P100SMotor : public IMotor {
public:
    explicit P100SMotor(int motorID, MotorRegisterAccessor* regAccessor);
    ~P100SMotor() override;

    bool setJogRPM(int rpm) override;
    int getJogRPM() const override;
    bool setMoveRPM(int rpm) override;
    int getMoveRPM() const override;

    bool relativeMoveRevolutions(double revolutions) override;
    bool absoluteMoveRevolutions(double targetRevolutions) override;
    bool startPositiveRPMJog() override;
    bool startNegativeRPMJog() override;
    bool stopRPMJog() override;
    bool goHome() override;
    void wait(int ms) override;
    bool setCurrentPositionAsZero() override;
    double getCurrentRevolutions() const override;

    int motorID() const { return motorID_; }

private:
    const int motorID_;
    MotorRegisterAccessor* regAccessor_;

    int jogRPM_ = 0;    // 点动转速，0-6000
    int moveRPM_ = 0;   // 位置移动转速，0-6000

    enum P100SRegisterAddr : uint32_t {
        P4_2_SetMultiTurns = 0x202,
        P4_3_SetInnerPulse = 0x203,
        P3_34_ResetEncoderMultiTurns = 0x122,
        CurrentPositionPulse = 0x1018,
        PA95_EncoderResolutionExp = 95,

        P4_4_MoveRPM = 0x204,  // 位置移动速度 0-6000 r/min
        PA21_JogRPM = 21       // 点动速度 0-6000 r/min
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
