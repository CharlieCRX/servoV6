#pragma once

#include <vector>
#include "../domain/port/IGantryEventBus.h"

/**
 * @brief IGantryEventBus 的测试替身实现
 *
 * 记录所有发布的事件，供测试验证。
 *
 * 使用方式：
 *   FakeGantryEventBus bus;
 *   bus.publish(someEvent);
 *   ASSERT_EQ(bus.publishedEvents().size(), 1);
 */
class FakeGantryEventBus : public IGantryEventBus {
public:
    void publish(const GantryEvents::Event& event) override {
        m_published.push_back(event);
    }

    void publishAll(const std::vector<GantryEvents::Event>& events) override {
        for (const auto& e : events) {
            publish(e);
        }
    }

    /// 返回所有已发布的事件
    const std::vector<GantryEvents::Event>& publishedEvents() const {
        return m_published;
    }

    /// 清除已发布的历史记录（用于测试之间的重置）
    void clear() {
        m_published.clear();
    }

private:
    std::vector<GantryEvents::Event> m_published;
};
