# F:/project/servoV6/adapters/protocol/CMakeLists.txt

add_library(adapters_protocol_lib STATIC
    ModbusServoAdapter.cpp    # 从 drivers 迁移过来的 Modbus 协议伺服适配器
    BluetoothServoAdapter.cpp # 从 drivers 迁移过来的蓝牙协议伺服适配器
    #YourProtocolFile.cpp     # 如果有其他协议相关的 .cpp 文件，也在这里列出
)

# 设置公共包含目录
target_include_directories(adapters_protocol_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 协议层适配器通常会依赖：
# - domain_lib (实现 IServoAdapter 接口，或者使用 MovementCommand 等)
# - utils_lib (用于日志)
target_link_libraries(adapters_protocol_lib PUBLIC
    domain_lib                   # 获取 IServoAdapter.h 等接口定义
    utilslib                     # 用于日志
    # 如果这些适配器还依赖 adapters/motors 中的 Motor 类，则需要链接 adapters_motors_lib
    # adapters_motors_lib
)