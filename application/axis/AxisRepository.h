#pragma once

#include "entity/Axis.h"
#include "entity/AxisId.h"
#include <unordered_map>
#include <stdexcept>

class AxisRepository {
public:
    // 注册一个新的轴到系统中
    void registerAxis(AxisId id) {
        // 使用 emplace 避免额外的拷贝构造
        m_axes.emplace(id, Axis{});
    }

    // 获取轴实例的引用
    Axis& getAxis(AxisId id) {
        auto it = m_axes.find(id);
        if (it == m_axes.end()) {
            throw std::out_of_range("Requested AxisId is not registered in this Repository.");
        }
        return it->second;
    }

private:
    std::unordered_map<AxisId, Axis> m_axes;
};