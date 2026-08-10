# ==========================================
# plc_vnext 独立测试通道（Step 0 搭建）
# ==========================================
# 不复用旧 unit_tests；每个 Step 在此显式追加对应测试文件（禁止 GLOB_RECURSE）。

add_executable(plc_vnext_tests
    test_plc_vnext_smoke.cpp            # Step 0：构建/测试通道冒烟验证
    # ============ Step 1：contracts ============
    contracts/test_plc_axis_slot.cpp
    contracts/test_plc_group_index.cpp
    contracts/test_communication_result.cpp
    contracts/test_read_result.cpp
    # ============ Step 2：codec ============
    codec/test_register_codec.cpp
    codec/test_endian_policy.cpp
    codec/test_raw_register_block.cpp
    # …… 每一步显式追加对应测试文件（如 Step 3 起追加 layout/test_axis_slot_register_layout.cpp 等）
)

target_include_directories(plc_vnext_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(plc_vnext_tests
    PRIVATE
        plc_vnext
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
)

# TEST_PREFIX 使 ctest 以 "plc_vnext." 命名空间发现具体测试，便于 -R "^plc_vnext\." 过滤
gtest_discover_tests(plc_vnext_tests TEST_PREFIX "plc_vnext.")
