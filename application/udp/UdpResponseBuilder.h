#pragma once

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <string>
#include <map>

// ═══════════════════════════════════════════════════════════════════
// UDP 回复构建工具 —— 统一构建 JSON 回复，保证格式一致性
// 参考: docs/architecture/UDP通讯层设计文档.md §3.5
// ═══════════════════════════════════════════════════════════════════

class UdpResponseBuilder {
public:
    /// @brief 构建成功回复
    /// @param request 原始请求 JSON 对象（用于回显请求字段）
    /// @param extraFields 额外字段 map（如 {"curr", 498.2}）
    /// @return JSON 字符串（Compact 格式）
    static std::string buildSuccess(const QJsonObject& request,
                                     const std::map<std::string, QJsonValue>& extraFields = {}) {
        QJsonObject reply = request;  // 复制所有请求字段（cmd, group, motor, target, ...）
        reply[QString::fromUtf8("result")] = 1;
        for (const auto& [key, val] : extraFields) {
            reply[QString::fromStdString(key)] = val;
        }
        QJsonDocument doc(reply);
        return doc.toJson(QJsonDocument::Compact).toStdString();
    }

    /// @brief 构建失败回复（当请求 JSON 可解析时使用）
    /// @param request 原始请求 JSON 对象（用于回显请求字段）
    /// @param errorMsg 错误描述
    /// @return JSON 字符串（Compact 格式）
    static std::string buildError(const QJsonObject& request,
                                   const std::string& errorMsg) {
        QJsonObject reply = request;
        reply[QString::fromUtf8("result")] = 0;
        reply[QString::fromUtf8("msg")] = QString::fromStdString(errorMsg);
        QJsonDocument doc(reply);
        return doc.toJson(QJsonDocument::Compact).toStdString();
    }

    /// @brief 构建最小错误回复（当 JSON 解析失败、无法获得原始请求字段时使用）
    /// @param cmd 命令码（若能提取）
    /// @param motor 电机 ID（若能提取）
    /// @param errorMsg 错误描述
    /// @return JSON 字符串（Compact 格式）
    static std::string buildRawError(int cmd, int motor, const std::string& errorMsg) {
        QJsonObject reply;
        reply[QString::fromUtf8("cmd")] = cmd;
        reply[QString::fromUtf8("motor")] = motor;
        reply[QString::fromUtf8("result")] = 0;
        reply[QString::fromUtf8("msg")] = QString::fromStdString(errorMsg);
        QJsonDocument doc(reply);
        return doc.toJson(QJsonDocument::Compact).toStdString();
    }
};