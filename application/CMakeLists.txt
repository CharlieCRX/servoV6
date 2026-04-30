add_library(application
    axis/EnableUseCase.h
    axis/JogAxisUseCase.h
    axis/MoveAbsoluteUseCase.h
    axis/MoveRelativeUseCase.h
    axis/StopAxisUseCase.h
    axis/AxisRepository.h

    policy/JogOrchestrator.h
    policy/AutoAbsMoveOrchestrator.h
    policy/AutoRelMoveOrchestrator.h
)

target_include_directories(application
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(application
    PUBLIC
        domain
)