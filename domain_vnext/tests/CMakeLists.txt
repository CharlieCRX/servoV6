# ==========================================
# domain_vnext 独立测试通道（P1 model + P2 state）
# ==========================================
# 不复用旧 unit_tests；每个 Phase 在此显式追加对应测试文件（禁止 GLOB_RECURSE）。
#
# gtest_discover_tests 来自 CMake 内置模块 GoogleTest；本子目录由根
# CMakeLists.txt 的 add_subdirectory(domain_vnext) 直接引入，并非在
# tests/CMakeLists.txt（那里才 include(GoogleTest)）之下，故需在此显式引入。

include(GoogleTest)

add_executable(domain_vnext_tests
    # ============ P1 model ============
    model/test_axis_function.cpp
    model/test_axis_key.cpp
    model/test_axis_state.cpp
    model/test_axis_parameter_set.cpp
    model/test_axis_command.cpp
    model/test_gantry_status.cpp
    model/test_gantry_param.cpp
    model/test_safety_state.cpp
    # ============ P2 state ============
    state/test_command_outbox.cpp
    state/test_axis_state_machine.cpp
    state/test_safety_state_machine.cpp
    state/test_gantry_coupling_state_machine.cpp
    # ============ P3 system（后续追加）============
    # …… 每个 Phase 在此显式追加对应测试文件
)

target_include_directories(domain_vnext_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(domain_vnext_tests
    PRIVATE
        domain_vnext
        GTest::gtest
        GTest::gtest_main
)

# TEST_PREFIX 使 ctest 以 "domain_vnext." 命名空间发现具体测试，便于 -R "^domain_vnext\." 过滤
gtest_discover_tests(domain_vnext_tests TEST_PREFIX "domain_vnext.")
