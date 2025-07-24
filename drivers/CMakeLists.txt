add_library(driverslib
	ModbusServoAdapter.cpp
		BluetoothServoAdapter.cpp
)

target_include_directories(driverslib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(driverslib PRIVATE spdlog::spdlog)
