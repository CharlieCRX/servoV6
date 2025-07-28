// core/MotorCommandExecutor.h (实现访问者，操作 IServoAdapter)
#ifndef MOTOR_COMMAND_EXECUTOR_H
#define MOTOR_COMMAND_EXECUTOR_H

#include "CommandVisitor.h"
#include "Logger.h"

class MotorCommandExecutor : public CommandVisitor {
public:
    MotorCommandExecutor();
    // 线性命令实现 (调用适配器的方法)
    bool visit(ILinearServoAdapter* adapter, const SetPositionSpeed& cmd) override {
        LOG_DEBUG("Executing linear SetPositionSpeed: {} mm/s.", cmd.mm_per_sec);
        return adapter->setPositionSpeed(cmd.mm_per_sec);
    }
    bool visit(ILinearServoAdapter* adapter, const SetJogSpeed& cmd) override {
        LOG_DEBUG("Executing linear SetJogSpeed: {} mm/s.", cmd.mm_per_sec);
        return adapter->setJogSpeed(cmd.mm_per_sec);
    }
    bool visit(ILinearServoAdapter* adapter, const RelativeMove& cmd) override {
        LOG_DEBUG("Executing linear RelativeMove: {} mm.", cmd.delta_mm);
        return adapter->relativeMove(cmd.delta_mm);
    }
    bool visit(ILinearServoAdapter* adapter, const AbsoluteMove& cmd) override {
        LOG_DEBUG("Executing linear AbsoluteMove: {} mm.", cmd.target_mm);
        return adapter->absoluteMove(cmd.target_mm);
    }
    bool visit(ILinearServoAdapter* adapter, const StartPositiveJog& cmd) override {
        LOG_DEBUG("Executing linear StartPositiveJog.");
        return adapter->startPositiveJog();
    }
    bool visit(ILinearServoAdapter* adapter, const StartNegativeJog& cmd) override {
        LOG_DEBUG("Executing linear StartNegativeJog.");
        return adapter->startNegativeJog();
    }

    // 角度命令实现 (调用适配器的方法)
    bool visit(IRotaryServoAdapter* adapter, const SetAngularPositionSpeed& cmd) override {
        LOG_DEBUG("Executing rotary SetAngularPositionSpeed: {} deg/s.", cmd.degrees_per_sec);
        return adapter->setAngularPositionSpeed(cmd.degrees_per_sec);
    }
    bool visit(IRotaryServoAdapter* adapter, const SetAngularJogSpeed& cmd) override {
        LOG_DEBUG("Executing rotary SetAngularJogSpeed: {} deg/s.", cmd.degrees_per_sec);
        return adapter->setAngularJogSpeed(cmd.degrees_per_sec);
    }
    bool visit(IRotaryServoAdapter* adapter, const AngularMove& cmd) override {
        LOG_DEBUG("Executing rotary AngularMove: {} degrees.", cmd.degrees);
        return adapter->angularMove(cmd.degrees);
    }
    bool visit(IRotaryServoAdapter* adapter, const StartPositiveAngularJog& cmd) override {
        LOG_DEBUG("Executing rotary StartPositiveAngularJog.");
        return adapter->startPositiveAngularJog();
    }
    bool visit(IRotaryServoAdapter* adapter, const StartNegativeAngularJog& cmd) override {
        LOG_DEBUG("Executing rotary StartNegativeAngularJog.");
        return adapter->startNegativeAngularJog();
    }

    // 通用命令实现 (调用适配器的方法)
    bool visit(IServoAdapter* adapter, const Wait& cmd) override {
        LOG_DEBUG("Executing Wait: {} ms.", cmd.milliseconds);
        adapter->wait(cmd.milliseconds);
        return true;
    }
    bool visit(IServoAdapter* adapter, const GoHome& cmd) override {
        LOG_DEBUG("Executing GoHome.");
        return adapter->goHome();
    }
    bool visit(IServoAdapter* adapter, const StopJog& cmd) override {
        LOG_DEBUG("Executing StopJog.");
        return adapter->stopJog();
    }
};

#endif // MOTOR_COMMAND_EXECUTOR_H
