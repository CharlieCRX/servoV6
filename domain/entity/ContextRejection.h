#pragma once
enum class ContextRejection {
    None,

    PhysicalAxisLockedByGantry,
    LogicalAxisUnavailableWhenDecoupled,

    AxisNotRegistered,
    GroupNotRegistered,

    DriverNotReady,
};