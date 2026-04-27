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

struct LoggerConfig {
    bool enableConsole = true;
    bool enableFile = false;
    std::string logDirectory = "logs";
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
            m_queue.push(ss.str());
        }
        m_cv.notify_one(); 
    }

private:
    inline static LoggerConfig m_config;
    inline static std::ofstream m_fileStream;
    
    inline static std::mutex m_mutex;
    inline static std::condition_variable m_cv;
    inline static std::queue<std::string> m_queue;
    inline static std::thread m_worker;
    inline static std::atomic<bool> m_running{false};

    // 🌟 核心升级：双缓冲批量处理循环
    static void processQueue() {
        while (true) {
            std::queue<std::string> localQueue; // 本地缓冲队列
            
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [] { return !m_queue.empty() || !m_running; });

                if (m_queue.empty() && !m_running) {
                    break;
                }

                // 🌟 神之一手：瞬间把共享队列里的所有数据“转移”到本地队列
                // std::queue::swap 是指针交换，耗时几乎为 0 (O(1)复杂度)
                // 这样能立刻释放锁，主线程完全不需要等待！
                m_queue.swap(localQueue);
            } // <--- 锁在这里已经被释放了

            // 🌟 批量处理开始（不持有任何锁）
            bool hasFile = m_config.enableFile && m_fileStream.is_open();
            
            while (!localQueue.empty()) {
                const std::string& logLine = localQueue.front();
                
                if (m_config.enableConsole) {
                    std::cout << logLine;
                }
                if (hasFile) {
                    m_fileStream << logLine;
                }
                localQueue.pop();
            }

            // 🌟 核心修复：积攒了一批日志后，只执行一次 flush！
            if (m_config.enableConsole) {
                std::cout.flush(); // 强制终端流畅输出当前批次
            }
            if (hasFile) {
                m_fileStream.flush(); // 强制写入磁盘
            }
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

// ... 宏定义保持不变 ...
#define LOG_TRACE(layer, module, msg) Logger::log(LogLevel::TRACE, layer, module, msg)
#define LOG_INFO(layer, module, msg)  Logger::log(LogLevel::INFO, layer, module, msg)
#define LOG_DEBUG(layer, module, msg) Logger::log(LogLevel::DEBUG, layer, module, msg)
#define LOG_WARN(layer, module, msg)  Logger::log(LogLevel::WARN, layer, module, msg)
#define LOG_ERROR(layer, module, msg) Logger::log(LogLevel::ERROR, layer, module, msg)
#define LOG_SUMMARY(layer, module, msg) Logger::log(LogLevel::SUMMARY, layer, module, msg)

#define LOG_TRACE_EVERY_N(n, layer, module, msg) \
    do { \
        static int _log_counter = 0; \
        if (_log_counter++ % (n) == 0) { \
            Logger::log(LogLevel::TRACE, layer, module, msg); \
        } \
    } while(0)

