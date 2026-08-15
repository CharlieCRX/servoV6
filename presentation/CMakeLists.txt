# presentation/CMakeLists.txt

add_library(presentation
    viewmodel/AxisViewModelCore.cpp
    viewmodel/QtAxisViewModel.cpp
    viewmodel/GantryViewModel.cpp
    viewmodel/ConnectionViewModel.h
    viewmodel/ConnectionViewModel.cpp
    viewmodel/ErrorTranslator.cpp
    viewmodel/ViewModelError.h
    viewmodel/EmergencyStopViewModel.h

    # ★ Phase 2：统一状态快照 -> Qt/QML 只读适配器（读 ControlStateStore 投影）
    viewmodel/UiControlAdapter.h
    viewmodel/UiControlAdapter.cpp

    # ★ 游戏手柄输入事件系统
    input/InputEvent.h
    input/GamepadInputInterpreter.h
    input/GamepadInputInterpreter.cpp
    input/AxisSelectionModel.h
    input/AxisSelectionModel.cpp
    input/AxisSelectionController.h
    input/AxisSelectionController.cpp
    input/MotionController.h
    input/MotionController.cpp
    input/JoystickCommandBuilder.h   # ★ Phase 4：摇杆 -> ControlCommand 纯命令构造（无 Qt，可单测）
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
        application_vnext      # ★ Phase 2：UiControlAdapter 依赖 MotionControlService/ControlStateStore
)
