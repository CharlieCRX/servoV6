#include "P100SMotor.h"
#include "Logger.h"

P100SMotor::P100SMotor(int motorID, MotorRegisterAccessor* regAccessor)
    : motorID_(motorID), regAccessor_(regAccessor)
{
    if (regAccessor_ == nullptr) {
        LOG_CRITICAL("P100S电机的寄存器指针为空，程序终止！");
    }
    LOG_INFO("创建P100S电机实例，ID: {}", motorID_);
}

bool P100SMotor::enable()
{
    LOG_INFO("伺服使能");
    return writeUInt16(PA53_ServoEnable, 1);
}

bool P100SMotor::disable()
{
    LOG_INFO("伺服使能断开");
    return writeUInt16(PA53_ServoEnable, 0);
}

bool P100SMotor::isEnabled()
{
    uint16_t motorEnableState = 0;
    readUInt16(PA53_ServoEnable, motorEnableState);
    return motorEnableState == 1;
}
P100SMotor::~P100SMotor() = default;

// ------------------- 点动控制 -------------------

bool P100SMotor::setJogRPM(int rpm) {
    if (!checkRPMRange(rpm)) {
        LOG_WARN("点动转速设置失败，超出范围: {}", rpm);
        return false;
    }

    if (!writeUInt32(PA21_JogRPM, static_cast<uint32_t>(rpm))) {
        LOG_ERROR("写入点动速度寄存器失败");
        return false;
    }

    jogRPM_ = rpm;
    LOG_INFO("设置点动转速为 {}", rpm);
    return true;
}

