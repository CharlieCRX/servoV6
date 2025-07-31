# F:/project/servoV6/adapters/servos/CMakeLists.txt

add_library(adapters_servos_lib STATIC

)

# 设置公共包含目录
target_include_directories(adapters_servos_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 伺服适配器实现了 IServoAdapter，所以它依赖 domain_lib。
# 它可能还依赖 Motor（来自 adapters_motors_lib）和 Logger（来自 utils）。
target_link_libraries(adapters_servos_lib PUBLIC
    domain_lib          # 获取 IServoAdapter.h
    adapters_motors_lib # 如果伺服适配器内部使用了 Motor 类
    utils_lib            # 用于日志等
)