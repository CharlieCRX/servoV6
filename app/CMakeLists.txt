qt_add_executable(servoV6
    main.cpp
)

qt_add_qml_module(servoV6
    URI servoV6
    VERSION 1.0
    QML_FILES
        qml/Main.qml
)

target_link_libraries(servoV6
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        utilslib
)

# 设置平台特定属性
set_target_properties(servoV6 PROPERTIES
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
)

# 安装规则
include(GNUInstallDirs)
install(TARGETS servoV6
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
