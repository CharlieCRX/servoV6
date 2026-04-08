include(GoogleTest)

add_executable(unit_tests
    domain/test_axis.cpp
    application/test_jog_usecase.cpp
)

target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(unit_tests
    PRIVATE
        domain
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(unit_tests)