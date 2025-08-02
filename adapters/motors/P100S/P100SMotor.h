#ifndef P100S_MOTOR_H
#define P100S_MOTOR_H

#include "IMotor.h"
#include "MotorRegisterAccessor.h"

// 电机及电子齿轮比配置宏定义
#define PA95_ENCODER_RESOLUTION_EXP 17
#define PA12_GEAR_NUMERATOR 8192
#define PA13_GEAR_DENOMINATOR 675
// 编码器每圈脉冲数 (2^17)
#define MOTOR_PULSES_PER_REVOLUTION (1 << PA95_ENCODER_RESOLUTION_EXP)
// 指令脉冲分辨率 (131072 * 675 / 8192)
#define COMMAND_PULSE_RESOLUTION (MOTOR_PULSES_PER_REVOLUTION * PA13_GEAR_DENOMINATOR / PA12_GEAR_NUMERATOR)

class P100SMotor : public IMotor {
public:
    explicit P100SMotor(int motorID, MotorRegisterAccessor* regAccessor);
    ~P100SMotor() override;
    // 使能
    bool enable() override;
    bool disable() override;
    bool isEnabled() override;

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

public: // 测试完毕后应改为 private ！
    const int motorID_;
    MotorRegisterAccessor* regAccessor_;

    int jogRPM_ = 0;
    int moveRPM_ = 0;
    double targetRevolutions_ = 0.0;

    enum P100SRegisterAddr : uint32_t {
        // 位置控制参数
        P4_4_MoveRPM = 0x204,                  // 内部位置速度 (P4-4)
        P4_2_SetMultiTurns = 0x202,            // 目标圈数 (P4-2)
        P4_3_SetInnerPulse = 0x203,            // 圈内脉冲 (P4-3)

        // 点动控制参数
        PA21_JogRPM = 21,                      // 点动转速 (PA-21)

        // 位置操作与状态
        P3_34_ResetEncoderMultiTurns = 0x122,  // 复位编码器多圈 (P3-34)
        P3_31_VirtualInput = 0x11F,            // 虚拟输入端口 (P3-31)，用于触发移动
        CurrentPositionPulse = 0x1018,         // 当前位置脉冲 (0x1018)
        DO_STATUS_WORD = 0x1010,               // 状态字 (0x1010)，Bit0 = COIN (定位完成)

        // 电机配置参数
        PA95_EncoderResolutionExp = 95,        // 编码器分辨率指数 (PA-95)

        // 使能控制
        PA53_ServoEnable = 53,                 // 伺服使能 (PA-53)，写入1通电并准备执行
    };

    bool writeUInt32(uint32_t regAddr, uint32_t value);
    bool writeUInt16(uint32_t regAddr, uint16_t value);
    bool readUInt64(uint32_t regAddr, uint64_t& outVal) const;
    bool readUInt16(uint32_t regAddr, uint16_t& outVal) const;

    bool checkRPMRange(int rpm) const {
        return rpm >= 0 && rpm <= 6000;
    }

    // TO1.2: 将浮点数圈数分解为P4-2和P4-3
    bool prepareMoveParameters(double targetRev, uint16_t& outRevolutions, uint16_t& outPulses) const;
    // TO2: 将移动参数写入寄存器并发送触发信号
    bool sendMoveCommand(uint32_t revolutions, uint16_t pulses);
};

#endif // P100S_MOTOR_H
