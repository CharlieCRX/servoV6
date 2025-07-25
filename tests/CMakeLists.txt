# tests/CMakeLists.txt

# --- 定义一个公共变量，用于存放所有测试共享的源文件 ---
# 这样可以避免在每个测试可执行文件中重复添加公共源文件。
set(TEST_COMMON_SOURCES
    test_main.cpp # 新增：自定义的测试入口文件，包含 main 函数和日志初始化
)

# --- test_businesslogic ---
add_executable(test_businesslogic
    ${TEST_COMMON_SOURCES}
    test_businesslogic.cpp
    mocks/MockMotor.h
)
set_property(TARGET test_businesslogic PROPERTY AUTOMOC OFF AUTOUIC OFF)
target_link_libraries(test_businesslogic PRIVATE
  gtest          # 链接 gtest 而不是 gtest_main
  gmock
  utilslib       # 日志库
  corelib        # 核心业务逻辑库
)

# --- test_motor ---
add_executable(test_motor
    ${TEST_COMMON_SOURCES}
    test_motor.cpp
)
set_property(TARGET test_motor PROPERTY AUTOMOC OFF AUTOUIC OFF)
target_link_libraries(test_motor PRIVATE
  Qt6::Core      # 注意：如果你的 corelib 或 driverslib 依赖 Qt6::Core，那么所有链接它们的测试也需要。
  gtest
  gmock
  utilslib
  corelib
  driverslib
)

# --- 注册测试 (不变) ---
include(GoogleTest)
gtest_discover_tests(test_businesslogic)
gtest_discover_tests(test_motor)
