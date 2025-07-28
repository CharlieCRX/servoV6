// core/LinearServoAdapter.cpp
#include "LinearServoAdapter.h"
#include "Logger.h" // 假设 Logger 存在于 utils 目录下

// **这是正确的构造函数实现，与 .h 文件中的声明匹配**
LinearServoAdapter::LinearServoAdapter(std::unique_ptr<IMotor> motor, double leadScrewPitchMmPerRev)
    : motor_(std::move(motor)), leadScrewPitchMmPerRev_(leadScrewPitchMmPerRev),
    currentPositionSpeedRPM_(0.0), currentJogSpeedRPM_(0.0) {
    if (!motor_) {
        LOG_ERROR("LinearServoAdapter: IMotor cannot be null.");
    }
    LOG_INFO("LinearServoAdapter initialized with pitch: {} mm/rev.", leadScrewPitchMmPerRev_);
}

IMotor* LinearServoAdapter::getMotor() { return motor_.get(); }
bool LinearServoAdapter::goHome() { return motor_->goHome(); }
void LinearServoAdapter::wait(int ms) { motor_->wait(ms); }
bool LinearServoAdapter::stopJog() { return motor_->stopRPMJog(); }

bool LinearServoAdapter::setPositionSpeed(double mmPerSec) {
    double rpm = mmPerSec / leadScrewPitchMmPerRev_ * 60.0; // mm/s / (mm/rev) * 60s/min = rev/min (RPM)
    currentPositionSpeedRPM_ = rpm; // 保存RPM，供后续move使用
    LOG_DEBUG("Linear: Set position speed {} mm/s -> {} RPM.", mmPerSec, rpm);
    return motor_->setRPM(rpm);
}
bool LinearServoAdapter::setJogSpeed(double mmPerSec) {
    double rpm = mmPerSec / leadScrewPitchMmPerRev_ * 60.0;
    currentJogSpeedRPM_ = rpm; // 保存RPM，供后续jog使用
    LOG_DEBUG("Linear: Set jog speed {} mm/s -> {} RPM.", mmPerSec, rpm);
    return true; // 不直接调用 setRPM，因为点动速度是独立的
}
bool LinearServoAdapter::relativeMove(double mm) {
    double revolutions = mm / leadScrewPitchMmPerRev_;
    LOG_DEBUG("Linear: Relative move {} mm -> {} revolutions.", mm, revolutions);
    return motor_->relativeMoveRevolutions(revolutions);
}
bool LinearServoAdapter::absoluteMove(double targetMm) {
    double targetRevolutions = targetMm / leadScrewPitchMmPerRev_;
    LOG_DEBUG("Linear: Absolute move {} mm -> {} revolutions.", targetMm, targetRevolutions);
    return motor_->absoluteMoveRevolutions(targetRevolutions);
}
bool LinearServoAdapter::startPositiveJog() {
    LOG_DEBUG("Linear: Start positive jog.");
    return motor_->startPositiveRPMJog();
}
bool LinearServoAdapter::startNegativeJog() {
    LOG_DEBUG("Linear: Start negative jog.");
    return motor_->startNegativeRPMJog();
}
