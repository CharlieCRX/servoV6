#include "P100SMotor.h"
#include "Logger.h" // 改为包含新的 Logger.h

P100SMotor::P100SMotor() {
    LOG_INFO("P100SMotor: Instance created.");
}

bool P100SMotor::setRPM(double rpm) {
    LOG_INFO("P100SMotor: Setting RPM to {}.", rpm);
    return true;
}

bool P100SMotor::relativeMoveRevolutions(double revolutions) {
    LOG_INFO("P100SMotor: Relative move by {} revolutions.", revolutions);
    return true;
}

bool P100SMotor::absoluteMoveRevolutions(double targetRevolutions) {
    LOG_INFO("P100SMotor: Absolute move to {} revolutions.", targetRevolutions);
    return true;
}

bool P100SMotor::startPositiveRPMJog() {
    LOG_INFO("P100SMotor: Starting positive RPM jog.");
    return true;
}

bool P100SMotor::startNegativeRPMJog() {
    LOG_INFO("P100SMotor: Starting negative RPM jog.");
    return true;
}

bool P100SMotor::stopRPMJog() {
    LOG_INFO("P100SMotor: Stopping RPM jog.");
    return true;
}

bool P100SMotor::goHome() {
    LOG_INFO("P100SMotor: Executing GoHome command.");
    return true;
}

void P100SMotor::wait(int ms) {
    LOG_INFO("P100SMotor: Waiting for {} ms.", ms);
}
