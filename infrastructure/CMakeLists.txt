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

add_library(infrastructure INTERFACE)

target_include_directories(infrastructure
    INTERFACE
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(infrastructure
    INTERFACE
        Qt6::Core
)
