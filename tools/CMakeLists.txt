# ==========================================
# tools —— 开发 / 验收工具
# ==========================================
# plc_vnext_readonly_probe: 基于新 C++ 客户端 + PlcRuntimeGateway 的真实 PLC 只读
# 探针（阶段 1 补验 / 阶段 2 最小落地入口）。只执行 readTopology/readRuntime/
# M224/M225/连接状态，禁止任何写（writeAxis / submitGantryRequest / FC05/06/10）。
add_executable(plc_vnext_readonly_probe plc_vnext_readonly_probe.cpp)
target_include_directories(plc_vnext_readonly_probe PRIVATE ${CMAKE_SOURCE_DIR})
# plc_vnext 以 PUBLIC 链接 asio（header-only），并传递 Windows ws2_32 依赖
target_link_libraries(plc_vnext_readonly_probe PRIVATE plc_vnext)

# plc_vnext_physical_axis_probe: 阶段3 单轴受控写入探针（物理 slot 0/1）。
# 写入闸门 + 点动心跳会话在 PhysicalAxisCommissioningService / JogSession 内实现。
add_executable(plc_vnext_physical_axis_probe plc_vnext_physical_axis_probe.cpp)
target_include_directories(plc_vnext_physical_axis_probe PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(plc_vnext_physical_axis_probe PRIVATE application_vnext plc_vnext)

# plc_vnext_motion_probe: 新策略层运动探针（经 SystemManagerVnext + AxisMotionApi）。
# 按 slot 解析到 AxisFunction，走"使能→触发→等待自然回 idle→掉电"时序；
# 点动为"使能→点动(心跳)→显式停止→掉电"。运动需 --confirm-write/--confirm-motion。
add_executable(plc_vnext_motion_probe plc_vnext_motion_probe.cpp)
target_include_directories(plc_vnext_motion_probe PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(plc_vnext_motion_probe PRIVATE application_vnext plc_vnext)
