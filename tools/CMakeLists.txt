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
