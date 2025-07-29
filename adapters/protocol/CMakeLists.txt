# F:/project/servoV6/adapters/protocol/CMakeLists.txt
find_package(Qt6 REQUIRED COMPONENTS SerialPort SerialBus)

add_library(adapters_protocol_lib STATIC
    SerialCommProtocol.h SerialCommProtocol.cpp
    #YourProtocolFile.cpp     # 如果有其他协议相关的 .cpp 文件，也在这里列出
)

# 设置公共包含目录
target_include_directories(adapters_protocol_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})


target_link_libraries(adapters_protocol_lib
    PRIVATE
        Qt6::SerialPort
        Qt6::SerialBus
)
target_link_libraries(adapters_protocol_lib PUBLIC
    domain_lib                   # 获取 ICommProtocol.h 等接口定义
    utils_lib                     # 用于日志
)
