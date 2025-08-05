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
#define DEFAULT_BRAKE_DELAY_MS 50

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

    void emergencyStop() override;
    bool initEnvironment() override;

    // ID
    int motorID() const { return motorID_; }

public: // 测试完毕后应改为 private ！
    const int motorID_;
    MotorRegisterAccessor* regAccessor_;

    int jogRPM_ = 0;
    int moveRPM_ = 0;
    double targetRevolutions_ = 0.0;

    enum P100SRegisterAddr : uint32_t {
        // 环境配置参数
        PA4_MoveMode = 4,                      // 位置移动模式 (PA-4)
        PA11_CmdPulseRev = 11,                 // 指令脉冲数/圈 (PA-11)
        PA12_EleGearNum = 12,                  // 电子齿轮分子 (PA-12)
        PA13_EleGearDen = 13,                  // 电子齿轮分母 (PA-13)
        PA14_PosCommandMode = 14,              // 位置指令脉冲方式 (PA-14)
        P3_30_VirtInputMode = 0x11E,           // 虚拟输入模式 (P3-30)
        P3_36_SingleTurnZero = 0x124,          // 单圈位置零点 (P3-36)
        P4_0_PositionMode = 0x200,             // 内部位置指令控制模式 (P4-0)
        PA62_EncoderType = 62,                 // 编码器类型 (PA-62)
        PA95_EncoderResolutionExp = 95,        // 编码器分辨率指数 (PA-95)

        // 位置控制参数
        P4_4_MoveRPM = 0x204,                  // 内部位置速度 (P4-4)
        P4_2_SetMultiTurns = 0x202,            // 目标圈数 (P4-2)
        P4_3_SetInnerPulse = 0x203,            // 圈内脉冲 (P4-3)

        // 点动控制参数
        PA21_JogRPM = 21,                      // 点动转速 (PA-21)
        PA22_JogSpeedSource = 22,              // 速度来源 (PA-22)

        // 加减速参数
        PA40_AccTime = 40,                     // 加速时间常数（0→1000rpm）
        PA41_DecTime = 41,                     // 减速时间常数（1000-0rpm）
        PA42_ScurveTime = 42,                  // S 型加减速曲线平滑时间

        // 抱闸/刹车参数
        PA47_BrakeDelay = 47,                  // 抱闸后延迟断电 (PA-47)
        PA48_BrakeRunSet = 48,                 // 电机运转时机械制动器动作设定 (PA-48) 定义从电流切断到制动器动作的延时
        PA49_MinBrakeSpeed = 49,               // 抱闸时电机最小转速 (PA-49)
        PA94_BrakeDelay = 94,                  // 电磁制动器打开的延时 (PA-94)

        // DI 功能码配置 (P3-38 ~ P3-45)
        // 功能码定义了虚拟DI的功能，例如启动、停止、模式切换等。
        P3_38_DI1Func = 0x126,                 // DI1功能码：28=CTRG，内部位置命令触发。
        P3_39_DI2Func = 0x127,                 // DI2功能码：16=CMODE，复合模式控制设定。用于切换位置/速度模式。
        P3_40_DI3Func = 0x128,                 // DI3功能码：22=JOGP，正向点动。
        P3_41_DI4Func = 0x129,                 // DI4功能码：23=JOGN，反向点动。
        P3_42_DI5Func = 0x12A,                 // DI5功能码：27=HOLD，内部位置控制命令停止。

        // 数字输出(DO)配置
        P3_20_DO1Func = 0x114,                 // DO1功能码：5=COIN，定位完成信号。
        P3_21_DO2Func = 0x115,                 // DO2功能码：8=BRK，电磁制动器输出控制。
        P3_22_DO3Func = 0x116,                 // DO3功能码：0=无效。
        P3_23_DO4Func = 0x117,                 // DO4功能码：3=ALM，报警输出。

        // 伺服状态和控制
        P3_34_ResetEncoderMultiTurns = 0x122,  // 复位编码器多圈 (P3-34)
        P3_31_VirtualInput = 0x11F,            // 虚拟输入端口 (P3-31)，用于触发移动
        PA53_ServoEnable = 53,                 // 伺服使能 (PA-53)
        PA60_ResetServo = 60,                  // 软复位伺服 (PA-60)

        // 状态监控 (读取)
        S0_CurrentSpeed = 0x1000,              // 当前转速 (S0)
        DO_STATUS_WORD = 0x1010,               // DO状态字 (S16)
        CurrentPositionPulse = 0x1018,         // 当前位置脉冲 (S24-27)
        PA75_ZeroSpeedDet = 75,                // 零速检测点 (PA-75)

        PA16_PosRange = 16,                    // 定位完成脉冲范围 (PA-16)
        PA17_PosTolerance = 17,                // 位置超差报警检测范围 (PA-17)
        PA18_PosToleranceEn = 18,              // 位置超差报警检测开关 (PA-18)
    };

    // 虚拟输入端子状态值 P3-31寄存器需要用到的值
    enum VirtualInputBits : uint16_t {
        CLEAR_ALL_BITS = 0x0000,             // 上升沿有效，所以初始化为全零状态
        TRIGGER_MOVE   = 0x0001,             // bit0（DI1）: CTRG. 用于触发位置模式移动，与点动无关。此时bit1 = 0，为位置移动模式（CMODE OFF）
        JOG_MODE_BIT   = 0x0002,             // bit1（DI2）: CMODE.点动模式（CMODE ON）
        JOG_POS_BIT    = 0x0004,             // bit2（DI3）: JOGP. 正向点动
        JOG_NEG_BIT    = 0x0008,             // bit3（DI4）: JOGN. 负向点动
        MOVE_HOLD_BIT  = 0x0010,             // bit4（DI5）: HOLD. 内部位置控制命令停止
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

    bool softReset();
};

#endif // P100S_MOTOR_H
