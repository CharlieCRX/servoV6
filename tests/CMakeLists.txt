# tests/CMakeLists.txt

# 1. 告诉 CMake 这是一个测试项目，并引入 GTest 辅助模块
include(GoogleTest)

# 2. 定义测试执行文件
# 路径 domain/test_axis.cpp 是相对于当前 tests 目录的，正确！
add_executable(unit_tests
    domain/test_axis.cpp
)

# 3. 链接库
# 链接 domain：自动获得 Axis.h 的搜索路径（因为你写了 PUBLIC）
# 链接 GTest：这里建议写成 GTest::gtest_main，它会自动处理 main 函数
target_link_libraries(unit_tests
    PRIVATE
        domain
        GTest::gtest
        GTest::gtest_main
)

# 4. 自动扫描测试用例（关键！）
# 使用这行代码后，Qt Creator 的“测试结果”窗口能看到具体的每一个 TEST_F 项
gtest_discover_tests(unit_tests)
