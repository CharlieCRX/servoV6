# tests/CMakeLists.txt

# 添加你的测试目标
add_executable(test_businesslogic test_businesslogic.cpp)
# 明确禁用 Qt 的自动 MOC 和 UIC 处理，因为这些是纯 C++ Google Test 文件
set_property(TARGET test_businesslogic PROPERTY AUTOMOC OFF)
set_property(TARGET test_businesslogic PROPERTY AUTOUIC OFF)
target_link_libraries(test_businesslogic PRIVATE gtest_main gmock corelib)

add_executable(test_motor test_motor.cpp
  mocks/MockMotor.h)
# 明确禁用 Qt 的自动 MOC 和 UIC 处理
set_property(TARGET test_motor PROPERTY AUTOMOC OFF)
set_property(TARGET test_motor PROPERTY AUTOUIC OFF)
target_link_libraries(test_motor PRIVATE gtest_main gmock corelib driverslib) # 确保 gmock 在这里，和之前的建议一致

add_executable(test_adapter test_adapter.cpp)
# 明确禁用 Qt 的自动 MOC 和 UIC 处理
set_property(TARGET test_adapter PROPERTY AUTOMOC OFF)
set_property(TARGET test_adapter PROPERTY AUTOUIC OFF)
target_link_libraries(test_adapter PRIVATE gtest_main gmock driverslib transportlib)



# 可选：注册测试
include(GoogleTest)
gtest_discover_tests(test_businesslogic)
gtest_discover_tests(test_motor)
gtest_discover_tests(test_adapter)
