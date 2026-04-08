#pragma once
#include "application/axis/IAxisDriver.h"

class JogAxisUseCase {
public:
    explicit JogAxisUseCase(IAxisDriver& driver) : driver_(driver) {}

    // 执行流程：Axis -> Command -> Driver
    void execute(Axis& axis, Direction dir) {
        // 1. 调用领域规则，尝试产生点动意图
        if (!axis.jog(dir)) {
            return; // 被领域规则拦截，流程终止
        }

        // 2. 规则允许，将 Axis 产生的命令发送给驱动
        driver_.send(axis.getPendingCommand());
    }

private:
    IAxisDriver& driver_;
};