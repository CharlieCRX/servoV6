# ==========================================
# domain_vnext —— PLC 领域层（新实现）
# ==========================================
#
# 职责边界（详见 README.md）：
#   namespace domain_vnext；只含纯领域 DTO + 状态机。
#   不 include 旧 domain/*、infrastructure/ISystemDriver、Qt、Modbus。
#   依赖方向：domain_vnext -> plc_vnext::contracts（纯 DTO）。
#
# 库类型从第一天起固定为 STATIC（锚点保证稳定），后续按 Phase 显式追加编译单元。

add_library(domain_vnext STATIC
    DomainVnextAnchor.cpp
    # ============ P1 model：纯头文件值对象（无 .cpp 编译单元）============
    # ============ P2 state：纯头文件状态机（无 .cpp 编译单元）============
    # ============ P3 system：纯头文件组合根（无 .cpp 编译单元）============
    # ============ P4 link：纯头文件（command/gateway/FeedbackDispatcher）============
    # …… 每个 Phase 在此显式追加对应编译单元
)

# 顶层目录作 include 根，便于以 "domain_vnext/..." 方式引用头文件，
# 并让 plc_vnext::contracts（纯 DTO 头文件）可用。
target_include_directories(domain_vnext
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# 独立测试通道（target: domain_vnext_tests）
# tests 依赖 gtest（仅在桌面平台构建），Android 下跳过。
if(NOT ANDROID)
    add_subdirectory(tests)
endif()
