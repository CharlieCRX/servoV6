#ifndef MOTOR_COMMAND_EXECUTOR_H
#define MOTOR_COMMAND_EXECUTOR_H

#include "CommandVisitor.h"
#include "Logger.h"

class MotorCommandExecutor : public CommandVisitor {
public:
    MotorCommandExecutor() = default;

    // 线性命令实现 (调用适配器的方法)
    bool visit(ILinearServoAdapter* adapter, const SetPositionSpeed& cmd) override {
        LOG_DEBUG("执行线性设置位置速度命令: {} mm/s.", cmd.mm_per_sec);
        return adapter->setPositionSpeed(cmd.mm_per_sec);
    }
    bool visit(ILinearServoAdapter* adapter, const SetJogSpeed& cmd) override {
        LOG_DEBUG("执行线性设置点动速度命令: {} mm/s.", cmd.mm_per_sec);
        return adapter->setJogSpeed(cmd.mm_per_sec);
    }
    bool visit(ILinearServoAdapter* adapter, const RelativeMove& cmd) override {
        LOG_DEBUG("执行线性相对移动命令: {} mm.", cmd.delta_mm);
        return adapter->relativeMove(cmd.delta_mm);
    }
    bool visit(ILinearServoAdapter* adapter, const AbsoluteMove& cmd) override {
        LOG_DEBUG("执行线性绝对移动命令: {} mm.", cmd.target_mm);
        return adapter->absoluteMove(cmd.target_mm);
    }
    bool visit(ILinearServoAdapter* adapter, const StartPositiveJog& cmd) override {
        LOG_DEBUG("执行线性正向点动开始命令.");
        return adapter->startPositiveJog();
    }
    bool visit(ILinearServoAdapter* adapter, const StartNegativeJog& cmd) override {
        LOG_DEBUG("执行线性负向点动开始命令.");
        return adapter->startNegativeJog();
    }

    // 旋转命令实现 (调用适配器的方法)
    bool visit(IRotaryServoAdapter* adapter, const SetAngularPositionSpeed& cmd) override {
        LOG_DEBUG("执行旋转设置位置速度命令: {} deg/s.", cmd.degrees_per_sec);
        return adapter->setAngularPositionSpeed(cmd.degrees_per_sec);
    }
    bool visit(IRotaryServoAdapter* adapter, const SetAngularJogSpeed& cmd) override {
        LOG_DEBUG("执行旋转设置点动速度命令: {} deg/s.", cmd.degrees_per_sec);
        return adapter->setAngularJogSpeed(cmd.degrees_per_sec);
    }
    bool visit(IRotaryServoAdapter* adapter, const RelativeAngularMove& cmd) override {
        LOG_DEBUG("执行旋转相对移动命令: {} 度.", cmd.degrees);
        return adapter->relativeAngularMove(cmd.degrees);
    }
    bool visit(IRotaryServoAdapter* adapter, const AbsoluteAngularMove& cmd) override {
        LOG_DEBUG("执行旋转绝对移动命令: {} 度.", cmd.degrees);
        return adapter->absoluteAngularMove(cmd.degrees);
    }
    bool visit(IRotaryServoAdapter* adapter, const StartPositiveAngularJog& cmd) override {
        LOG_DEBUG("执行旋转正向点动开始命令.");
        return adapter->startPositiveAngularJog();
    }
    bool visit(IRotaryServoAdapter* adapter, const StartNegativeAngularJog& cmd) override {
        LOG_DEBUG("执行旋转负向点动开始命令.");
        return adapter->startNegativeAngularJog();
    }

    // 通用命令实现 (调用适配器的方法)
    bool visit(IServoAdapter* adapter, const Wait& cmd) override {
        LOG_DEBUG("执行等待命令: {} 毫秒.", cmd.milliseconds);
        adapter->wait(cmd.milliseconds);
        return true;
    }
    bool visit(IServoAdapter* adapter, const GoHome& cmd) override {
        LOG_DEBUG("执行回零命令.");
        return adapter->goHome();
    }
    bool visit(IServoAdapter* adapter, const StopJog& cmd) override {
        LOG_DEBUG("执行停止点动命令.");
        return adapter->stopJog();
    }
    bool visit(IServoAdapter* adapter, const InitEnvironment& cmd) override {
        LOG_DEBUG("执行初始化环境命令.");
        return adapter->initEnvironment();
    }

    bool visit(IServoAdapter* adapter, const EmergencyStop& cmd) override {
        LOG_DEBUG("执行急停命令.");
        adapter->emergencyStop();
        return true;
    }
};

#endif // MOTOR_COMMAND_EXECUTOR_H
