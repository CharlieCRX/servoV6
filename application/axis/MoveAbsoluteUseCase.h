#pragma once
#include "IAxisDriver.h"
#include "AxisRepository.h"
#include "infrastructure/logger/Logger.h"

/**
 * @brief 绝对定位执行案例（纯净动作层）
 * 严格遵守单一语义原则：只负责发起定位，不混入使能策略
 */
class MoveAbsoluteUseCase {
public:
    MoveAbsoluteUseCase(AxisRepository& repo, IAxisDriver& driver)
        : m_repo(repo), m_driver(driver) {}

    /**
     * @brief 执行绝对定位
     * @param id 目标轴的标识符
     * @param target 目标绝对位置
     * @return RejectionReason 100% 透传自领域层的原始原因
     */
    RejectionReason execute(AxisId id, double target) {
        // 1. 从仓库中获取目标轴的引用
        Axis& axis = m_repo.getAxis(id);

        // 2. 调用领域层规则，判定该移动请求是否合法
        if (!axis.moveAbsolute(target)) {
            // 2. 拒绝：直接返回领域层的拦截原因，不执行任何自愈逻辑
            LOG_WARN(LogLayer::APP, "MoveAbsUC", "Move rejected. Reason code: " + std::to_string(static_cast<int>(axis.lastRejection())));
            return axis.lastRejection();
        }

        // 3. 将 Axis 产生的 MoveCommand 下发给驱动,带上 AxisId，让驱动知道该发给谁
        m_driver.send(id, axis.getPendingCommand());
        return RejectionReason::None;
    }

private:
    AxisRepository& m_repo;
    IAxisDriver& m_driver;
};