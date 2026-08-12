// ============================================================================
// IPlcDriver.h —— P4 gateway: 领域侧读/写门面抽象
// ============================================================================
// 设计稿 §6：domain 保持「面向抽象」。本接口是领域定义的驱动门面（读拓扑 /
// 读运行 / 写轴 / 提交龙门），由一层适配器实现为对 plc_vnext 的
// IPlcRuntimeGateway 的调用（适配器可放 infrastructure/plc_vnext 或独立
// PlcRuntimeDriverAdapter）。测试用 plc_vnext::fake::FakePlcRuntimeGateway。
// 依赖方向：domain_vnext -> plc_vnext::contracts（纯 DTO）；不 include 旧
// domain/*、ISystemDriver、Qt、Modbus。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace domain_vnext::gateway {

/// 领域侧的 PLC 读/写门面。实现委托 plc_vnext::IPlcRuntimeGateway。
class IPlcDriver {
public:
    virtual ~IPlcDriver() = default;

    /// 读拓扑快照（只读）。
    virtual plc_vnext::contracts::ReadResult<plc_vnext::contracts::TopologySnapshot>
        readTopology() = 0;

    /// 读运行快照（只读，16 槽位 + 龙门状态）。
    virtual plc_vnext::contracts::ReadResult<plc_vnext::contracts::RuntimeSnapshot>
        readRuntime() = 0;

    /// 提交单轴写入意图（参数写/使能/点动/触发/清除）。
    virtual plc_vnext::contracts::CommunicationResult writeAxis(
        plc_vnext::contracts::PlcAxisSlot slot,
        const plc_vnext::contracts::PlcAxisCommand& cmd) = 0;

    /// 提交龙门请求（组事务），返回完整提交阶段。
    virtual plc_vnext::contracts::GantrySubmitResult submitGantryRequest(
        plc_vnext::contracts::PlcGroupIndex g,
        const plc_vnext::contracts::GantryRequest& req) = 0;
};

}  // namespace domain_vnext::gateway
