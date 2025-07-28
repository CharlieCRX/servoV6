set(TEST_COMMON_SOURCES
    test_main.cpp
)

add_executable(test_businesslogic
    ${TEST_COMMON_SOURCES}
    test_businesslogic.cpp
    mocks/MockServoAdapters.h
)
target_link_libraries(test_businesslogic PRIVATE
  gtest
  gmock
  utilslib
  corelib
)
target_precompile_headers(test_businesslogic PRIVATE <gtest/gtest.h> <gmock/gmock.h>)
set_property(TARGET test_businesslogic PROPERTY AUTOMOC OFF AUTOUIC OFF)

add_executable(test_motor
    ${TEST_COMMON_SOURCES}
    test_motor.cpp
)
target_link_libraries(test_motor PRIVATE
  gtest
  gmock
  utilslib
  corelib
  driverslib
)
target_precompile_headers(test_motor PRIVATE <gtest/gtest.h> <gmock/gmock.h>)
set_property(TARGET test_motor PROPERTY AUTOMOC OFF AUTOUIC OFF)



# test_adapter 可执行文件 (新增)
# 用于测试核心适配器层 (LinearServoAdapter, RotaryServoAdapter) 的单位转换和行为
add_executable(test_adapter
    ${TEST_COMMON_SOURCES}
    test_adapter.cpp # 这个是你要添加的适配器测试文件
)
target_link_libraries(test_adapter PRIVATE
  gtest
  gmock
  utilslib          # 适配器可能依赖日志
  corelib           # 适配器是核心库的一部分，需要链接 corelib
)
# 添加 mocks 目录，让测试文件能找到 mocks 的头文件
target_include_directories(test_adapter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/mocks)

target_precompile_headers(test_adapter PRIVATE <gtest/gtest.h> <gmock/gmock.h>)
set_property(TARGET test_adapter PROPERTY AUTOMOC OFF AUTOUIC OFF)

include(GoogleTest)
gtest_discover_tests(test_businesslogic)
#gtest_discover_tests(test_motor)
