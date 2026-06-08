# presentation/CMakeLists.txt

add_library(presentation
    viewmodel/AxisViewModelCore.cpp
    viewmodel/QtAxisViewModel.cpp
    viewmodel/GantryViewModel.cpp
    viewmodel/ErrorTranslator.cpp
    viewmodel/ViewModelError.h
    viewmodel/EmergencyStopViewModel.h

    # ★ 游戏手柄输入事件系统
    input/InputEvent.h
    input/GamepadInputInterpreter.h
    input/GamepadInputInterpreter.cpp
    input/AxisSelectionModel.h
    input/AxisSelectionModel.cpp
    input/AxisSelectionController.h
    input/AxisSelectionController.cpp
)

set_target_properties(presentation PROPERTIES AUTOMOC ON)

target_include_directories(presentation
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(presentation
    PUBLIC
        Qt6::Core
        domain
        application
        infrastructure
)
