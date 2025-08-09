#ifndef SETTING_H
#define SETTING_H

#pragma once
#include <string>
#include <unordered_map>

class Setting {
public:
    static Setting& getInstance();

    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    void setWiFiSSID(const std::string& ssid);
    void setWiFiPassword(const std::string& password);
    std::string getWiFiSSID() const;
    std::string getWiFiPassword() const;

    void set(const std::string& key, const std::string& value);
    std::string get(const std::string& key) const;

private:
    Setting() = default;
    ~Setting() = default;

    Setting(const Setting&) = delete;
    Setting& operator=(const Setting&) = delete;

    std::unordered_map<std::string, std::string> config_;
};


#endif // SETTING_H
