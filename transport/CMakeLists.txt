add_library(transportlib
    ModbusClient.cpp
)

target_include_directories(transportlib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(corelib PRIVATE utilslib)
