#pragma once
#include "LogContext.h"
#include "TraceScope.h"
#include <iostream>
#include <chrono>
#include <iomanip>

class Logger {
public:
    static void log(LogLevel level, LogLayer layer, const std::string& module, const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        LogContext ctx = TraceScope::current();

        // 🌟 修复点：将 "%T" 改为 "%H:%M:%S"
        std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
                  << "[" << levelToString(level) << "]"
                  << "[" << layerToString(layer) << "]"
                  << "[" << module << "]"
                  << "[" << ctx.group << "][" << ctx.axis << "][" << ctx.traceId << "] "
                  << msg << std::endl;
    }

private:
    static std::string levelToString(LogLevel l) {
        switch(l) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO";
            case LogLevel::WARN:  return "WARN";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::SUMMARY:return "SUMMARY";
            default: return "UNKNOWN";
        }
    }
    static std::string layerToString(LogLayer l) {
        switch(l) {
            case LogLayer::UI:  return "UI";
            case LogLayer::APP: return "APP";
            case LogLayer::DOM: return "DOM";
            case LogLayer::HAL: return "HAL";
            default: return "UNK";
        }
    }
};

// ==========================================
// 🚀 开发者 API (日志宏)
// ==========================================

#define LOG_INFO(layer, module, msg)  Logger::log(LogLevel::INFO, layer, module, msg)
#define LOG_DEBUG(layer, module, msg) Logger::log(LogLevel::DEBUG, layer, module, msg)
#define LOG_WARN(layer, module, msg)  Logger::log(LogLevel::WARN, layer, module, msg)
#define LOG_ERROR(layer, module, msg) Logger::log(LogLevel::ERROR, layer, module, msg)
#define LOG_SUMMARY(layer, module, msg) Logger::log(LogLevel::SUMMARY, layer, module, msg)

// 🌟 高频控制宏：每 N 次才打印一次 (极其适合 FakePLC Tick)
#define LOG_TRACE_EVERY_N(n, layer, module, msg) \
    do { \
        static int _log_counter_##__LINE__ = 0; \
        if (_log_counter_##__LINE__++ % (n) == 0) { \
            Logger::log(LogLevel::TRACE, layer, module, msg); \
        } \
    } while(0)


#define LOG_TRACE(layer, module, msg) Logger::log(LogLevel::TRACE, layer, module, msg)