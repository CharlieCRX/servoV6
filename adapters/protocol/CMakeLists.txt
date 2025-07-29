find_package(Qt6 REQUIRED COMPONENTS SerialPort SerialBus)

add_library(adapters_protocol_lib STATIC
    SerialCommProtocol.cpp
    SerialCommProtocol.h
)

target_include_directories(adapters_protocol_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(adapters_protocol_lib
    PRIVATE Qt6::SerialPort Qt6::SerialBus
    PUBLIC domain_lib utils_lib
)
