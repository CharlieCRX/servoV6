cmake_minimum_required(VERSION 3.16)

project(servoV6 VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt6 组件
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Test)

# 推荐 Qt 标准项目设置（自动启用 MOC/UIC/etc）
qt_standard_project_setup(REQUIRES 6.8)

# 添加子模块
add_subdirectory(core)
add_subdirectory(drivers)
add_subdirectory(transport)
add_subdirectory(app)
add_subdirectory(tests)
