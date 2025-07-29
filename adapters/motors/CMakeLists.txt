# F:/project/servoV6/adapters/motors/CMakeLists.txt

add_library(adapters_motors_lib STATIC
    Motor.cpp # 添加你的 Motor 源文件
)

# 设置公共包含目录
target_include_directories(adapters_motors_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Motor.h/.cpp 实现了 IMotor 接口，因此它需要 domain_lib 的头文件。
# 它可能还使用 Logger（来自 utils）。
target_link_libraries(adapters_motors_lib PUBLIC
    domain_lib # 获取 IMotor.h
    utils_lib  # 用于日志等
)