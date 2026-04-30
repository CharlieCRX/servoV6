#pragma once
#include "IAxisDriver.h"
#include "AxisRepository.h"

class EnableUseCase {
public:
    EnableUseCase(AxisRepository& repo, IAxisDriver& driver)
        : m_repo(repo), m_driver(driver) {}

    /**
     * @brief 执行使能/掉电操作
     * @param id 目标轴的标识符
     * @param active true: 使能(上电), false: 掉电
     * @return RejectionReason 透传领域层判定结果
     */
    RejectionReason execute(AxisId id, bool active) {
        Axis& axis = m_repo.getAxis(id);

        if (!axis.enable(active)) {
            return axis.lastRejection();
        }

        if (axis.hasPendingCommand()) {
            m_driver.send(id, axis.getPendingCommand());
        }
        return RejectionReason::None;
    }

private:
    AxisRepository& m_repo;
    IAxisDriver& m_driver;
};
