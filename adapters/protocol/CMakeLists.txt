# F:/project/servoV6/adapters/protocol/CMakeLists.txt

# 找到 Qt6 的依赖，因为 SerialCommProtocol 可能会用到
set(ANDROID_PLATFORM 33)  # 或您使用的 35
find_package(Qt6 REQUIRED COMPONENTS SerialPort SerialBus Bluetooth)

# 创建一个静态库，包含所有协议相关的实现文件
add_library(adapters_protocol_lib STATIC
    SerialCommProtocol.h        SerialCommProtocol.cpp
    MotorRegisterAccessor.h     MotorRegisterAccessor.cpp
    BluetoothCommProtocol.h     BluetoothCommProtocol.cpp
)

# 设置公共包含目录，以便其他模块可以引用到这些头文件
target_include_directories(adapters_protocol_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR} # 自身头文件
)

target_link_libraries(adapters_protocol_lib
    PRIVATE Qt6::SerialPort Qt6::SerialBus
    PUBLIC Qt6::Bluetooth
    PUBLIC domain_lib utils_lib
)
