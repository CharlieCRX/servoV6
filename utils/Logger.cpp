// core/Logger.cpp
#include "Logger.h" // 包含Logger类的定义
#include <spdlog/spdlog.h> // spdlog核心库
#include <spdlog/sinks/basic_file_sink.h> // 用于文件输出的sink
#include <spdlog/sinks/stdout_color_sinks.h> // 用于控制台输出的sink（带颜色）
#include <iostream> // 用于在日志系统初始化失败时输出到cerr
#include <vector> // 用于存储 sinks

// Logger的私有构造函数
// 初始化initialized_标记为false
Logger::Logger() : initialized_(false) {}

// Logger的析构函数
// 在Logger实例销毁时，清理spdlog的所有logger
Logger::~Logger() {
    spdlog::drop_all(); // 释放所有logger，避免内存泄漏
}

// 获取Logger单例的唯一实例
Logger& Logger::getInstance() {
    static Logger instance; // 静态局部变量，保证线程安全地创建单例
    return instance;
}

// 初始化日志系统
void Logger::init(const std::string& logFilePath, const std::string& logLevel, bool enableConsoleOutput) {
    // 如果日志系统已经初始化过，则直接返回，避免重复初始化
    if (initialized_) {
        return;
    }

    try {
        // 创建文件输出 sink
        // logFilePath: 日志文件的完整路径
        // true: 设置为追加模式，每次程序启动都会在文件末尾添加新日志，而不是覆盖（根据你上一个反馈修正为true，如果需要覆盖请改为false）
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true);
        // 设置文件日志的格式（包含详细信息）
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] (%s:%#) %! - %v");

        // 创建一个 sink 列表
        std::vector<spdlog::sink_ptr> sinks;
        sinks.push_back(file_sink); // 文件 sink 总是被添加

        // 根据 enableConsoleOutput 参数决定是否添加控制台 sink
        if (enableConsoleOutput) {
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            // 设置控制台日志的格式（更简洁，只显示级别和消息）
            // 移除了文件名、行号和函数名，避免与 Google Test 输出冲突
            console_sink->set_pattern("[%^%l%$] %v");
            sinks.push_back(console_sink);
        }

        // 使用 sinks 列表初始化 logger
        logger_ = std::make_shared<spdlog::logger>("main_logger", sinks.begin(), sinks.end());

        // 根据传入的字符串设置日志级别
        spdlog::level::level_enum level = spdlog::level::info; // 默认 info
        if (logLevel == "trace") {
            level = spdlog::level::trace;
        } else if (logLevel == "debug") {
            level = spdlog::level::debug;
        } else if (logLevel == "info") {
            level = spdlog::level::info;
        } else if (logLevel == "warn") {
            level = spdlog::level::warn;
        } else if (logLevel == "error") {
            level = spdlog::level::err;
        } else if (logLevel == "critical") {
            level = spdlog::level::critical;
        } else {
            std::cerr << "Warning: Unknown log level '" << logLevel << "'. Defaulting to 'info'." << std::endl;
        }
        logger_->set_level(level);
        // 确保全局 spdlog 级别也被设置，影响所有 logger
        spdlog::set_level(level);

        // 设置在 INFO 级别及以上时，立即将日志刷新到输出
        logger_->flush_on(spdlog::level::info);

        // 将这个logger设置为spdlog的默认logger
        spdlog::set_default_logger(logger_);

        // 设置spdlog的错误处理函数
        spdlog::set_error_handler([](const std::string& msg) {
            std::cerr << "SPDLOG ERROR: " << msg << std::endl;
        });

        initialized_ = true; // 标记日志系统已成功初始化

        // 打印一条日志，确认日志系统初始化成功及其日志文件路径
        LOG_INFO("Logger initialized successfully. Log file: {}", logFilePath);

    } catch (const spdlog::spdlog_ex& ex) {
        // 如果spdlog初始化过程中发生异常（如文件无法创建），捕获并打印错误
        std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        initialized_ = false; // 标记初始化失败
    }
}

// 获取当前Logger实例持有的spdlog::logger对象
std::shared_ptr<spdlog::logger> Logger::getLogger() const {
    return logger_;
}
