# ==========================================
# Infrastructure — INTERFACE (header-only)
# ==========================================
#
# 提供:
#   - ISystemDriver                            通讯抽象
#   - FakePLC / FakeAxisDriver                 测试替身
#   - plc::protocol (RegisterMetadata, Codec)  寄存器协议
#   - logger (Logger, LogContext, TraceScope)  日志系统
#   - utils (CommandFormatter)                 工具函数
#
# 使用方式:
#   target_link_libraries(<your_target> PRIVATE infrastructure)
#

add_library(infrastructure STATIC
    plc/protocol/validator/ProtocolConstraintValidator.cpp
    plc/protocol/PlcPoller.cpp
    plc/protocol/AsioModbusTcpClient.cpp

    # ★ 阶段 1：UDP 通讯层（infrastructure 侧）
    udp/UdpServer.h

    # ★ SDL3 摇杆控制（infrastructure 侧）
    joystick/SDL3JoystickDriver.cpp
    joystick/SDL3JoystickWrapper.cpp
)

target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/domain
)

target_link_libraries(infrastructure
    PUBLIC
        asio
        Qt6::Core
        Qt6::Network
        ${SDL3_LIBRARIES}
)
