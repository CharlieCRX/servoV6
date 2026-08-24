#pragma once

#include <string>

#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/model/GantryStatus.h"
#include "domain_vnext/model/SafetyState.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace domain_vnext::logging {

inline std::string groupName(plc_vnext::contracts::PlcGroupIndex g) {
    switch (g.value()) {
        case 0: return "A";
        case 1: return "B";
        default: return "G" + std::to_string(g.value());
    }
}

inline std::string axisName(model::AxisFunction fn) {
    return std::string(model::axisFunctionName(fn));
}

inline LogContext context(plc_vnext::contracts::PlcGroupIndex g,
                          model::AxisFunction fn,
                          const std::string& traceId) {
    return LogContext{groupName(g), axisName(fn), traceId};
}

inline LogContext gantryContext(plc_vnext::contracts::PlcGroupIndex g,
                                const std::string& traceId) {
    return LogContext{groupName(g), "Gantry", traceId};
}

inline const char* axisCommandKindName(model::AxisCommandKind k) {
    using K = model::AxisCommandKind;
    switch (k) {
        case K::EnableAxis: return "EnableAxis";
        case K::ClearRelZero: return "ClearRelZero";
        case K::ClearAbsPosition: return "ClearAbsPosition";
        case K::TriggerAbsMove: return "TriggerAbsMove";
        case K::TriggerRelMove: return "TriggerRelMove";
        case K::JogForward: return "JogForward";
        case K::JogBackward: return "JogBackward";
        case K::ResetAlarm: return "ResetAlarm";
        case K::EnableMotor: return "EnableMotor";
        case K::StopRelMove: return "StopRelMove";
        case K::StopAbsMove: return "StopAbsMove";
        case K::SetRelZero: return "SetRelZero";
        case K::JogHeartbeat: return "JogHeartbeat";
        case K::ClearAlarmWord: return "ClearAlarmWord";
        case K::SetManualSpeed: return "SetManualSpeed";
        case K::SetPositioningSpeed: return "SetPositioningSpeed";
        case K::SetAbsDistance: return "SetAbsDistance";
        case K::SetRelDistance: return "SetRelDistance";
        case K::SetSoftNegLimit: return "SetSoftNegLimit";
        case K::SetSoftPosLimit: return "SetSoftPosLimit";
        case K::SetSoftLimitControl: return "SetSoftLimitControl";
        case K::SetRelZeroRecord: return "SetRelZeroRecord";
    }
    return "?";
}

}  // namespace domain_vnext::logging
