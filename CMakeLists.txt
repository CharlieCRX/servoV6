cmake_minimum_required(VERSION 3.21)
project(servoV6)

set(CMAKE_CXX_STANDARD 20)

# Qt
find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)

qt_standard_project_setup()


enable_testing()

# 子模块
add_subdirectory(external/googletest)
add_subdirectory(domain)
add_subdirectory(application)
add_subdirectory(presentation)
add_subdirectory(tests)


# UI（暂时保留）
qt_add_executable(appservoV6
    main.cpp
)

set_source_files_properties(presentation/qml/core/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(appservoV6
    URI servoV6
    QML_FILES
        Main.qml
        presentation/qml/core/Theme.qml
        presentation/qml/components/IndustrialButton.qml
        presentation/qml/components/AxisItemDelegate.qml
        presentation/qml/blocks/AxisSelectorBlock.qml
        presentation/qml/blocks/TelemetryBlock.qml
        presentation/qml/blocks/ActionControlBlock.qml
        presentation/qml/views/MainDashboard.qml
)

target_link_libraries(appservoV6
    PRIVATE 
        Qt6::Quick
        Qt6::QuickControls2
    PUBLIC
        domain
        presentation
)
