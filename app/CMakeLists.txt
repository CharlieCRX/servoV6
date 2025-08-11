# app/CMakeLists.txt

# 定义项目可执行文件
qt_add_executable(servoV6
    main.cpp
)

# 定义 QML 模块，并指定 QML 文件
qt_add_qml_module(servoV6
    URI servoV6
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Bluetooth.qml
)

# 链接项目所需库
target_link_libraries(servoV6
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Quick
        adapters_motors_lib
        utils_lib
)

# --- Android 平台权限配置 ---
# 你需要保留这个 if(ANDROID) 块，但移除 'target_link_libraries'
if(ANDROID)
    # 声明权限，这些权限将自动被添加到 AndroidManifest.xml 中
    set_property(TARGET servoV6 PROPERTY
        QT_ANDROID_PERMISSIONS
        "android.permission.BLUETOOTH"
        "android.permission.BLUETOOTH_ADMIN"
        "android.permission.BLUETOOTH_CONNECT"
        "android.permission.BLUETOOTH_SCAN"
        "android.permission.ACCESS_FINE_LOCATION"
    )
endif()

# --- 桌面平台特定属性 ---
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
