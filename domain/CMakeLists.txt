# domain/CMakeLists.txt
# 定义一个接口库。它不包含任何源文件，
# 仅提供包含目录和接口属性。
add_library(domain_lib INTERFACE)

# 设置公共包含目录，以便其他模块可以 #include "IMotor.h" 等
target_include_directories(domain_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# 如果这些接口文件之间有内部依赖（例如 IServoAdapter.h 使用了 IMotor.h 中的类型），
# 并且你需要确保这种依赖关系被传递给链接 domain_lib 的其他目标，
# 可以在这里添加 INTERFACE 链接。但对于纯头文件接口，通常其头文件包含已足够。
# target_link_libraries(domain_lib INTERFACE) # 暂时保持为空
