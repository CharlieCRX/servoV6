// core/Logger.h
#ifndef LOGGER_H
#define LOGGER_H

#include <memory> // For std::shared_ptr
#include <string>

// 需要包含 spdlog/common.h 来使用 spdlog::source_loc 和 SPDLOG_FUNCTION
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

// Forward declare spdlog::logger to avoid including its header in the public interface
namespace spdlog {
class logger;
}

class Logger {
public:
    // 获取单例实例
    static Logger& getInstance();

    // 初始化日志系统：设置日志文件路径和级别
    // logLevel参数应为"trace", "debug", "info", "warn", "error", "critical"之一
    // enableConsoleOutput: 是否同时将日志输出到控制台，默认为 false
    void init(const std::string& logFilePath, const std::string& logLevel = "info", bool enableConsoleOutput = false);

    // 获取日志器
    std::shared_ptr<spdlog::logger> getLogger() const;

private:
    Logger(); // 私有构造函数，实现单例
    ~Logger(); // 析构函数，用于清理spdlog资源

    // 禁用拷贝构造和赋值，确保单例模式
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::shared_ptr<spdlog::logger> logger_; // spdlog日志器实例
    bool initialized_; // 标记是否已经初始化过
};

// 方便的宏，用于直接在代码中调用日志
// 这些宏会自动捕获调用它们的文件名、行号和函数名，并传递给spdlog
#define LOG_TRACE(...) Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::trace, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::debug, __VA_ARGS__)
#define LOG_INFO(...)  Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::info, __VA_ARGS__)
#define LOG_WARN(...)  Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::warn, __VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::err, __VA_ARGS__)
#define LOG_CRITICAL(...) Logger::getInstance().getLogger()->log(spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}, spdlog::level::critical, __VA_ARGS__)

#endif // LOGGER_H
