// domain/entity/SystemContext.h
#pragma once
#include <memory>
#include "entity/Axis.h"
#include "entity/AxisId.h"
#include "entity/ContextRejection.h"
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
     *
     * 业务语义：
     * - NotSynchronized 态：X / X1 / X2 全部拒绝，物理真相未知
     * - Coupled 态：物理轴 X1/X2 锁定，仅暴露逻辑轴 X
     * - Decoupled 态：逻辑轴 X 不可用，暴露物理轴 X1/X2
     */
    bool tryGetAxis(AxisId id, Axis*& outAxis, ContextRejection& reason) {
        // 仅龙门相关轴受联动状态约束，非龙门轴跳过
        if (id == AxisId::X || id == AxisId::X1 || id == AxisId::X2) {
            // A. 前置拦截：状态机尚未同步，物理真相未知 → 拒绝一切龙门轴访问
            if (m_gantry->isNotSynchronized()) {
                reason = ContextRejection::GantryNotSynchronized;
                outAxis = nullptr;
                return false;
            }

            // B. 龙门联动/解耦语义：由 GantryGroup 唯一真相源裁决
            if (m_gantry->isCoupled()) {
                if (id == AxisId::X1 || id == AxisId::X2) {
                    reason = ContextRejection::PhysicalAxisLockedByGantry;
                    outAxis = nullptr;
                    return false;
                }
            } else {
                if (id == AxisId::X) {
                    reason = ContextRejection::LogicalAxisUnavailableWhenDecoupled;
                    outAxis = nullptr;
                    return false;
                }
            }
        }

        // C. 容器查找
        auto it = m_axes.find(id);
        if (it == m_axes.end()) {
            reason = ContextRejection::AxisNotRegistered;
            outAxis = nullptr;
            return false;
        }

        // D. 校验通过
        outAxis = it->second.get();
        reason = ContextRejection::None;
        return true;
    }

    // --- 其他基础接口 ---
    GantryGroup& gantry() { return *m_gantry; }
    
    void setDriver(ISystemDriver* driver) { m_driver = driver; }
    ISystemDriver* driver() { return m_driver; }

private:
    std::unordered_map<AxisId, std::unique_ptr<Axis>> m_axes;
    std::unique_ptr<GantryGroup> m_gantry;
    ISystemDriver* m_driver = nullptr;
};
