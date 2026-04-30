#pragma once
#include "IAxisDriver.h"
#include "AxisRepository.h"
#include "infrastructure/logger/Logger.h"

class JogAxisUseCase {
public:
    JogAxisUseCase(AxisRepository& repo, IAxisDriver& driver)
        : m_repo(repo), m_driver(driver) {}

    /**
     * @brief 执行点动并返回结果原因
     * @param id 目标轴的标识符
     * @param dir 点动方向
     * @return RejectionReason::None 表示成功（已发送 Jog 指令）
     */
    RejectionReason execute(AxisId id, Direction dir) {
        Axis& axis = m_repo.getAxis(id);

        // 1. 调用领域规则，尝试产生点动意图
        if (!axis.jog(dir)) {
            LOG_WARN(LogLayer::APP, "JogUC", "Jog Start rejected. Reason: " + std::to_string(static_cast<int>(axis.lastRejection())));
            return axis.lastRejection();
        }

        // 2. 规则允许，将 Axis 产生的命令发送给驱动，带上 AxisId
        m_driver.send(id, axis.getPendingCommand());
        return RejectionReason::None;
    }


    /**
     * @brief 停止点动
     * 这是一个安全操作，不返回 RejectionReason
     */
    void stop(AxisId id, Direction dir) {
        Axis& axis = m_repo.getAxis(id);

        // 1. 调用领域层产生的停止点动意图
        if (axis.stopJog(dir)) {
            // 2. 将产生的指令（JogCommand {active: false}）下发，带上 AxisId
            m_driver.send(id, axis.getPendingCommand());
        }
    }

private:
    AxisRepository& m_repo;
    IAxisDriver& m_driver;
};
