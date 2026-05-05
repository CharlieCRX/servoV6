#pragma once

#include "../event/GantryEvents.h"
#include <memory>

/**
 * @file IGantryEventPublisher.h
 * @brief 龙门事件发布端口接口
 *
 * 职责：
 *   定义领域事件发布的抽象契约。
 *   领域层通过此端口发布领域事件，而不关心具体发布机制
 *   （内存队列、消息队列、信号槽等）。
 *
 * 实现者：
 *   - infrastructure/event/InMemoryEventBus.h
 *   - 真实系统事件总线（生产环境）
 *
 * 使用方：
 *   - GantrySystem（聚合根发布事件）
 *   - 所有 Gantry*Service
 *
 * 约束映射：
 *   约束2  — 模型一致性（事件是解耦机制）
 *   约束21 — 事件驱动架构
 */
class IGantryEventPublisher {
public:
    virtual ~IGantryEventPublisher() = default;

    /**
     * @brief 发布领域事件
     *
     * @param event 要发布的领域事件
     */
    virtual void publish(const GantryEvents::Event& event) = 0;

    /**
     * @brief 清空已发布事件队列（用于测试验证）
     *
     * 某些实现（如 InMemoryEventBus）可能提供排空接口，
     * 默认实现为空操作。
     */
    virtual void flush() {}
};
