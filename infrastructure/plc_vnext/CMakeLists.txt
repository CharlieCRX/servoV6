# ==========================================
# plc_vnext —— 16 槽位 PLC 通讯基础设施（新实现）
# ==========================================
#
# 职责边界（详见 README.md）：
#   只含 plc_vnext 命名空间的实现；不与旧 plc::protocol、Qt、Domain 混用。
#   后续子模块（contracts/codec/layout/topology/telemetry/command/...）
#   按 Step 在下方逐个显式追加编译单元。
#
# 库类型从第一天起固定为 STATIC，避免后续新增 .cpp 时改 target 定义。

add_library(plc_vnext STATIC
    PlcVnextAnchor.cpp
    # ============ Step 2：codec ============
    codec/RegisterCodec.cpp
    codec/RawRegisterBlock.cpp
    # …… 每一步显式追加对应编译单元
)

# 顶层目录作 include 根，便于以 "infrastructure/plc_vnext/..." 方式引用头文件
target_include_directories(plc_vnext
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)