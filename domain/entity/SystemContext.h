// domain/entity/SystemContext.h
#pragma once
#include <memory>
#include "entity/Axis.h"
#include "entity/AxisId.h"
#include "gantry/GantryGroup.h"
#include "infrastructure/ISystemDriver.h"
#include <unordered_map>

class SystemContext {
public:
    SystemContext() {
        // 1. 初始化 6 个固定轴实体
        m_axes[AxisId::X] = std::make_unique<Axis>();
        m_axes[AxisId::X1] = std::make_unique<Axis>();
        m_axes[AxisId::X2] = std::make_unique<Axis>();
        m_axes[AxisId::Y] = std::make_unique<Axis>();
        m_axes[AxisId::Z] = std::make_unique<Axis>();
        m_axes[AxisId::R] = std::make_unique<Axis>();

        // 2. 初始化龙门大脑，并注入本组内部的 X1, X2 引用
        m_gantry = std::make_unique<GantryGroup>(*m_axes[AxisId::X1], *m_axes[AxisId::X2]);
    }

    /**
     * @brief Try-Get 模式获取轴对象
     * @param id 目标轴ID
     * @param outAxis [输出参数] 成功则指向轴对象，失败为 nullptr
     * @param reason [输出参数] 失败时的具体拒绝原因
     * @return true 允许访问；false 拒绝访问
     */
    bool tryGetAxis(AxisId id, Axis*& outAxis, RejectionReason& reason) {
        // A. 龙门业务语义校验（宪法）
        if (m_isGantryCoupled) {
            if (id == AxisId::X1 || id == AxisId::X2) {
                reason = RejectionReason::InvalidState; // 联动模式锁定物理轴
                outAxis = nullptr;
                return false;
            }
        } else {
            if (id == AxisId::X) {
                reason = RejectionReason::InvalidState; // 解耦模式锁定逻辑轴
                outAxis = nullptr;
                return false;
            }
        }

        // B. 容器查找
        auto it = m_axes.find(id);
        if (it == m_axes.end()) {
            reason = RejectionReason::UnknownError; // 轴未在系统中注册
            outAxis = nullptr;
            return false;
        }

        // C. 校验通过
        outAxis = it->second.get();
        reason = RejectionReason::None;
        return true;
    }

    // --- 其他基础接口 ---
    GantryGroup& gantry() { return *m_gantry; }
    
    void setDriver(ISystemDriver* driver) { m_driver = driver; }
    ISystemDriver* driver() { return m_driver; }

    bool isGantryCoupled() const { return m_isGantryCoupled; }
    void setCoupledState(bool coupled) { m_isGantryCoupled = coupled; }

private:
    std::unordered_map<AxisId, std::unique_ptr<Axis>> m_axes;
    std::unique_ptr<GantryGroup> m_gantry;
    ISystemDriver* m_driver = nullptr;
    bool m_isGantryCoupled = true; 
};