#pragma once

#include <vector>
#include "../event/GantryEvents.h"

/**
 * @file IGantryEventBus.h
 * @brief 事件总线端口 — 领域层发布事件给应用层
 *
 * 领域层通过此端口将领域事件传递给应用层（如 QML 层）。
 * 实现在 Infrastructure 或 Application 层。
 *
 * 职责：
 *   - 将 GantryEvents::Event 传递给订阅者
 *   - 支持多个事件订阅者（UI 日志、状态机等）
 *   - 与 GantrySystem::drainEvents() 配合使用
 *
 * 典型使用模式：
 *   1. 每周期调用 GantrySystem::aggregateState()
 *   2. 调用 GantrySystem::drainEvents() 获取事件列表
 *   3. 逐条调用 publish(event) 发布到外部
 */
class IGantryEventBus {
public:
    virtual ~IGantryEventBus() = default;

    /**
     * @brief 发布一条领域事件
     * @param event 领域事件
     */
    virtual void publish(const GantryEvents::Event& event) = 0;

    /**
     * @brief 批量发布领域事件
     * @param events 事件列表
     */
    virtual void publishAll(const std::vector<GantryEvents::Event>& events) {
        for (const auto& e : events) {
            publish(e);
        }
    }
};
