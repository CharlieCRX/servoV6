#pragma once
#include "application/axis/IAxisDriver.h"

class JogAxisUseCase {
public:
    explicit JogAxisUseCase(IAxisDriver& driver) : driver_(driver) {}

    // 执行流程：Axis -> Command -> Driver
    void execute(Axis& axis, Direction dir) {
        // 1. 调用领域规则，尝试产生点动意图
        if (!axis.jog(dir)) {
            // 2. 自动上电逻辑：如果失败是因为轴处于禁用状态，则尝试产生使能意图
            if (axis.state() == AxisState::Disabled && 
                axis.lastRejection() == RejectionReason::InvalidState) {
                
                if (axis.enable(true)) {
                    // 下发使能意图给驱动器
                    driver_.send(axis.getPendingCommand());
                }
            }
            return; // 流程终止，等待状态同步后再由用户或逻辑再次触发点动
        }

        // 3. 规则允许，将 Axis 产生的命令发送给驱动
        driver_.send(axis.getPendingCommand());
    }

private:
    IAxisDriver& driver_;
};