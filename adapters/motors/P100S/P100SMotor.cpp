#include "P100SMotor.h"
#include "Logger.h"
#include <QElapsedTimer>
#include <QThread>

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
    LOG_INFO("正在使能电机 {} ...", motorID_);

    // 写入 1 到 PA53 寄存器以使能伺服
    if (writeUInt16(PA53_ServoEnable, 1)) {
        LOG_INFO("电机 {} 伺服使能成功。", motorID_);
        return true;
    } else {
        LOG_ERROR("电机 {} 伺服使能失败。", motorID_);
        return false;
    }
}

bool P100SMotor::disable()
{
    LOG_INFO("正在断开电机 {} 的伺服使能...", motorID_);

    // 写入 0 到 PA53 寄存器以断开伺服使能
    if (writeUInt16(PA53_ServoEnable, 0)) {
        LOG_INFO("电机 {} 伺服使能断开成功。", motorID_);
        return true;
    } else {
        LOG_ERROR("电机 {} 伺服使能断开失败。", motorID_);
        return false;
    }
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
    // 步骤 0: 检查并使能电机
    if (!isEnabled()) {
        LOG_INFO("电机[{}]未使能，正在进行使能...", motorID_);
        if (!enable()) {
            LOG_ERROR("电机[{}]使能失败，无法执行移动命令", motorID_);
            return false;
        }
    }

    if (jogRPM_ == 0) {
        LOG_WARN("点动转速为0，请先设置点动速度。");
        return false;
    }

    uint16_t jogConfig = VirtualInputBits::JOG_MODE_BIT | VirtualInputBits::JOG_POS_BIT;
    if (!writeUInt16(P3_31_VirtualInput, jogConfig)) {
        LOG_ERROR("写入点动配置 [0x{:04X}] 到 P3-31 失败，无法启动正向点动。", jogConfig);
        return false;
    }

    LOG_INFO("已启动电机 {} 的正向点动，转速：{} RPM", motorID_, jogRPM_);
    return true;
}

bool P100SMotor::startNegativeRPMJog()
{
    // 步骤 0: 检查并使能电机
    if (!isEnabled()) {
        LOG_INFO("电机[{}]未使能，正在进行使能...", motorID_);
        if (!enable()) {
            LOG_ERROR("电机[{}]使能失败，无法执行移动命令", motorID_);
            return false;
        }
    }

    if (jogRPM_ == 0) {
        LOG_WARN("点动转速为0，请先设置点动速度。");
        return false;
    }

    uint16_t jogConfig = VirtualInputBits::JOG_MODE_BIT | VirtualInputBits::JOG_NEG_BIT;
    if (!writeUInt16(P3_31_VirtualInput, jogConfig)) {
        LOG_ERROR("写入点动配置 [0x{:04X}] 到 P3-31 失败，无法启动负向点动。", jogConfig);
        return false;
    }

    LOG_INFO("已启动电机 {} 的负向点动，转速：{} RPM", motorID_, jogRPM_);
    return true;
}


