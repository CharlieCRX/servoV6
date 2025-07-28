set(TEST_COMMON_SOURCES
    test_main.cpp
)

add_executable(test_businesslogic
    ${TEST_COMMON_SOURCES}
    test_businesslogic.cpp
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

include(GoogleTest)
gtest_discover_tests(test_businesslogic)
#gtest_discover_tests(test_motor)
