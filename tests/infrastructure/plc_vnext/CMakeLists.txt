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
    # ============ Step 3：layout ============
    layout/test_axis_slot_register_layout.cpp
    layout/test_axis_topology_layout.cpp
    layout/test_gantry_layout.cpp
    # ============ Step 4：ReadPlanBuilder ============
    layout/test_read_plan_builder.cpp
    # ============ Step 5：transport / Fake ============
    transport/test_imodbus_client_contract.cpp
    transport/test_modbus_io_executor.cpp
    transport/test_connection_monitor.cpp
    # ============ Step 6：topology ============
    topology/test_topology_decoder.cpp
    topology/test_topology_validator.cpp
    topology/test_topology_reader.cpp
    topology/test_plc_topology_real_dump.cpp
    # ============ Step 7：telemetry ============
    telemetry/test_axis_snapshot_decoder.cpp
    telemetry/test_gantry_status_reader.cpp
    telemetry/test_snapshot_reader.cpp
    # ============ Step 8：command（单轴写路径，仅 Fake 离线 TDD）============
    command/test_plc_axis_command_writer.cpp
    command/test_command_write_policy.cpp
    layout/test_axis_slot_coil_layout.cpp     # 支撑 M 区线圈命令地址公式对拍
    # ============ Step 9：command（龙门请求，仅 Fake 离线 TDD）============
    command/test_plc_gantry_command_writer.cpp
    command/test_gantry_request.cpp
    # ============ Step 10：PlcRuntimeGateway（组合门面）============
    integration/test_plc_runtime_gateway.cpp
    # ============ Step 11：fake 收尾 ============
    fake/test_plc_fixture_builder.cpp
    fake/test_fake_plc_runtime_gateway.cpp
    # …… 每一步显式追加对应测试文件
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
