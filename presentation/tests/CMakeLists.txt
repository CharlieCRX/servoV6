# ==========================================
# presentation/tests —— Phase 2：UiControlAdapter 与真实 MotionControlService 集成测试
# ==========================================
# 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 补验：
#   - 真实 MotionControlService 快照 -> UiControlAdapter -> QML 可见状态（connected/
#     axis 位置/locked/gantry 状态）的实时投影；
#   - UiControlAdapter(service) 调用 refresh() 后 Q_PROPERTY / QVariantMap 字段正确。
# 本 target 依赖 Qt（QObject/QVariant/QQmlEngine）+ presentation + application_vnext，
# 与无 Qt 的 application_vnext_tests 分开，独立通道。
# ==========================================
include(GoogleTest)

add_executable(presentation_tests
    test_ui_control_adapter.cpp
)

target_include_directories(presentation_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/application_vnext/tests   # fake/FakeControlRuntime.h（纯头）
)

target_link_libraries(presentation_tests
    PRIVATE
        Qt6::Core
        Qt6::Qml
        presentation
        application_vnext
        domain_vnext
        plc_vnext
        GTest::gtest
        GTest::gtest_main
)

# 由 Qt6_DIR 推导 Qt 运行时 bin 目录，供测试运行（ctest 需 Qt DLL 在 PATH）。
# Qt6_DIR 形如 <prefix>/lib/cmake/Qt6，向上三级即 <prefix>，bin 在其下。
get_filename_component(QT6_ROOT_DIR "${Qt6_DIR}/../../.." ABSOLUTE)
set(QT6_RUNTIME_BIN "${QT6_ROOT_DIR}/bin")

gtest_discover_tests(presentation_tests TEST_PREFIX "presentation."
    PROPERTIES ENVIRONMENT "PATH=${QT6_RUNTIME_BIN};$ENV{PATH}")

