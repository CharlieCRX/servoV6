#pragma once
#include <string>

// 日志层级枚举
enum class LogLayer { UI, APP, DOM, HAL };

// 日志级别枚举
enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, SUMMARY };

// 日志上下文实体
struct LogContext {
    std::string group = "N/A";
    std::string axis = "N/A";
    std::string traceId = "N/A";
};