// core/Motor.cpp (真实的电机实现)
#include "Motor.h"
#include "Logger.h" // 假设你有日志

Motor::Motor(int id) : motorId(id), currentRPM(0.0), currentRevolutions(0.0) {
    LOG_INFO("Motor {} initialized.", motorId);
    // driver = new HardwareDriver(id); // 真实场景中初始化硬件驱动
}

bool Motor::setRPM(double rpm) {
    if (rpm < 0) {
        LOG_ERROR("Motor {}: RPM cannot be negative.", motorId);
        return false;
    }
    this->currentRPM = rpm;
    LOG_DEBUG("Motor {}: Set RPM to {}.", motorId, rpm);
    // driver->setSpeed(rpm); // 调用底层硬件接口
    return true;
}

bool Motor::relativeMoveRevolutions(double revolutions) {
    LOG_INFO("Motor {}: Relative move {} revolutions at {} RPM.", motorId, revolutions, currentRPM);
    // driver->moveRelative(revolutions, currentRPM); // 调用底层硬件接口
    this->currentRevolutions += revolutions;
    return true; // 模拟成功
}

bool Motor::absoluteMoveRevolutions(double targetRevolutions) {
    LOG_INFO("Motor {}: Absolute move to {} revolutions at {} RPM.", motorId, targetRevolutions, currentRPM);
    // driver->moveAbsolute(targetRevolutions, currentRPM); // 调用底层硬件接口
    this->currentRevolutions = targetRevolutions;
    return true; // 模拟成功
}

bool Motor::startPositiveRPMJog() {
    LOG_INFO("Motor {}: Starting positive RPM jog at {} RPM.", motorId, currentRPM);
    // driver->startJog(currentRPM, true);
    return true;
}

bool Motor::startNegativeRPMJog() {
    LOG_INFO("Motor {}: Starting negative RPM jog at {} RPM.", motorId, currentRPM);
    // driver->startJog(currentRPM, false);
    return true;
}

bool Motor::stopRPMJog() {
    LOG_INFO("Motor {}: Stopping RPM jog.", motorId);
    // driver->stopJog();
    return true;
}

bool Motor::goHome() {
    LOG_INFO("Motor {}: Homing...", motorId);
    // driver->goHome();
    this->currentRevolutions = 0.0; // 假设归零后圈数为0
    return true;
}

void Motor::wait(int ms) {
    LOG_INFO("Motor {}: Waiting for {} ms.", motorId, ms);
    // std::this_thread::sleep_for(std::chrono::milliseconds(ms)); // 真实等待
}
