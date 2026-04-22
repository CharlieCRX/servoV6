#pragma once
#include "axis/IAxisDriver.h"

/**
 * @brief 停止轴动作执行案例
 * 负责紧急停止或正常停止，具有最高优先级，可覆盖其他运动意图
 */
class StopAxisUseCase {
public:
    explicit StopAxisUseCase(IAxisDriver& driver) : driver_(driver) {}

    /**
     * @brief 执行停止动作
     * 停止是安全指令，在领域层被设计为不可拒绝
     */
    void execute(Axis& axis) {
        // 1. 调用实体层停止方法
        // 该方法会清除 m_pending_intent 中的 Move 或 Jog，替换为 StopCommand
        if (axis.stop()) {
            // 2. 将产生的停止指令发送至硬件抽象层
            if (axis.hasPendingCommand()) {
                driver_.send(axis.getPendingCommand());
            }
        }
    }

private:
    IAxisDriver& driver_;
};