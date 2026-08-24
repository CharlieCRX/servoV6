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

    # ★ 游戏手柄 JNI 桥接层
    joystick/AndroidGamepadJoystick.h
    joystick/AndroidGamepadJoystick.cpp

    # ★ Qt/qDebug/QML console.log -> Logger.h 统一桥接
    logger/QtLogBridge.h
    logger/QtLogBridge.cpp
)

set_target_properties(infrastructure PROPERTIES AUTOMOC ON)

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
)
