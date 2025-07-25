// tests/test_main.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <Logger.h> // 包含你的 Logger 单例头文件
#include <string>
#include <filesystem> // 用于创建日志目录

namespace fs = std::filesystem;

// 可以在这里定义一个全局的测试监听器，如果需要的话
// class CustomTestEventListener : public ::testing::EmptyTestEventListener {
// public:
//     void OnTestStart(const ::testing::TestInfo& test_info) override {
//         LOG_INFO("Running test: {}.{}", test_info.test_suite_name(), test_info.name());
//     }
//     void OnTestEnd(const ::testing::TestInfo& test_info) override {
//         if (test_info.result()->Failed()) {
//             LOG_ERROR("Test failed: {}.{}", test_info.test_suite_name(), test_info.name());
//         } else {
//             LOG_INFO("Test passed: {}.{}", test_info.test_suite_name(), test_info.name());
//         }
//     }
// };

int main(int argc, char** argv) {
    // 1. 初始化 Google Test 框架
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::InitGoogleMock(&argc, argv); // 如果使用了 Google Mock，也需要初始化

    // 2. 初始化你的日志系统
    // 定义日志文件路径
    std::string log_dir = "test_logs";
    fs::create_directories(log_dir); // 确保日志目录存在

    std::string log_file_path = log_dir + "/tests_log.txt";
    std::string log_level = "debug"; // 在测试中通常使用debug级别，以便记录更多信息

    Logger::getInstance().init(log_file_path, log_level, /*enableConsoleOutput=*/false);

    // 可以在这里添加一个自定义的测试事件监听器，用于记录测试的开始和结束
//    ::testing::TestEventListeners& listeners = ::testing::UnitTest::GetInstance()->listeners();
//    listeners.Append(new CustomTestEventListener());

    // 3. 运行所有测试
    int result = RUN_ALL_TESTS();

    // 4. (可选) 确保所有日志都已写入文件
    // 虽然 Logger::init 中设置了 flush_on(info)，但为了确保测试结束时所有日志都写入，
    // 尝试注释掉这一行，看看是否解决问题
    spdlog::shutdown();

    return result;
}
