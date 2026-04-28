#pragma once
#include "IAxisDriver.h"
#include "AxisRepository.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 相对位置移动执行案例（纯净动作层）
 * 严格遵守单一语义原则：只负责发起定位，不混入使能策略
 * 采用与 MoveAbsoluteUseCase 一致的 AxisId 寻址模式
 */
class MoveRelativeUseCase {
public:
    MoveRelativeUseCase(AxisRepository& repo, IAxisDriver& driver)
        : m_repo(repo), m_driver(driver) {}

    /**
     * @brief 执行相对位移
     * @param id 目标轴的标识符
     * @param distance 移动增量（可正可负）
     * @return RejectionReason 100% 透传自领域层的原始原因
     */
    RejectionReason execute(AxisId id, double distance) {
        // 1. 从仓库中获取目标轴的引用
        Axis& axis = m_repo.getAxis(id);

        // 2. 调用领域层规则，判定该相对位移请求是否合法
        if (!axis.moveRelative(distance)) {
            LOG_WARN(LogLayer::APP, "MoveRelUC", "MoveRelative rejected. Reason code: " + std::to_string(static_cast<int>(axis.lastRejection())));
            return axis.lastRejection();
        }

        // 3. 规则允许，将 Axis 产生的 MoveCommand 下发给驱动，带上 AxisId
        m_driver.send(id, axis.getPendingCommand());
        return RejectionReason::None;
    }

private:
    AxisRepository& m_repo;
    IAxisDriver& m_driver;
};
