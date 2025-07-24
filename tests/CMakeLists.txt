# tests/CMakeLists.txt

# 添加你的测试目标
add_executable(test_businesslogic test_businesslogic.cpp)
target_link_libraries(test_businesslogic PRIVATE gtest_main corelib)

add_executable(test_motor test_motor.cpp)
target_link_libraries(test_motor PRIVATE gtest_main corelib driverslib)

add_executable(test_adapter test_adapter.cpp)
target_link_libraries(test_adapter PRIVATE gtest_main driverslib transportlib)

# 可选：注册测试
include(GoogleTest)
gtest_discover_tests(test_businesslogic)
gtest_discover_tests(test_motor)
gtest_discover_tests(test_adapter)
