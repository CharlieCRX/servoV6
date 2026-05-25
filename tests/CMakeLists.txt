include(GoogleTest)

add_executable(unit_tests

    # ================ First-party tests ================
    # presentation/viewmodel/test_axis_viewmodel_core.cpp
    # presentation/viewmodel/test_error_translator.cpp
    # presentation/viewmodel/test_gantry_viewmodel.cpp
 
    # infrastructure/test_system_integration.cpp
    # infrastructure/test_fake_plc.cpp

    # application/policy/test_auto_rel_move_orchestrator.cpp
    # application/policy/test_auto_abs_move_orchestrator.cpp
    # application/policy/test_jog_orchestrator.cpp
    # application/policy/test_gantry_orchestrator.cpp

    # application/test_move_absolute_usecase.cpp
    # application/test_move_relative_usecase.cpp
    # application/test_stop_usecase.cpp
    # application/test_jog_usecase.cpp
    # application/test_enable_usecase.cpp
    # application/test_system_manager.cpp
    # application/safety/test_emergency_stop_usecase.cpp

    # domain/test_system_context.cpp
    # domain/gantry/test_gantry_power_controller.cpp
    # domain/gantry/test_gantry_coupling_controller.cpp
    # domain/gantry/test_gantry_coupling_state.cpp
    # domain/safety/test_emergency_stop_controller.cpp

    # domain/test_axis.cpp


    #=============== Third-party tests ================
    infrastructure/protocol/test_raw_word_snapshot.cpp
    infrastructure/protocol/test_raw_bit_snapshot.cpp
    infrastructure/protocol/test_protocol_validator.cpp
    infrastructure/protocol/test_register_registry.cpp
    infrastructure/protocol/test_protocol_validator.cpp
    infrastructure/protocol/test_register_codec.cpp
)

target_link_libraries(unit_tests
    PRIVATE
        infrastructure
        domain
        application
        presentation
        GTest::gtest
        GTest::gtest_main
        Qt6::Core
)

set_target_properties(unit_tests
    PROPERTIES
        AUTOMOC ON
        AUTORCC OFF
        AUTOUIC OFF
)

gtest_discover_tests(unit_tests)
