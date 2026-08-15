// ============================================================================
// GatewayControlRuntime.h —— Phase 5：生产版 IControlRuntime（委托 IPlcRuntimeGateway）
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§5.1：
//   安全 / 连接 / 急停能力必须走独立的 IControlRuntime，不能依赖 domain 的
//   IPlcDriver。本类是生产组合根使用的默认实现：把 IControlRuntime 的全部方法
//   委托给 plc_vnext::IPlcRuntimeGateway（同一串行 I/O 通道）。
//
// 纯头文件：只依赖 contracts 纯 DTO 与 IControlRuntime / IPlcRuntimeGateway 接口。
// ============================================================================
#pragma once

#include "application_vnext/control/IControlRuntime.h"
#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"

namespace application_vnext::control {

/// 生产版 IControlRuntime：把协调层最小运行时接口委托给 plc_vnext::IPlcRuntimeGateway。
class GatewayControlRuntime : public IControlRuntime {
public:
    explicit GatewayControlRuntime(plc_vnext::IPlcRuntimeGateway& gw) : gw_(&gw) {}

    plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>
        readRuntime() override { return gw_->readRuntime(); }

    plc_vnext::contracts::ReadResult<plc_vnext::contracts::SafetySnapshot>
        readSafety() override { return gw_->readSafety(); }

    plc_vnext::contracts::ConnectionState connectionState() const override {
        return gw_->connectionState();
    }

    plc_vnext::contracts::CommunicationResult triggerEmergencyStop() override {
        return gw_->triggerEmergencyStop();
    }

    plc_vnext::contracts::CommunicationResult requestEmergencyStopRelease() override {
        return gw_->requestEmergencyStopRelease();
    }

private:
    plc_vnext::IPlcRuntimeGateway* gw_;
};

}  // namespace application_vnext::control
