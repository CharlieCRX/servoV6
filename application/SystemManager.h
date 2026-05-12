#pragma once

#include <string>
#include <map>
#include <memory>
#include "domain/entity/SystemContext.h"

/**
 * @brief SystemManager - 分组管理器（应用层单例或全局容器）
 * 负责管理多个 SystemContext 实例的生命周期与路由
 */
class SystemManager {
public:
    SystemManager() = default;

    /**
     * @brief 创建一个新系统分组
     * @return true 创建成功；false 已存在同名分组
     */
    bool createGroup(const std::string& name) {
        if (m_groups.find(name) != m_groups.end()) {
            return false;
        }
        // 使用 unique_ptr 自动管理 SystemContext 的生命周期
        m_groups[name] = std::make_unique<SystemContext>();
        return true;
    }

    /**
     * @brief 获取分组实例
     * @param name 分组唯一名称
     * @return SystemContext* 成功返回指针；失败返回 nullptr
     */
    SystemContext* getGroup(const std::string& name) {
        auto it = m_groups.find(name);
        if (it == m_groups.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    /**
     * @brief 移除分组
     */
    void removeGroup(const std::string& name) {
        m_groups.erase(name);
    }

private:
    // 使用 std::map 保证查找效率并支持按名称排序
    std::map<std::string, std::unique_ptr<SystemContext>> m_groups;
};