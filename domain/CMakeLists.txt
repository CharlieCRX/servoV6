add_library(domain
    entity/Axis.h
    entity/Axis.cpp
    entity/AxisId.h
    entity/SystemContext.h
    entity/PhysicalAxis.h
    entity/LogicalAxis.h
    entity/GantrySystem.h
    event/GantryEvents.h
    value/GantryMode.h
    value/MotionDirection.h
    value/GantryPosition.h
    value/PositionConsistency.h
    value/CouplingCondition.h
    value/SafetyCheckResult.h
    port/IPhysicalAxisDriver.h
    port/IGantryEventPublisher.h
    port/IGantryStateQuery.h
    service/GantryCouplingService.h
    service/GantrySafetyService.h
    service/GantryStateAggregator.h
)

target_include_directories(domain PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
