// ============================================================================
// FakePlcRuntimeGateway.h —— Step 11 fake: 应用层 TDD 用的高层假实现
// ============================================================================
// 实现 IPlcRuntimeGateway 的纯内存假实现，供 application 层 TDD 使用，模拟
// PLC 契约（拓扑/运行快照、单轴写、龙门提交、连接生命周期），而不是旧 Domain。
// 与 FakeModbusClient（原始寄存器级替身）不同，本 Fake 直接产出**已解码的
// contracts DTO**，application 测试无需构造 Modbus 寄存器或解码。
//
// 用法（显式脚本优先）：
//   - setTopologySnapshot / setRuntimeSnapshot：readTopology()/readRuntime() 默认返回
//     该脚本快照；未脚本化时返回 Transport 失败（强制测试显式声明预期）。
//   - script*Failure / clear*：覆盖上述快照，模拟通讯/提交故障（sticky，直到清除）。
//   - writtenAxis() / gantrySubmissions() / requestReconnectCount()：断言写入与连接动作。
//   - setConnected()：驱动 connectionState()。
//
// 约束（与 fake/ 目录边界一致）：只依赖 contracts 纯 DTO 与本模块的
// IPlcRuntimeGateway 接口；不 include transport / 旧 FakePLC / Domain / Qt。
// 线程安全（内部互斥锁），可与并发 application 测试配合。
// ============================================================================
#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace plc_vnext::fake {

class FakePlcRuntimeGateway : public IPlcRuntimeGateway {
public:
    // ============ 脚本化：状态 ============
    /// 设置 readTopology() 默认返回的快照（覆盖之前脚本）。
    void setTopologySnapshot(contracts::TopologySnapshot snap);
    /// 设置 readRuntime() 默认返回的快照（覆盖之前脚本）。
    void setRuntimeSnapshot(contracts::RuntimeSnapshot snap);
    /// 设置 readSafety() 默认返回的快照（覆盖之前脚本）。
    void setSafetySnapshot(contracts::SafetySnapshot snap);
    /// 设置 readAxisParameters() 默认返回的快照（覆盖之前脚本）。
    void setAxisParameterSnapshot(contracts::AxisParameterSnapshot snap);
    /// 驱动 connectionState() 的连接位。
    void setConnected(bool connected);

    // ============ 脚本化：故障（sticky，直到清除）============
    void scriptTopologyReadFailure(
        contracts::ReadResult<contracts::TopologySnapshot>::FailureKind kind,
        std::string diagnostic);
    void clearTopologyReadFailure();
    void scriptRuntimeReadFailure(
        contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind kind,
        std::string diagnostic);
    void clearRuntimeReadFailure();
    /// readSafety() 一律返回该失败状态；clearSafetyReadFailure() 恢复脚本快照。
    void scriptSafetyReadFailure(
        contracts::ReadResult<contracts::SafetySnapshot>::FailureKind kind,
        std::string diagnostic);
    void clearSafetyReadFailure();
    /// writeAxis() 一律返回该失败状态（ok()==false）；clearWriteAxisFailure() 恢复 sent。
    void scriptWriteAxisFailure(contracts::CommunicationResult::Status status,
                                std::string diagnostic = {});
    void clearWriteAxisFailure();
    /// readAxisParameters() 一律返回该失败状态；clearAxisParameterReadFailure() 恢复脚本快照。
    void scriptAxisParameterReadFailure(
        contracts::ReadResult<contracts::AxisParameterSnapshot>::FailureKind kind,
        std::string diagnostic = {});
    void clearAxisParameterReadFailure();
    /// triggerEmergencyStop()/requestEmergencyStopRelease() 一律返回该失败状态。
    void scriptEmergencyStopWriteFailure(contracts::CommunicationResult::Status status,
                                         std::string diagnostic = {});
    void clearEmergencyStopWriteFailure();
    /// submitGantryRequestDetailed() 一律返回该提交阶段（非 Submitted）。
    void scriptGantrySubmitFailure(contracts::GantrySubmitState state,
                                   std::string diagnostic = {});
    void clearGantrySubmitFailure();

