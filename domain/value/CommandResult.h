#pragma once

#include "Operability.h"
#include <string>

/**
 * @file CommandResult.h
 * @brief 命令结果结构体
 *
 * 封装 GantrySystem 中运动命令的执行结果。
 * 包含可行性判定（verdict）和人类可读的详情描述。
 *
 * 使用方：
 *   - GantrySystem::jog(), moveAbsolute(), moveRelative(), stop()
 *   - Application 层 Orchestrator
 *   - 测试断言
 */
namespace GantryValue {

struct CommandResult {
    Operability verdict;    ///< 操作可行性判定
    std::string detail;     ///< 详细信息（错误描述或成功信息）

    /// 默认构造：Allowed + 空详情
    CommandResult() : verdict(Operability::Allowed), detail() {}

    /// 值构造
    CommandResult(Operability v, std::string d)
        : verdict(v), detail(std::move(d)) {}

    /// 命令是否被接受（verdict == Allowed）
    bool isAccepted() const {
        return verdict == Operability::Allowed;
    }

    /// 工厂方法：创建一个表示"接受"的结果
    static CommandResult accept() {
        return CommandResult(Operability::Allowed, "Accepted");
    }

    /// 工厂方法：创建一个表示"拒绝"的结果
    static CommandResult reject(Operability reason, const std::string& detail) {
        return CommandResult(reason, detail);
    }
};

} // namespace GantryValue
