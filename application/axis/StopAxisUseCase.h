#pragma once
#include "IAxisDriver.h"
#include "AxisRepository.h"

/**
 * @brief 停止轴动作执行案例
 * 负责紧急停止或正常停止，具有最高优先级，可覆盖其他运动意图
 * 采用 AxisId 寻址模式（与 MoveAbsoluteUseCase 一致）
 */
class StopAxisUseCase {
public:
    StopAxisUseCase(AxisRepository& repo, IAxisDriver& driver)
        : m_repo(repo), m_driver(driver) {}

    /**
     * @brief 执行停止动作
     * 停止是安全指令，在领域层被设计为不可拒绝
     * @param id 目标轴的标识符
     */
    void execute(AxisId id) {
        Axis& axis = m_repo.getAxis(id);

        // 1. 调用实体层停止方法
        // 该方法会清除 m_pending_intent 中的 Move 或 Jog，替换为 StopCommand
        if (axis.stop()) {
            // 2. 将产生的停止指令发送至硬件抽象层，带上 AxisId
            m_driver.send(id, axis.getPendingCommand());
        }
    }

private:
    AxisRepository& m_repo;
    IAxisDriver& m_driver;
};
