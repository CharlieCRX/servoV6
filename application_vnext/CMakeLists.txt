# ==========================================
# application_vnext —— 应用层（基于 domain_vnext 的新实现）
# ==========================================
#
# P5 app（设计稿 §8）：SystemManager / UseCase 迁移到 domain_vnext。
#   本库是对旧 application 的并行迁移目标：组合根门面 SystemManagerVnext
#   持有 domain_vnext::system::AxisSystem + gateway::IPlcDriver，封装
#   boot / poll / 单轴用例 / 急停 / 龙门联动，并配套一个把
#   gateway::IPlcDriver 委托给 plc_vnext::IPlcRuntimeGateway 的适配器。
#
# 职责边界：
#   - 依赖方向：application_vnext -> domain_vnext -> plc_vnext::contracts；
#   - 不 include 旧 domain/*、infrastructure/ISystemDriver、Qt、Modbus；
#   - 与旧 application 并行迁移（P6 清理前旧层保留），互不侵入。
#
# 库类型固定 STATIC（锚点保证稳定），后续 Phase 显式追加编译单元。
add_library(application_vnext STATIC
    AppVnextAnchor.cpp
    # ============ P5 app：纯头文件门面（无 .cpp 编译单元）============
    # ============ 阶段3：单轴物理调试服务（写入闸门 + 心跳会话）============
    commissioning/JogSession.cpp
    commissioning/PhysicalAxisCommissioningService.cpp
)

# 顶层目录作 include 根，便于以 "application_vnext/..." 方式引用头文件。
target_include_directories(application_vnext
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(application_vnext
    PUBLIC
        domain_vnext
        plc_vnext
)

# 独立测试通道（target: application_vnext_tests）
add_subdirectory(tests)
