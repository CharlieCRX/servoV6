# servoV6/utils/CMakeLists.txt

add_library(utils_lib
    Logger.cpp
)

# 设置 utils_lib 的公共头文件目录
# 任何链接 utils_lib 的目标都可以通过 #include <Logger.h> 找到头文件
target_include_directories(utils_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# 链接 spdlog 到 utils_lib
# utils_lib 内部的 Logger.cpp 会直接使用 spdlog。
# 由于 Logger.h 可能会暴露 spdlog 的类型（如 std::shared_ptr<spdlog::logger>），
# 并且其他模块通过 utils_lib::utils_lib 使用日志功能，
# 所以这里将 spdlog::spdlog 设为 PUBLIC，确保依赖正确传递。
target_link_libraries(utils_lib PUBLIC spdlog::spdlog)

# 如果你希望其他项目能够通过 find_package 找到 utils_lib
# 并且它是一个通用的独立库，你可以添加安装和导出规则。
# 但在这个内部子项目场景下，通常不需要。
