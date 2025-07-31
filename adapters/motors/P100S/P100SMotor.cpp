#include "P100SMotor.h"
#include "Logger.h"

P100SMotor::P100SMotor(int motorID, MotorRegisterAccessor* regAccessor)
    : motorID_(motorID), regAccessor_(regAccessor)
{
    if (regAccessor_ == nullptr) {
        LOG_CRITICAL("P100S电机的寄存器指针为空，程序终止！");
        std::terminate(); // 或 exit(EXIT_FAILURE);
    }
    LOG_INFO("创建P100S电机实例，ID: {}", motorID_);
}
P100SMotor::~P100SMotor() = default;


bool P100SMotor::setRPM(double rpm)
{
    LOG_INFO("设置转速RPM: {}", rpm);
    return false;
}

bool P100SMotor::relativeMoveRevolutions(double revolutions)
{
    LOG_INFO("执行相对移动，圈数: {}", revolutions);
    return false;
}

bool P100SMotor::absoluteMoveRevolutions(double targetRevolutions)
{
    LOG_INFO("执行绝对移动，目标圈数: {}", targetRevolutions);
    return false;
}

bool P100SMotor::startPositiveRPMJog()
{
    LOG_INFO("启动正向点动");
    return false;
}

bool P100SMotor::startNegativeRPMJog()
{
    LOG_INFO("启动反向点动");
    return false;
}

bool P100SMotor::stopRPMJog()
{
    LOG_INFO("停止点动");
    return false;
}

bool P100SMotor::goHome()
{
    LOG_INFO("回原点");
    return false;
}

void P100SMotor::wait(int ms)
{
    LOG_INFO("等待 {} 毫秒", ms);
}

bool P100SMotor::readUInt16(uint32_t regAddr, uint16_t& outVal) const {
    return regAccessor_->readReg16(motorID_, regAddr, outVal);
}

bool P100SMotor::setCurrentPositionAsZero() {
    if (!regAccessor_) {
        LOG_ERROR("电机[{}]未绑定寄存器访问器，无法归零当前位置", motorID_);
        return false;
    }

    bool ok = true;

    // 设置圈数为 0
    ok &= writeUInt32(P4_2_SetMultiTurns, 0);

    // 设置圈内脉冲为 0
    ok &= writeUInt32(P4_3_SetInnerPulse, 0);

    // 触发编码器多圈归零
    ok &= writeUInt16(P3_34_ResetEncoderMultiTurns, 1);

    if (ok) {
        LOG_INFO("电机[{}]当前位置归零完成", motorID_);
    } else {
        LOG_ERROR("电机[{}]当前位置归零失败，请检查通信状态", motorID_);
    }
    return ok;
}

double P100SMotor::getCurrentRevolutions() const {
    if (!regAccessor_) {
        LOG_ERROR("电机[{}]未绑定寄存器访问器，无法获取当前位置", motorID_);
        return 0.0;
    }

    uint64_t pulses = 0;
    if (!readUInt64(CurrentPositionPulse, pulses)) {
        LOG_ERROR("电机[{}]读取当前位置脉冲(0x1018)失败", motorID_);
        return 0.0;
    }

    uint16_t resolutionExp = 0; // 分辨率的指数部分
    if (!readUInt16(PA95_EncoderResolutionExp, resolutionExp)) {
        LOG_ERROR("电机[{}]读取编码器分辨率指数(PA95)失败", motorID_);
        return 0.0;
    }

    // 计算每转脉冲数 (2^resolutionExp)
    uint32_t pulsesPerRevolution = 1 << resolutionExp;
    LOG_INFO("电机[{}]编码器分辨率: {} 脉冲/转", motorID_, pulsesPerRevolution);

    // 计算实际圈数 (考虑有符号位置值)
    double revolutions = static_cast<double>(static_cast<int64_t>(pulses)) / pulsesPerRevolution;
    LOG_INFO("电机[{}]当前旋转圈数: {:.3f} 转", motorID_, revolutions);

    return revolutions;
}

// helper function
bool P100SMotor::writeUInt32(uint32_t regAddr, uint32_t value) {
    return regAccessor_->writeReg32(motorID_, regAddr, value);
}

bool P100SMotor::writeUInt16(uint32_t regAddr, uint16_t value) {
    return regAccessor_->writeReg16(motorID_, regAddr, value);
}

bool P100SMotor::readUInt64(uint32_t regAddr, uint64_t& outVal) const {
    return regAccessor_->readReg64(motorID_, regAddr, outVal);
}
