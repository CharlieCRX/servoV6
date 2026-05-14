add_library(domain
    entity/Axis.h
    entity/Axis.cpp
    entity/AxisId.h
    entity/ContextRejection.h
    entity/SystemContext.h

    gantry/GantryRejection.h
    gantry/GantryGroup.h
    gantry/GantryCouplingState.h
    gantry/GantryFeedback.h
    gantry/GantryMotorController.h
)

target_include_directories(domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
