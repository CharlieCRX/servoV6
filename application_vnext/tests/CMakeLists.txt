# ==========================================
# application_vnext 独立测试通道（P5 app）
# ==========================================
# 不复用旧 unit_tests；每个 Phase 在此显式追加对应测试文件（禁止 GLOB_RECURSE）。
# gtest_discover_tests 来自 CMake 内置模块 GoogleTest；本子目录由
# 根 CMakeLists.txt 的 add_subdirectory(application_vnext) 直接引入，故显式引入。

include(GoogleTest)

add_executable(application_vnext_tests
    # ============ P5 app ============
    test_system_manager_vnext.cpp
    # ============ 阶段 2：只读影子运行 ============
    test_shadow_run_assessor.cpp
    # ============ 阶段3/后续：运动策略层（Abs/Rel/Jog/AxisMotionApi）============
    test_axis_motion_policy.cpp
    # ============ 龙门生命周期 + 运动许可守卫 + PowerOwnership ============
    test_gantry_policy.cpp
    # ============ Phase 0：统一控制协调层——类型与命令模型 ============
    test_control_command.cpp
    test_control_state_store.cpp
    test_required_resources.cpp
    # ============ Phase 1/3：MotionControlService 骨架 + 仲裁 + 会话 tick ============
    test_motion_control_service.cpp
    # ============ Phase 2：状态快照投影（先接 UI 只读）============
    test_ui_projection.cpp
)

target_include_directories(application_vnext_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(application_vnext_tests
    PRIVATE
        application_vnext
        domain_vnext
        plc_vnext
        GTest::gtest
        GTest::gtest_main
)

# TEST_PREFIX 使 ctest 以 "application_vnext." 命名空间发现具体测试，便于过滤。
gtest_discover_tests(application_vnext_tests TEST_PREFIX "application_vnext.")
