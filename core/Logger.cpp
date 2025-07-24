// core/Logger.cpp
#include "Logger.h" // 包含Logger类的定义
#include <spdlog/spdlog.h> // spdlog核心库
#include <spdlog/sinks/basic_file_sink.h> // 用于文件输出的sink
#include <spdlog/sinks/stdout_color_sinks.h> // 用于控制台输出的sink（带颜色）
#include <iostream> // 用于在日志系统初始化失败时输出到cerr

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
void Logger::init(const std::string& logFilePath, const std::string& logLevel) {
    // 如果日志系统已经初始化过，则直接返回，避免重复初始化
    if (initialized_) {
        return;
    }

    try {
        // 创建文件输出 sink
        // logFilePath: 日志文件的完整路径
        // false: 设置为追加模式，每次程序启动都会在文件末尾添加新日志，而不是覆盖
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, false);
        // 设置文件日志的格式
        // [%Y-%m-%d %H:%M:%S.%e] : 年-月-日 时:分:秒.毫秒
        // [%l]                    : 日志级别 (如 info, debug)
        // (%s:%#)                 : 源文件名:行号
        // %!                      : 函数名
        // %v                      : 实际的日志消息内容
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] (%s:%#) %! - %v");

        // 创建控制台输出 sink (带颜色，便于在终端查看)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        // 设置控制台日志的格式
        // [%^%l%$]                : 带颜色的日志级别
        // (%s:%#)                 : 源文件名:行号
        // %!                      : 函数名
        // %v                      : 实际的日志消息内容
        console_sink->set_pattern("[%^%l%$] (%s:%#) %! - %v");

        // 将文件sink和控制台sink添加到主logger中
        logger_ = std::make_shared<spdlog::logger>("main_logger", spdlog::sinks_init_list{file_sink, console_sink});

        // 根据传入的字符串设置日志级别
        if (logLevel == "trace") {
            logger_->set_level(spdlog::level::trace);
        } else if (logLevel == "debug") {
            logger_->set_level(spdlog::level::debug);
        } else if (logLevel == "info") {
            logger_->set_level(spdlog::level::info);
        } else if (logLevel == "warn") {
            logger_->set_level(spdlog::level::warn);
        } else if (logLevel == "error") {
            logger_->set_level(spdlog::level::err);
        } else if (logLevel == "critical") {
            logger_->set_level(spdlog::level::critical);
        } else {
            logger_->set_level(spdlog::level::info); // 如果级别字符串无效，默认为info级别
            std::cerr << "Warning: Unknown log level '" << logLevel << "'. Defaulting to 'info'." << std::endl;
        }

        // 设置在INFO级别及以上时，立即将日志刷新到输出（避免日志丢失，但可能略微影响性能）
        logger_->flush_on(spdlog::level::info);
        // 将这个logger设置为spdlog的默认logger，这样可以直接使用spdlog::info()等全局函数（尽管我们定义了LOG_宏）
        spdlog::set_default_logger(logger_);
        // 设置spdlog的错误处理函数，当spdlog内部发生错误时会调用
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
