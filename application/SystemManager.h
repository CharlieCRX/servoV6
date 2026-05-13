#pragma once
#include <string>
#include <map>
#include <memory>
#include "domain/entity/SystemContext.h"

class SystemManager {
public:
    SystemManager() = default;

    /**
     * @brief 创建一个新系统分组
     * @param[out] outReason 失败时的拒绝原因
     * @return true 成功；false 失败（通过 outReason 获取原因）
     */
    bool createGroup(const std::string& name, ContextRejection& outReason) {
        if (name.empty()) {
            outReason = ContextRejection::GroupNameInvalid;
            return false;
        }
        if (m_groups.find(name) != m_groups.end()) {
            outReason = ContextRejection::GroupAlreadyExists;
            return false;
        }
        m_groups[name] = std::make_unique<SystemContext>();
        outReason = ContextRejection::None;
        return true;
    }

    /**
     * @brief 获取分组实例（Try-Get 模式）
     * @param[out] outGroup 成功时指向分组实例
     * @param[out] outReason 失败时的拒绝原因
     * @return true 成功；false 失败
     */
    bool tryGetGroup(const std::string& name, 
                     SystemContext*& outGroup, 
                     ContextRejection& outReason) {
        if (name.empty()) {
            outReason = ContextRejection::GroupNameInvalid;
            outGroup = nullptr;
            return false;
        }
        auto it = m_groups.find(name);
        if (it == m_groups.end()) {
            outReason = ContextRejection::GroupNotFound;
            outGroup = nullptr;
            return false;
        }
        outGroup = it->second.get();
        outReason = ContextRejection::None;
        return true;
    }

    void removeGroup(const std::string& name) {
        m_groups.erase(name);
    }

private:
    std::map<std::string, std::unique_ptr<SystemContext>> m_groups;
};
