add_library(domain
    entity/Axis.h
    entity/Axis.cpp
    entity/AxisId.h
    entity/ContextRejection.h
    entity/SystemContext.h

    gantry/GantryRejection.h
    gantry/GantryCouplingState.h
    gantry/GantryFeedback.h
    gantry/GantryCouplingController.h
    gantry/GantryPowerController.h

    safety/SafetyRejection.h
    safety/SafetyState.h
    safety/EmergencyStopController.h
)

target_include_directories(domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
