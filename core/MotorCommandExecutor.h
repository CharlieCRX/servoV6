// core/MotorCommandExecutor.h
#ifndef MOTOR_COMMAND_EXECUTOR_H
#define MOTOR_COMMAND_EXECUTOR_H

#include "CommandVisitor.h"
#include "Logger.h" // 用于日志

class MotorCommandExecutor : public CommandVisitor {
public:
    MotorCommandExecutor(); // ← 声明构造函数
    // 为每个命令类型实现访问方法
    bool visit(IMotor* motor, const SetPositionSpeed& cmd) override {
        LOG_DEBUG("Setting speed for motor to {} mm/s.", cmd.mm_per_sec);
        if (!motor->SetPositionSpeed(cmd.mm_per_sec)) {
            LOG_ERROR("Failed to set speed.");
            return false;
        }
        return true;
    }

    bool visit(IMotor* motor, const SetJogSpeed& cmd) override {
        LOG_DEBUG("Setting jog speed for motor to {} mm/s.", cmd.mm_per_sec);
        if (!motor->setJogSpeed(cmd.mm_per_sec)) { // <-- 调用新接口
            LOG_ERROR("Failed to set jog speed.");
            return false;
        }
        return true;
    }

    bool visit(IMotor* motor, const RelativeMove& cmd) override {
        LOG_DEBUG("Moving motor by {} mm.", cmd.delta_mm);
        if (!motor->relativeMove(cmd.delta_mm)) {
            LOG_ERROR("Failed to move motor.");
            return false;
        }
        return true;
    }

    bool visit(IMotor* motor, const Wait& cmd) override {
        LOG_DEBUG("Waiting for {} ms...", cmd.milliseconds);
        motor->wait(cmd.milliseconds);
        return true; // Wait 操作通常不会失败，或者失败逻辑在 wait 内部处理
    }

    bool visit(IMotor* motor, const GoHome& cmd) override {
        LOG_DEBUG("Homing motor...");
        if (!motor->goHome()) {
            LOG_ERROR("Failed to home motor.");
            return false;
        }
        return true;
    }

    bool visit(IMotor* motor, const AbsoluteMove& cmd) override {
        LOG_DEBUG("Moving motor to absolute position {} mm.", cmd.target_mm);
        if (!motor->absoluteMove(cmd.target_mm)) {
            LOG_ERROR("Failed to move motor to absolute position {}.", cmd.target_mm);
            return false;
        }
        return true;
    }

    // 新增 visit(StartPositiveJog)
    bool visit(IMotor* motor, const StartPositiveJog& cmd) override {
        // 调用 IMotor 的新接口，不需要传入速度参数
        LOG_DEBUG("Starting positive jog for motor.");
        if (!motor->startPositiveJog()) {
            LOG_ERROR("Failed to start positive jog.");
            return false;
        }
        return true;
    }

    // 新增 visit(StartNegativeJog)
    bool visit(IMotor* motor, const StartNegativeJog& cmd) override {
        // 调用 IMotor 的新接口，不需要传入速度参数
        LOG_DEBUG("Starting negative jog for motor.");
        if (!motor->startNegativeJog()) {
            LOG_ERROR("Failed to start negative jog.");
            return false;
        }
        return true;
    }

    bool visit(IMotor* motor, const StopJog& cmd) override {
        LOG_DEBUG("Stopping jog for motor.");
        if (!motor->stopJog()) {
            LOG_ERROR("Failed to stop jog.");
            return false;
        }
        return true;
    }
};

#endif // MOTOR_COMMAND_EXECUTOR_H
