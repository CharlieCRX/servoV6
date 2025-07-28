cmake_minimum_required(VERSION 3.16)

project(servoV6 VERSION 0.1 LANGUAGES CXX)

include(ProcessorCount)
ProcessorCount(N)
if(NOT N EQUAL 0)
  set(CMAKE_BUILD_PARALLEL_LEVEL ${N})  # 自动设置为CPU核心数
  message(STATUS "Build parallel level: ${N}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找 Qt6 组件
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick Test)

# 推荐 Qt 标准项目设置（自动启用 MOC/UIC/etc）
qt_standard_project_setup(REQUIRES 6.8)

# 添加子模块
add_subdirectory(external/spdlog)
add_subdirectory(utils)
add_subdirectory(core)
add_subdirectory(drivers)
add_subdirectory(transport)
add_subdirectory(app)
add_subdirectory(tests)

# === 添加 GoogleTest ===
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/5376968f6948923e2411081fd9372e71a59d8e77.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
