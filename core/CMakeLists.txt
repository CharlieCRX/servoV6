add_library(corelib
    BusinessLogic.cpp
    Motor.cpp
)


# 设置 corelib 的公共头文件目录
# 任何链接 corelib 的目标都可以通过 #include <BusinessLogic.h> 等方式找到头文件
target_include_directories(corelib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接 utilslib
# corelib 现在依赖 utilslib 来获取日志功能。
target_link_libraries(corelib PRIVATE utilslib)
