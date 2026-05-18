#pragma once
#include "LogContext.h"
#include "TraceScope.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <iomanip>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <queue>
#include <thread>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <cstdint>

struct LoggerConfig {
    bool enableConsole = true;
    bool enableFile = false;
    LogLevel minConsoleLevel = LogLevel::INFO;   // 控制台最低级别，默认屏蔽 TRACE/DEBUG 噪音
    LogLevel minFileLevel    = LogLevel::TRACE;  // 文件最低级别，默认记录全部
    std::string logDirectory = "logs";
};

// ─── 日志条目：携带输出目标标记，解决 processQueue 无差别输出 bug ───
struct LogEntry {
    bool toConsole;
    bool toFile;
    std::string text;
};

// ─── 节流辅助：每 N 次调用输出 1 条 ───
struct Throttle {
    uint64_t counter = 0;
    uint64_t interval;
    explicit Throttle(uint64_t n) : interval(n) {}
    bool should() { return ++counter % interval == 0; }
};

class Logger {
public:
    static void init(const LoggerConfig& cfg) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_config = cfg;

        if (m_config.enableFile) {
            std::filesystem::create_directories(m_config.logDirectory);
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::stringstream ss;
            ss << m_config.logDirectory << "/servoV6_" 
               << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S") << ".log";

            if (m_fileStream.is_open()) m_fileStream.close();
            m_fileStream.open(ss.str(), std::ios::out | std::ios::app);
        }

        if (!m_running) {
            m_running = true;
            m_worker = std::thread(&Logger::processQueue);
        }
    }

    static void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_running = false;
        }
        m_cv.notify_all(); 
        if (m_worker.joinable()) {
            m_worker.join(); 
        }
        if (m_fileStream.is_open()) {
            m_fileStream.flush();
            m_fileStream.close();
        }
    }

    static void log(LogLevel level, LogLayer layer, const std::string& module, const std::string& msg) {
        // 级别过滤：控制台与文件各自独立
        bool toConsole = m_config.enableConsole && (level >= m_config.minConsoleLevel);
        bool toFile    = m_config.enableFile    && (level >= m_config.minFileLevel);
        if (!toConsole && !toFile) return;

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        LogContext ctx = TraceScope::current();

        std::stringstream ss;
        ss << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "." << std::setfill('0') << std::setw(3) << ms.count() << "]"
           << "[" << levelToString(level) << "]"
           << "[" << layerToString(layer) << "]"
           << "[" << module << "]"
           << "[" << ctx.group << "][" << ctx.axis << "][" << ctx.traceId << "] "
           << msg << "\n";

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push({toConsole, toFile, ss.str()});
        }
        m_cv.notify_one(); 
    }

private:
    inline static LoggerConfig m_config;
    inline static std::ofstream m_fileStream;
    
    inline static std::mutex m_mutex;
    inline static std::condition_variable m_cv;
    inline static std::queue<LogEntry> m_queue;
    inline static std::thread m_worker;
    inline static std::atomic<bool> m_running{false};

    static void processQueue() {
        while (true) {
            std::queue<LogEntry> localQueue;
            
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [] { return !m_queue.empty() || !m_running; });

                if (m_queue.empty() && !m_running) {
                    break;
                }

                m_queue.swap(localQueue);
            }

            bool hasFile = m_config.enableFile && m_fileStream.is_open();
            
            while (!localQueue.empty()) {
                const LogEntry& entry = localQueue.front();
                
                // 🔧 修复：按 toConsole / toFile 分别输出
                if (entry.toConsole && m_config.enableConsole) {
                    std::cout << entry.text;
                }
                if (entry.toFile && hasFile) {
                    m_fileStream << entry.text;
                }
                localQueue.pop();
            }

            if (m_config.enableConsole) std::cout.flush();
            if (hasFile) m_fileStream.flush();
        }
    }

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

// ─── 日志宏 ───
#define LOG_TRACE(layer, module, msg)   Logger::log(LogLevel::TRACE, layer, module, msg)
#define LOG_INFO(layer, module, msg)    Logger::log(LogLevel::INFO, layer, module, msg)
#define LOG_DEBUG(layer, module, msg)   Logger::log(LogLevel::DEBUG, layer, module, msg)
#define LOG_WARN(layer, module, msg)    Logger::log(LogLevel::WARN, layer, module, msg)
#define LOG_ERROR(layer, module, msg)   Logger::log(LogLevel::ERROR, layer, module, msg)
#define LOG_SUMMARY(layer, module, msg) Logger::log(LogLevel::SUMMARY, layer, module, msg)

// 每 N 次调用输出 1 条
#define LOG_TRACE_EVERY_N(n, layer, module, msg) \
    do { \
        static Throttle _throttle(n); \
        if (_throttle.should()) { \
            Logger::log(LogLevel::TRACE, layer, module, msg); \
        } \
    } while(0)
