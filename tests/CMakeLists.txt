enable_testing()

add_executable(tests
    test_businesslogic.cpp
    test_motor.cpp
    test_adapter.cpp
    main.cpp
)

target_link_libraries(tests
    PRIVATE
        Qt6::Test
        corelib
        driverslib
        transportlib
)

add_test(NAME RunAllTests COMMAND tests)
