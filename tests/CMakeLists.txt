include(GoogleTest)

add_executable(unit_tests

    infrastructure/test_system_integration.cpp
    infrastructure/test_fake_plc.cpp

    application/policy/test_auto_rel_move_orchestrator.cpp
    application/policy/test_auto_abs_move_orchestrator.cpp
    application/policy/test_jog_orchestrator.cpp
    application/policy/test_gantry_orchestrator.cpp

    application/test_move_absolute_usecase.cpp
    application/test_move_relative_usecase.cpp
    application/test_stop_usecase.cpp
    application/test_jog_usecase.cpp
    application/test_enable_usecase.cpp
    application/test_system_manager.cpp
    application/safety/test_emergency_stop_usecase.cpp

    domain/test_system_context.cpp
    domain/gantry/test_gantry_power_controller.cpp
    domain/gantry/test_gantry_coupling_controller.cpp
    domain/gantry/test_gantry_coupling_state.cpp
    domain/safety/test_emergency_stop_controller.cpp


    domain/test_axis.cpp


    # presentation/viewmodel/test_axis_viewmodel_core.cpp
)

target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(unit_tests
    PRIVATE
        domain
        application
        # presentation
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(unit_tests
    PROPERTIES
        AUTOMOC OFF
        AUTORCC OFF
        AUTOUIC OFF
)

gtest_discover_tests(unit_tests)
