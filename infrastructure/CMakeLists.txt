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
)

target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infrastructure
    PUBLIC
        Qt6::Core
)
