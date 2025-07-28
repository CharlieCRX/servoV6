# F:/project/servoV6/infrastructure/transport/CMakeLists.txt

add_library(infrastructure_transport_lib STATIC
    ModbusClient.cpp # 添加你的 ModbusClient 源文件
    # 如果还有其他协议相关的 .cpp 文件，也在这里列出
)

# 设置公共包含目录
target_include_directories(infrastructure_transport_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ModbusClient 可能会使用 Logger (来自 utils)
# 如果 ModbusClient 依赖其他库（例如 Boost.Asio 或其他网络库），也要在这里链接。
target_link_libraries(infrastructure_transport_lib PUBLIC utilslib)
