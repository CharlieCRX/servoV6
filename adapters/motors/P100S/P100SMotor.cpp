#include "P100SMotor.h"
#include "Logger.h" // 改为包含新的 Logger.h

P100SMotor::P100SMotor(IRegisterAccessor *accessor, int motorID)
{
    bind(accessor, motorID);
}

void P100SMotor::bind(IRegisterAccessor *accessor, int motorID)
{
    reg_ = accessor;
    motorID_ = motorID;
}

bool P100SMotor::setRPM(double rpm) {
    LOG_INFO("Setting RPM to {}.", rpm);
    return true;
}

bool P100SMotor::relativeMoveRevolutions(double revolutions) {
    LOG_INFO("Relative move by {} revolutions.", revolutions);
    return true;
}

bool P100SMotor::absoluteMoveRevolutions(double targetRevolutions) {
    LOG_INFO("Absolute move to {} revolutions.", targetRevolutions);
    return true;
}

bool P100SMotor::startPositiveRPMJog() {
    LOG_INFO("Starting positive RPM jog.");
    return true;
}

bool P100SMotor::startNegativeRPMJog() {
    LOG_INFO("Starting negative RPM jog.");
    return true;
}

bool P100SMotor::stopRPMJog() {
    LOG_INFO("Stopping RPM jog.");
    return true;
}

bool P100SMotor::goHome() {
    LOG_INFO("Executing GoHome command.");
    return true;
}

void P100SMotor::wait(int ms) {
    LOG_INFO("Waiting for {} ms.", ms);
}

bool P100SMotor::setCurrentPositionAsZero() {
    if (!reg_) {
        LOG_ERROR("未绑定寄存器访问器，无法归零当前位置");
        return false;
    }

    bool ok = true;

    // 设置圈数为 0
    ok &= reg_->writeUInt32(motorID_, RegisterType::HOLDING_REGISTER, P4_2_SetMultiTurns, 0);

    // 设置圈内脉冲为 0
    ok &= reg_->writeUInt32(motorID_, RegisterType::HOLDING_REGISTER, P4_3_SetInnerPulse, 0);

    // 触发编码器多圈归零
    ok &= reg_->writeUInt16(motorID_, RegisterType::HOLDING_REGISTER, P3_34_ResetEncoderMultiTurns, 1);

    if (ok) {
        LOG_INFO("当前位置归零完成");
    } else {
        LOG_ERROR("当前位置归零失败，请检查通信状态");
    }
    return ok;
}


double P100SMotor::getCurrentRevolutions() const {
    if (!reg_) {
        LOG_INFO("未绑定寄存器访问器，无法获取当前位置");
        return 0.0;
    }

    uint64_t pulses = 0;
    if (!reg_->readUInt64(motorID_, RegisterType::INPUT_REGISTER, CurrentPositionPulse, pulses)) {
        LOG_INFO("读取当前位置脉冲（地址 0x1018）失败");
        return 0.0;
    }

    uint32_t pulsesPerRevolution = 0; // 电机分辨率
    uint16_t resolutionExp = 0;       // 分辨率的指数部分
    if (!reg_->readUInt16(motorID_, RegisterType::INPUT_REGISTER, PA95_EncoderResolutionExp, resolutionExp)) {
        LOG_INFO("读取编码器分辨率指数（地址 PA95）失败");
        return 0.0;
    }

    pulsesPerRevolution = 1 << resolutionExp;
    LOG_INFO("电机编码器分辨率为{}", pulsesPerRevolution);

    double revolutions = static_cast<double>((int64_t)pulses) / pulsesPerRevolution;
    LOG_INFO("电机的旋转圈数为{}", revolutions);

    return revolutions;
}