bool P100SMotor::stopRPMJog()
{
    LOG_INFO("正在停止电机 {} 的点动...", motorID_);

    // 1. 进入减速阶段：平稳停车
    uint16_t jogModeValue = VirtualInputBits::JOG_MODE_BIT;
    if (!writeUInt16(P3_31_VirtualInput, jogModeValue)) {
        LOG_ERROR("写入 JOG_MODE_BIT [0x{:04X}] 到 P3-31 失败，无法停止点动。", jogModeValue);
        return false;
    }
    LOG_INFO("P3-31 已设置为 JogMode，电机 {} 正在减速。", motorID_);

    // 2. 抱闸阶段：机械锁定
    const uint16_t brakeToServoOffDelayMs = DEFAULT_BRAKE_DELAY_MS;

    uint16_t motorBrakeEngageSpeed = 0;
    if (!readUInt16(PA49_MinBrakeSpeed, motorBrakeEngageSpeed)) {
        LOG_WARN("读取 PA49 失败。使用默认最小抱闸转速 50RPM。");
        motorBrakeEngageSpeed = 50;
    }
    LOG_INFO("电机 {} 的抱闸参数：最小转速={} RPM，伺服断电延迟={} ms。", motorID_, motorBrakeEngageSpeed, brakeToServoOffDelayMs);

    const int maxWaitTime = 5000;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < maxWaitTime) {
        uint16_t currentSpeed = 0;
        if (readUInt16(S0_CurrentSpeed, currentSpeed)) {
            if (currentSpeed <= motorBrakeEngageSpeed) {
                LOG_INFO("电机 {} 转速 ({} RPM) 已低于最小抱闸转速 ({} RPM)。", motorID_, currentSpeed, motorBrakeEngageSpeed);
                break;
            }
        } else {
            LOG_WARN("读取电机 {} 当前转速失败。正在重试...", motorID_);
        }
        QThread::msleep(100);
    }

    if (timer.elapsed() >= maxWaitTime) {
        LOG_WARN("等待电机 {} 转速降至 {} RPM 以下超时。将继续执行伺服断电。", motorID_, motorBrakeEngageSpeed);
    }

    LOG_INFO("电机 {} 正在等待 {} ms 以完成抱闸。", motorID_, brakeToServoOffDelayMs);
    QThread::msleep(brakeToServoOffDelayMs);

    // 3. 断电阶段：最终停止
    if (!disable()) {
        LOG_ERROR("为电机 {} 写入 PA53=0 失败，无法断开伺服使能。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 点动已停止。伺服使能已断开。", motorID_);

    return true;
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

    // 等待0.5秒钟，确保驱动器处理完命令
    wait(500);

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
        // 在每次轮询时，增加对电机使能状态的检查
        // 如果电机不再使能，则认为运动被中断或紧急停止
        if (!isEnabled()) {
            LOG_WARN("电机 {} 移动等待被中断，因为伺服使能已断开。", motorID_);
            return false;
        }

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
    LOG_INFO("开始电机 {} 的回原点操作...", motorID_);

    // 步骤 1: 设置绝对目标圈数为0
    if (!setAbsoluteTargetRevolutions(0.0)) {
        LOG_ERROR("电机 {} 设置回原点目标位置失败。", motorID_);
        return false;
    }

    // 步骤 2: 触发移动命令，电机开始移动
    if (!triggerMove()) {
        LOG_ERROR("电机 {} 触发回原点移动命令失败。", motorID_);
        return false;
    }

    // 如果所有步骤都成功，则回原点操作开始
    LOG_INFO("电机 {} 回原点操作已成功启动。", motorID_);
    return true;
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

void P100SMotor::emergencyStop()
{
    LOG_CRITICAL("电机 {} 触发紧急停止！", motorID_);

    // 1. 向 P3-31 虚拟输入端子下发 内部位置控制命令停止
    // 相当于取消点动并且终止位置移动
    if (!writeUInt16(P3_31_VirtualInput, MOVE_HOLD_BIT)) {
        LOG_ERROR("清空P3-31虚拟输入端子失败。");
    }

    wait(50);   // 延迟 50 ms，等待降速抱闸

    // 2. 断开伺服使能 (PA-53 = 0)，立即切断电机扭矩
    if (!disable()) {
        LOG_ERROR("为电机 {} 写入 PA53=0 失败，无法断开伺服使能。", motorID_);
    }

    // 3. 执行软复位，确保伺服从异常状态恢复
    // 紧急停止后，伺服驱动器通常处于错误或需要复位的状态
    if (!softReset()) {
        LOG_ERROR("电机 {} 软复位失败。", motorID_);
    }

    LOG_CRITICAL("电机 {} 紧急停止操作完成。所有运动已中止，伺服使能已断开，并已尝试软复位。", motorID_);
}

bool P100SMotor::initEnvironment()
{
    LOG_INFO("开始初始化电机 {} 的所有环境配置...", motorID_);
    // 0. 设置前确保伺服处于禁用状态
    if (!disable()) {
        LOG_ERROR("电机 {} 初始化结束时无法断开伺服使能。", motorID_);
        return false;
    }

    // 1. 设置控制模式、指令方式和电子齿轮比
    // PA4=3, PA14=3, PA11=0, PA12=131072/16, PA13=10800/16
    if (!writeUInt16(PA4_MoveMode, 3) || !writeUInt16(PA14_PosCommandMode, 3) ||
        !writeUInt16(PA11_CmdPulseRev, 0) ||
        !writeUInt16(PA12_EleGearNum, PA12_GEAR_NUMERATOR) ||
        !writeUInt16(PA13_EleGearDen, PA13_GEAR_DENOMINATOR)) {
        LOG_ERROR("电机 {} 设置控制模式、指令方式或电子齿轮比失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置控制模式、指令方式和电子齿轮比完成。", motorID_);

    // 2. 设置虚拟输入模式和点动速度/来源
    // P3_30 = 2, PA21 = 10, PA22 = 5
    if (!writeUInt16(P3_30_VirtInputMode, 2) || !writeUInt16(PA21_JogRPM, 10) ||
        !writeUInt16(PA22_JogSpeedSource, 5)) {
        LOG_ERROR("电机 {} 设置DI模式、点动速度或速度来源失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置DI配置为：混合输入模式；点动转速：10；速度来源：内部速度。", motorID_);

    // 3. 设置加减速时间常数 (PA40, PA41, PA42)
    if (!writeUInt16(PA40_AccTime, 500) ||
        !writeUInt16(PA41_DecTime, 500) ||
        !writeUInt16(PA42_ScurveTime, 500)) {
        LOG_ERROR("电机 {} 设置加减速时间常数失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置加减速时间常数 (PA40=500, PA41=500, PA42=500) 完成。", motorID_);

    // 4. 设置刹车/抱闸相关配置 (PA47, PA48, PA49, PA94)
    if (!writeUInt16(PA47_BrakeDelay, 10) || !writeUInt16(PA48_BrakeRunSet, 100) ||
        !writeUInt16(PA49_MinBrakeSpeed, 60) || !writeUInt16(PA94_BrakeDelay, 0)) {
        LOG_ERROR("电机 {} 设置刹车/抱闸配置失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置刹车/抱闸配置完成。", motorID_);

    // 5. 设置数字输出DO功能 (P3-20, P3-21, P3-22, P3-23)
    if (!writeUInt16(P3_20_DO1Func, 5) || !writeUInt16(P3_21_DO2Func, 8) ||
        !writeUInt16(P3_22_DO3Func, 0) || !writeUInt16(P3_23_DO4Func, 3)) {
        LOG_ERROR("电机 {} 设置数字输出DO功能失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置数字输出DO功能完成。", motorID_);

    // 6. 设置虚拟DI功能定义 (P3-38~P3-42)
    if (!writeUInt16(P3_38_DI1Func, 28) || !writeUInt16(P3_39_DI2Func, 16) ||
        !writeUInt16(P3_40_DI3Func, 22) || !writeUInt16(P3_41_DI4Func, 23) ||
        !writeUInt16(P3_42_DI5Func, 27)) {
        LOG_ERROR("电机 {} 设置虚拟DI功能定义失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置虚拟DI功能定义完成。", motorID_);

    // 7. 设置绝对位置控制和编码器类型
    // P4_0 = 0 (绝对位置指令), PA62 = 5 (多圈绝对值编码器)
    if (!writeUInt16(PA62_EncoderType, 5) || !writeUInt16(P4_0_PositionMode, 0)) {
        LOG_ERROR("电机 {} 设置绝对位置控制和编码器类型失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 设置为：绝对式位置指令控制，并确认使用多圈绝对值编码器。", motorID_);

    // 8. 设置单圈位置零点并复位多圈编码器
    if (!writeUInt16(P3_36_SingleTurnZero, 1) || !writeUInt16(P3_34_ResetEncoderMultiTurns, 1)) {
        LOG_ERROR("电机 {} 设置单圈零点或复位多圈编码器失败。", motorID_);
        return false;
    }
    LOG_INFO("电机 {} 已设置单圈零点并复位多圈编码器。", motorID_);

    LOG_INFO("电机 {} 所有环境配置已成功完成。", motorID_);
    return true;
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

    // TO2.3：发送触发信号（CTRG上升沿有效）
    if (!writeUInt16(P3_31_VirtualInput, CLEAR_ALL_BITS)) {
        LOG_ERROR("电机{}清零 CTRG 触发信号失败", motorID_);
        return false;
    }
    if (!writeUInt16(P3_31_VirtualInput, TRIGGER_MOVE)) {
        LOG_ERROR("电机{}置位 CTRG 触发信号失败", motorID_);
        return false;
    }

    return true;
}

// P100SMotor.cpp 中修正后的方法实现
bool P100SMotor::softReset() {
    LOG_INFO("正在对电机 {} 执行软复位...", motorID_);

    // 根据文档，向 PA60 写入 1 来触发软复位
    if (!writeUInt16(PA60_ResetServo, 1)) {
        LOG_ERROR("电机 {} 执行软复位失败。无法写入寄存器。", motorID_);
        return false;
    }

    LOG_INFO("电机 {} 软复位命令已发送。系统将重新启动。", motorID_);
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
