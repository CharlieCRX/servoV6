// core/RotaryServoAdapter.cpp
#include "RotaryServoAdapter.h"
#include "Logger.h" // 假设 Logger 存在于 utils 目录下

// **这是正确的构造函数实现，与 .h 文件中的声明匹配**
RotaryServoAdapter::RotaryServoAdapter(std::unique_ptr<IMotor> motor, double degreesPerRevolution)
    : motor_(std::move(motor)), degreesPerRevolution_(degreesPerRevolution),
    currentAngularPositionSpeedRPM_(0.0), currentAngularJogSpeedRPM_(0.0) {
    if (!motor_) {
        LOG_ERROR("RotaryServoAdapter: IMotor cannot be null.");
    }
    LOG_INFO("RotaryServoAdapter initialized with degrees/rev: {}.", degreesPerRevolution_);
}

IMotor* RotaryServoAdapter::getMotor() { return motor_.get(); }
bool RotaryServoAdapter::goHome() { return motor_->goHome(); }
void RotaryServoAdapter::wait(int ms) { motor_->wait(ms); }
bool RotaryServoAdapter::stopJog() { return motor_->stopRPMJog(); }

bool RotaryServoAdapter::setAngularPositionSpeed(double degreesPerSec) {
    double rpm = degreesPerSec / degreesPerRevolution_ * 60.0; // deg/s / (deg/rev) * 60s/min = rev/min (RPM)
    currentAngularPositionSpeedRPM_ = rpm; // 保存RPM
    LOG_DEBUG("Rotary: Set angular position speed {} deg/s -> {} RPM.", degreesPerSec, rpm);
    return motor_->setRPM(rpm);
}
bool RotaryServoAdapter::setAngularJogSpeed(double degreesPerSec) {
    double rpm = degreesPerSec / degreesPerRevolution_ * 60.0;
    currentAngularJogSpeedRPM_ = rpm; // 保存RPM
    LOG_DEBUG("Rotary: Set angular jog speed {} deg/s -> {} RPM.", degreesPerSec, rpm);
    return true; // 不直接调用 setRPM
}
bool RotaryServoAdapter::angularMove(double degrees) {
    double revolutions = degrees / degreesPerRevolution_;
    LOG_DEBUG("Rotary: Move {} degrees -> {} revolutions.", degrees, revolutions);
    return motor_->relativeMoveRevolutions(revolutions); // 角度移动通常是相对的
}
bool RotaryServoAdapter::startPositiveAngularJog() {
    LOG_DEBUG("Rotary: Start positive angular jog.");
    return motor_->startPositiveRPMJog();
}
bool RotaryServoAdapter::startNegativeAngularJog() {
    LOG_DEBUG("Rotary: Start negative jog.");
    return motor_->startNegativeRPMJog();
}
