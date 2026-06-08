add_library(application
    axis/EnableUseCase.h
    axis/JogAxisUseCase.h
    axis/MoveAbsoluteUseCase.h
    axis/MoveRelativeUseCase.h
    axis/StopAxisUseCase.h

    policy/JogOrchestrator.h
    policy/AutoAbsMoveOrchestrator.h
    policy/AutoRelMoveOrchestrator.h
    policy/GantryOrchestrator.h
    policy/AbsMovePolicy.h
    policy/RelMovePolicy.h
    policy/JoystickControlPolicy.h

    safety/EmergencyStopUseCase.h
    safety/ReleaseEmergencyStopUseCase.h

    SystemManager.h
    UsecaseError.h

    # ★ 阶段 1：UDP 通讯层（application 侧）
    udp/UdpProtocol.h
    udp/UdpResponseBuilder.h
    udp/UdpCommandDispatcher.h
)

target_include_directories(application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(application
    PUBLIC
        domain
        infrastructure
        Qt6::Core
)