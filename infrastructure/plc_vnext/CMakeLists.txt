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
    # ============ Step 4：ReadPlanBuilder ============
    layout/ReadPlanBuilder.cpp
    # ============ Step 5：transport / Fake ============
    transport/ModbusIoExecutor.cpp
    transport/ConnectionMonitor.cpp
    transport/AsioModbusTcpClient.cpp   # 阶段 1：真实通讯客户端
    fake/FakeModbusClient.cpp
    # ============ Step 6：topology ============
    topology/TopologyDecoder.cpp
    topology/TopologyValidator.cpp
    topology/PlcTopologyReader.cpp
    # ============ Step 7：telemetry ============
    telemetry/AxisSnapshotDecoder.cpp
    telemetry/GantryStatusReader.cpp
    telemetry/PlcSnapshotReader.cpp
    telemetry/SafetyStateReader.cpp       # 阶段 2：急停只读器（M224/M225）
    # ============ Step 8：command（单轴写路径，仅 Fake 离线 TDD）============
    command/PlcAxisCommandWriter.cpp
    # ============ Step 9：command（龙门请求，仅 Fake 离线 TDD）============
    command/PlcGantryCommandWriter.cpp
    # ============ Step 10：PlcRuntimeGateway（组合门面）============
    PlcRuntimeGateway.cpp
    # ============ Step 11：fake 收尾（应用层高层假实现）============
    fake/FakePlcRuntimeGateway.cpp
    # …… 每一步显式追加对应编译单元
)

# 顶层目录作 include 根，便于以 "infrastructure/plc_vnext/..." 方式引用头文件
target_include_directories(plc_vnext
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# 真实客户端使用 Standalone Asio（header-only）+ 旧 Logger（header-only）。
# asio INTERFACE 目标在顶层 CMakeLists 定义（含 Windows ws2_32 依赖），
# PUBLIC 使下游（plc_vnext_tests）自动获得 asio include 与 ws2_32。
target_link_libraries(plc_vnext PUBLIC asio)