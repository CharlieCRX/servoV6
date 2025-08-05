# adapters/motors/CMakeLists.txt

add_library(adapters_motors_lib STATIC
    P100S/P100SMotor.h
    P100S/P100SMotor.cpp
)

target_include_directories(adapters_motors_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..     # adapters/ 目录，用于找到 protocol/
)

target_link_libraries(adapters_motors_lib PUBLIC
    domain_lib
    utils_lib
    adapters_protocol_lib
    Qt6::Core
)