int P100SMotor::getJogRPM() const {
    return jogRPM_;
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


// ------------------- 位置移动控制 -------------------

bool P100SMotor::setMoveRPM(int rpm) {
    if (!checkRPMRange(rpm)) {
        LOG_WARN("位置移动转速设置失败，超出范围: {}", rpm);
        return false;
    }

    if (!writeUInt32(P4_4_MoveRPM, static_cast<uint32_t>(rpm))) {
        LOG_ERROR("写入位置移动速度寄存器失败");
        return false;
    }

    moveRPM_ = rpm;
    LOG_INFO("设置位置移动转速为 {}", rpm);
    return true;
}

int P100SMotor::getMoveRPM() const {
    return moveRPM_;
}


bool P100SMotor::setAbsoluteTargetRevolutions(double rev) {
    this->targetRevolutions_ = rev;
    LOG_INFO("设置电机 {} 的绝对目标圈数为 {}。", motorID_, rev);
    return true;
}

bool P100SMotor::setRelativeTargetRevolutions(double deltaRev) {
    // 相对移动 = 当前位置 + 相对位移
    double currentRev = getCurrentRevolutions();
    if (currentRev == 0.0 && !isMoveDone()) {
        LOG_WARN("电机正在运动或当前位置未知，无法设置相对目标。");
        return false;
    }
    this->targetRevolutions_ = currentRev + deltaRev;
    LOG_INFO("设置电机 {} 的相对目标圈数 (增量: {}) 为绝对位置 {}。", deltaRev, this->targetRevolutions_, motorID_);
    return true;
}

bool P100SMotor::triggerMove() {
    // 步骤 0: 检查并使能电机
    if (!isEnabled()) {
        LOG_INFO("电机[{}]未使能，正在进行使能...", motorID_);
        if (!enable()) {
            LOG_ERROR("电机[{}]使能失败，无法执行移动命令", motorID_);
            return false;
        }
    }

    uint16_t revolutions_value;
    uint16_t pulses_value;

    // 步骤 1: 准备移动参数
    if (!prepareMoveParameters(targetRevolutions_, revolutions_value, pulses_value)) {
        LOG_ERROR("电机[{}]准备移动参数失败", motorID_);
        return false;
    }

    // 步骤 2: 发送移动命令
    if (!sendMoveCommand(revolutions_value, pulses_value)) {
        LOG_ERROR("电机[{}]发送移动命令失败", motorID_);
        return false;
    }

    // 等待1秒钟，确保驱动器处理完命令
    wait(1000);

    // 步骤 3: 等待并处理移动结果
    // 假设给一个合理的超时时间，例如 10000ms
    if (!waitMoveDone(10000)) {
        LOG_ERROR("电机[{}]移动操作未在指定时间内完成或超时", motorID_);
        return false;
    }

    LOG_INFO("电机[{}]成功位置移动到目标位置", motorID_);

    // 步骤 4: 移动完成后关闭伺服使能
    LOG_INFO("电机[{}]移动完成，准备关闭伺服使能...", motorID_);
    if (disable()) {
        LOG_INFO("电机[{}]成功关闭伺服使能", motorID_);
    } else {
        LOG_WARN("电机[{}]尝试关闭伺服使能失败，请检查设备状态", motorID_);
    }

    return true;
}


// ------------------- 状态监测 -------------------
// todo() 这里的超时时间，可以通过计算 ( 位置 / 速度 ) 来获取
bool P100SMotor::waitMoveDone(int timeoutMs)
{
    int elapsed = 0;
    const int pollIntervalMs = 10;
    while (elapsed < timeoutMs) {
        if (isMoveDone()) {
            LOG_INFO("电机 {} 位置移动完成。", motorID_);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        elapsed += pollIntervalMs;
    }
    LOG_WARN("电机 {} 位置移动超时 ({}ms)。", motorID_, timeoutMs);
    return false;
}

bool P100SMotor::isMoveDone() const
{
    uint16_t doStatusWord;
    if (!readUInt16(DO_STATUS_WORD, doStatusWord)) {
        LOG_ERROR("电机[{}]读取DO状态字(地址0x1010)失败", motorID_);
        return false;
    }

    // COIN (定位完成) 标志对应 DO_STATUS_WORD 的 Bit5
    return (doStatusWord & (1 << 0)) != 0;
}

bool P100SMotor::isInPosition() const
{
    return isMoveDone();
}


// ------------------- 位置操作 -------------------

bool P100SMotor::goHome()
{
    LOG_INFO("回原点");
    return false;
}

void P100SMotor::wait(int ms)
{
    LOG_INFO("等待 {} 毫秒", ms);
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
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

    // 计算实际圈数 (考虑有符号位置值)
    double revolutions = static_cast<double>(static_cast<int64_t>(pulses)) / MOTOR_PULSES_PER_REVOLUTION;
    LOG_INFO("电机[{}]当前旋转圈数: {:.5f} 转", motorID_, revolutions);

    return revolutions;
}

// 辅助函数：将浮点数圈数分解为P4-2和P4-3
bool P100SMotor::prepareMoveParameters(double targetRev, uint16_t& outRevolutions, uint16_t& outPulses) const {

    // 分解为整数部分和小数部分
    double revolutions_double;
    double fractional_part = modf(targetRev, &revolutions_double);

    // 处理负数情况
    // modf(-1.5, &revolutions_double) 会返回 -0.5 和 -1.0
    // 所以这里的逻辑是正确的
    int32_t revolutions_signed = static_cast<int32_t>(revolutions_double);
    int32_t pulses_signed = static_cast<int32_t>(fractional_part * COMMAND_PULSE_RESOLUTION);

    // 将有符号数转换为无符号数，由底层通信协议处理
    outRevolutions = static_cast<uint32_t>(revolutions_signed);
    outPulses = static_cast<uint32_t>(pulses_signed);

    // 计算电机绝对位置脉冲
    int64_t targetMotorAbsolutePositionPulses = targetRev * MOTOR_PULSES_PER_REVOLUTION;

    LOG_INFO("准备移动参数：目标圈数={}, 分解为 指令脉冲圈数={}，单圈指令脉冲数={}，最终电机的绝对位置脉冲数={}(±100)",
             targetRev, revolutions_signed, pulses_signed, targetMotorAbsolutePositionPulses);

    return true; // 总是返回true，因为参数本身是有效的
}

bool P100SMotor::sendMoveCommand(uint32_t revolutions, uint16_t pulses) {
    // 将无符号数转换为有符号数进行日志输出，以更清晰地显示负值
    int32_t revolutions_signed = static_cast<int32_t>(revolutions);
    int32_t pulses_signed = static_cast<int32_t>(pulses);

    LOG_INFO("发送移动命令：圈数={}, 脉冲={}", revolutions_signed, pulses_signed);

    // TO2.1：写入目标圈数（P4-2）
    if (!writeUInt16(P4_2_SetMultiTurns, revolutions)) {
        LOG_ERROR("电机{}写入 目标圈数（P4-2） 寄存器失败", motorID_);
        return false;
    }

    // TO2.2：写入圈内脉冲（P4-3）
    if (!writeUInt16(P4_3_SetInnerPulse, pulses)) {
        LOG_ERROR("电机{}写入 圈内脉冲（P4-3） 寄存器失败", motorID_);
        return false;
    }

    // TO2.3：发送触发信号（CTRG上升沿）
    if (!writeUInt16(P3_31_VirtualInput, 0x0000)) {
        LOG_ERROR("电机{}清零 CTRG 触发信号失败", motorID_);
        return false;
    }
    if (!writeUInt16(P3_31_VirtualInput, 0x0001)) {
        LOG_ERROR("电机{}置位 CTRG 触发信号失败", motorID_);
        return false;
    }

    return true;
}


// helper function

bool P100SMotor::readUInt16(uint32_t regAddr, uint16_t& outVal) const {
    return regAccessor_->readReg16(motorID_, regAddr, outVal);
}

bool P100SMotor::writeUInt32(uint32_t regAddr, uint32_t value) {
    return regAccessor_->writeReg32(motorID_, regAddr, value);
}

bool P100SMotor::writeUInt16(uint32_t regAddr, uint16_t value) {
    return regAccessor_->writeReg16(motorID_, regAddr, value);
}

bool P100SMotor::readUInt64(uint32_t regAddr, uint64_t& outVal) const {
    return regAccessor_->readReg64(motorID_, regAddr, outVal);
}
