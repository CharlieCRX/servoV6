# adapters/motors/CMakeLists.txt

add_library(adapters_motors_lib STATIC
    P100S/P100SMotor.h P100S/P100SMotor.cpp
    RelayIO/RelayIOModule.h RelayIO/RelayIOModule.cpp
    RelayIO/RelayController.h RelayIO/RelayController.cpp
)

target_include_directories(adapters_motors_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(adapters_motors_lib PUBLIC
    domain_lib
    utils_lib
    adapters_protocol_lib
    Qt6::Core
)
