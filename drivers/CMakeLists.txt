add_library(driverslib
	ModbusServoAdapter.cpp
		BluetoothServoAdapter.cpp
)

target_include_directories(driverslib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 包含日志系统
target_link_libraries(driverslib PRIVATE corelib)
