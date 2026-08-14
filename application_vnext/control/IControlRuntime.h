// ============================================================================
// IControlRuntime.h —— Phase 0：协调层应用层最小运行时接口
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.1。
//
// 安全 / 连接 / 急停能力必须走独立的应用层最小接口，不能依赖 domain 的 IPlcDriver
// （IPlcDriver 只有 readTopology/readRuntime/writeAxis/submitGantryRequest，**没有**
// readSafety/connectionState/triggerEmergencyStop/requestEmergencyStopRelease）。
//
// 本接口承载协调层需要的 safety / 连接 / 急停能力，由应用层适配器实现为对
// plc_vnext::IPlcRuntimeGateway 的委托。不 include domain 的 IPlcDriver。
//
// 纯头文件接口：不依赖 Qt / Modbus / 旧 domain/*。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"

namespace application_vnext::control {

/// 协调层需要的最小运行时读/写接口；由应用层适配器实现为对
/// plc_vnext::IPlcRuntimeGateway 的委托。不 include domain 的 IPlcDriver。
class IControlRuntime {
public:
    virtual ~IControlRuntime() = default;

    virtual plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>
        readRuntime() = 0;
    virtual plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>
        readSafety() = 0;
    virtual plc_vnext::contracts::ConnectionState connectionState() const = 0;

    virtual plc_vnext::contracts::CommunicationResult triggerEmergencyStop() = 0;
    virtual plc_vnext::contracts::CommunicationResult requestEmergencyStopRelease() = 0;
};

}  // namespace application_vnext::control
