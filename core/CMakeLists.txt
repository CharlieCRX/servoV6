add_library(corelib
    BusinessLogic.cpp
    Motor.cpp
    IMotor.h
    Logger.cpp
)


# 设置 corelib 的公共头文件目录
# 任何链接 corelib 的目标都可以通过 #include <BusinessLogic.h> 等方式找到头文件
target_include_directories(corelib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接 spdlog 到 corelib
# corelib 内部的代码（如 BusinessLogic 和 Logger）会使用 spdlog
target_link_libraries(corelib PRIVATE spdlog::spdlog)
