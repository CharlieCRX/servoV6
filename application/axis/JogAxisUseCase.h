#pragma once
#include "application/axis/IAxisDriver.h"

class JogAxisUseCase {
public:
    explicit JogAxisUseCase(IAxisDriver& driver) : driver_(driver) {}

    /**
     * @brief 执行点动并返回结果原因
     * @return RejectionReason::None 表示成功（已发送 Jog 或 Enable 指令）
     */
    RejectionReason execute(Axis& axis, Direction dir) {
        // 1. 调用领域规则，尝试产生点动意图
        if (!axis.jog(dir)) {
            return axis.lastRejection();
        }

        // 3. 规则允许，将 Axis 产生的命令发送给驱动
        driver_.send(axis.getPendingCommand());
        return RejectionReason::None;
    }


    /**
     * @brief 停止点动
     * 这是一个安全操作，不返回 RejectionReason，因为在业务逻辑上它永远不应被拒绝
     */
    void stop(Axis& axis, Direction dir) {
        // 1. 调用领域层产生的停止点动意图
        // 即使 Axis 处于 Error 态，Domain 层的 stopJog() 也应返回 true 并生成 active=false 的意图
        if (axis.stopJog(dir)) {
            // 2. 将产生的指令（JogCommand {active: false}）下发
            driver_.send(axis.getPendingCommand());
        }
    }

private:
    IAxisDriver& driver_;
};