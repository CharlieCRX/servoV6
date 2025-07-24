add_library(driverslib
	ModbusServoAdapter.cpp
		BluetoothServoAdapter.cpp
)

target_include_directories(driverslib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
