add_library(domain
    entity/Axis.h
    entity/Axis.cpp
    entity/AxisId.h
    entity/SystemContext.h
    value/GantryMode.h
    value/MotionDirection.h
    value/GantryPosition.h
    value/PositionConsistency.h
    value/CouplingCondition.h
    value/SafetyCheckResult.h
)

target_include_directories(domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
