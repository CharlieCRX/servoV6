cmake_minimum_required(VERSION 3.21)
project(servoV6)

set(CMAKE_CXX_STANDARD 20)

# ==========================================
# 1. 先引入 Qt 环境 (务必加上 Qml)
# ==========================================
find_package(Qt6 REQUIRED COMPONENTS Qml Quick QuickControls2)

# ==========================================
# 1.5 自动获取 asio (header-only 网络库)
# ==========================================
#
# asio 是 header-only 库，无需编译。
# 通过 FetchContent 从 GitHub 自动下载，团队成员无需手动安装。
# 如果本地已有 external/asio 目录，则跳过下载（离线友好）。
#
include(FetchContent)

FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG        asio-1-28-0
    GIT_SHALLOW    TRUE
    SOURCE_DIR     ${CMAKE_SOURCE_DIR}/external/asio
)

FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
endif()

# asio 原生使用 autotools，不提供 CMakeLists.txt。
# 手动创建一个 INTERFACE 目标，使其可被 target_link_libraries(… asio) 引用。
if(NOT TARGET asio)
    add_library(asio INTERFACE)
    # asio 的 include 路径在其仓库子目录 asio/include/ 下
    target_include_directories(asio INTERFACE
        ${asio_SOURCE_DIR}/asio/include
    )
    # asio 在 Windows 上依赖 winsock
    if(WIN32)
        target_link_libraries(asio INTERFACE ws2_32)
    endif()
endif()

# ==========================================
# 2. 初始化 Qt 标准工程策略
# ==========================================
qt_standard_project_setup()
qt_policy(SET QTP0004 OLD)
if(NOT ANDROID)

    enable_testing()
    add_subdirectory(external/googletest)
    add_subdirectory(tests)

endif()

# 子模块（infrastructure 必须在最前面，domain/application 依赖它）
add_subdirectory(infrastructure)
add_subdirectory(domain)
add_subdirectory(application)
add_subdirectory(presentation)


# ==========================================
# 3. 主可执行目标 + QML 模块
# ==========================================
qt_add_executable(appservoV6
    main.cpp
)

set_target_properties(appservoV6 PROPERTIES AUTOMOC ON)

set_source_files_properties(presentation/qml/core/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appservoV6
    URI servoV6
    RESOURCE_PREFIX "/qt/qml"
    QML_FILES
        Main.qml
        presentation/qml/core/Theme.qml
        presentation/qml/components/IndustrialButton.qml
        presentation/qml/components/AxisItemDelegate.qml
        presentation/qml/components/VelocitySettingsPopup.qml
        presentation/qml/blocks/AxisSelectorBlock.qml
        presentation/qml/blocks/TelemetryBlock.qml
        presentation/qml/blocks/ActionControlBlock.qml
        presentation/qml/blocks/ErrorPanelBlock.qml
        presentation/qml/views/MainDashboard.qml
)

target_link_libraries(appservoV6
    PRIVATE
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
    PUBLIC
        domain
        presentation
)
