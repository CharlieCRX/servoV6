#include "Setting.h"
#include "Logger.h"
#include <fstream>
#include <nlohmann/json.hpp> // 需要 nlohmann/json 库

using json = nlohmann::json;

Setting& Setting::getInstance() {
    static Setting instance;
    return instance;
}

bool Setting::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("无法打开配置文件: {}", path);
        return false;
    }

    try {
        json j;
        file >> j;
        for (auto& [key, value] : j.items()) {
            config_[key] = value.get<std::string>();
        }
        LOG_INFO("配置文件加载成功: {}", path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("解析配置文件失败: {}", e.what());
        return false;
    }
}

bool Setting::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("无法写入配置文件: {}", path);
        return false;
    }

    try {
        json j(config_);
        file << j.dump(4);
        LOG_INFO("配置文件保存成功: {}", path);
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR("保存配置文件失败: {}", e.what());
        return false;
    }
}

void Setting::setWiFiSSID(const std::string& ssid) {
    config_["wifi_ssid"] = ssid;
    LOG_INFO("WiFi SSID 已设置为: {}", ssid);
}

void Setting::setWiFiPassword(const std::string& password) {
    config_["wifi_password"] = password;
    LOG_INFO("WiFi 密码已更新");
}

std::string Setting::getWiFiSSID() const {
    auto it = config_.find("wifi_ssid");
    return it != config_.end() ? it->second : "";
}

std::string Setting::getWiFiPassword() const {
    auto it = config_.find("wifi_password");
    return it != config_.end() ? it->second : "";
}

void Setting::set(const std::string& key, const std::string& value) {
    config_[key] = value;
    LOG_INFO("配置项 [{}] 已更新为: {}", key, value);
}

std::string Setting::get(const std::string& key) const {
    auto it = config_.find(key);
    return it != config_.end() ? it->second : "";
}