    // ============ 记录（供断言；返回快照，避免并发下数据竞争）============
    struct WrittenAxis {
        contracts::PlcAxisSlot slot;
        contracts::PlcAxisCommand cmd;
    };
    /// 记录一次系统级急停线圈写（0=M224 触发 / 1=M225 解除）。
    struct WrittenEmergencyCoil {
        bool trigger;  // true=M224 触发；false=M225 解除
    };
    struct GantrySubmission {
        contracts::PlcGroupIndex group;
        contracts::GantryRequest req;
    };
    std::vector<WrittenAxis> writtenAxis() const;
    std::vector<WrittenEmergencyCoil> emergencyCoilWrites() const;
    std::vector<GantrySubmission> gantrySubmissions() const;
    unsigned requestReconnectCount() const;
    /// readRuntime() 被调用次数（Phase 1 验证「每 tick 只经 IControlRuntime 读一次 runtime，
    /// driver 侧不得重复读」）。
    unsigned readRuntimeCallCount() const;

    // ============ IPlcRuntimeGateway ============
    contracts::ReadResult<contracts::TopologySnapshot> readTopology() override;
    contracts::ReadResult<contracts::RuntimeSnapshot> readRuntime() override;
    contracts::ReadResult<contracts::SafetySnapshot> readSafety() override;
    contracts::ReadResult<contracts::AxisParameterSnapshot> readAxisParameters(
        contracts::PlcAxisSlot slot) override;
    contracts::CommunicationResult triggerEmergencyStop() override;
    contracts::CommunicationResult requestEmergencyStopRelease() override;
    contracts::CommunicationResult writeAxis(
        contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) override;
    contracts::CommunicationResult submitGantryRequest(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) override;
    contracts::GantrySubmitResult submitGantryRequestDetailed(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) override;
    contracts::ConnectionState connectionState() const override;
    void requestReconnect() override;

private:
    /// 由故障脚本构造失败 CommunicationResult（不抛异常）。
    static contracts::CommunicationResult failedResult(
        contracts::CommunicationResult::Status status, const std::string& diagnostic);

    mutable std::mutex m_mtx;

    // 状态脚本
    std::optional<contracts::TopologySnapshot> m_topology;
    std::optional<contracts::RuntimeSnapshot> m_runtime;
    std::optional<contracts::SafetySnapshot> m_safety;
    std::optional<contracts::AxisParameterSnapshot> m_param;
    bool m_connected = true;
    unsigned m_reconnectCount = 0;
    unsigned m_readRuntimeCount = 0;   // readRuntime() 调用次数（driver 侧）

    // 故障脚本（sticky）
    std::optional<contracts::ReadResult<contracts::TopologySnapshot>::FailureKind>
        m_topoFailure;
    std::string m_topoDiag;
    std::optional<contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind>
        m_runtimeFailure;
    std::string m_runtimeDiag;
    std::optional<contracts::ReadResult<contracts::SafetySnapshot>::FailureKind>
        m_safetyFailure;
    std::string m_safetyDiag;
    std::optional<contracts::ReadResult<contracts::AxisParameterSnapshot>::FailureKind>
        m_paramFailure;
    std::string m_paramDiag;
    std::optional<contracts::CommunicationResult::Status> m_estopWriteFailure;
    std::string m_estopWriteDiag;
    std::optional<contracts::CommunicationResult::Status> m_writeAxisFailure;
    std::string m_writeAxisDiag;
    std::optional<contracts::GantrySubmitState> m_gantryFailure;
    std::string m_gantryDiag;

    // 记录
    std::vector<WrittenAxis> m_writtenAxis;
    std::vector<WrittenEmergencyCoil> m_emergencyCoils;
    std::vector<GantrySubmission> m_gantrySubmissions;
};

}  // namespace plc_vnext::fake
