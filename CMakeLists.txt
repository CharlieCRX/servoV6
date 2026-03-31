cmake_minimum_required(VERSION 3.21)
project(servoV6)

set(CMAKE_CXX_STANDARD 20)

# Qt
find_package(Qt6 REQUIRED COMPONENTS Quick)

qt_standard_project_setup()


enable_testing()

# 子模块
add_subdirectory(external/googletest)
add_subdirectory(domain)
add_subdirectory(tests)


# UI（暂时保留）
qt_add_executable(appservoV6
    main.cpp
)

qt_add_qml_module(appservoV6
    URI servoV6
    QML_FILES Main.qml
)

target_link_libraries(appservoV6
    PRIVATE Qt6::Quick
    domain
)
