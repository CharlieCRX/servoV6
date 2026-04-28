include(GoogleTest)

add_executable(unit_tests
    application/test_move_relative_usecase.cpp
    application/test_move_absolute_usecase.cpp
    application/test_axis_repository.cpp
    # presentation/viewmodel/test_axis_viewmodel_core.cpp

    # application/policy/test_jog_orchestrator.cpp
    # application/policy/test_auto_rel_move_orchestrator.cpp
    # application/policy/test_auto_abs_move_orchestrator.cpp

    domain/test_axis.cpp
    # application/test_jog_usecase.cpp
    # application/test_enable_usecase.cpp
    # application/test_stop_usecase.cpp

    # infrastructure/test_fake_plc.cpp
    # infrastructure/test_system_integration.cpp
)

target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(unit_tests
    PRIVATE
        domain
        # presentation
        GTest::gtest
        GTest::gtest_main
)

gtest_discover_tests(unit_tests)