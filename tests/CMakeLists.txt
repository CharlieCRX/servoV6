# tests/CMakeLists.txt

file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    test_*.cpp
)

foreach(test_file ${TEST_SOURCES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file} test_main.cpp)
    target_link_libraries(${test_name}
        PRIVATE
            Qt6::Test
            application_lib
            domain_lib
            adapters_protocol_lib     # 你可能需要 link 的模块
            utils_lib
    )
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
