// ============================================================================
// PlcRuntimeGateway.h —— Step 10 Gateway: 组合门面实现
// ============================================================================
// 组合 topology/telemetry readers、单轴/龙门 writers 与 transport 连接监控，
// 对外实现唯一的 IPlcRuntimeGateway 高层接口。
//
// 职责边界（Step 10.3）：
//   - 只透传组合，不含联动业务编排、重试策略、超时判定、ViewModel；
//   - 不依赖 SystemContext；接口输入输出一律 contracts::* 纯 DTO；
//   - 写路径只提交，`ok()` 仅证明写请求到达 PLC，不做读回确认。
//
// 共享串行化通道（评审修补①）：所有 reader/writer 统一注入同一个
// transport::ModbusIoExecutor（其实现 IModbusClient），从而：
//   - 每笔 I/O 单通道串行（ModbusIoExecutor 内部闸门）；
//   - 龙门提交用 executeGroup 把 Command→RequestSeq 包成全局临界区，保证单轴写 /
//     telemetry 读 / 其它龙门提交不会插入其间。
//
// 组准入边界：Gateway 缺省放行协议定义的 A/B 两组（Group 0/1）。是否开放控制
// 由上层按 AxisTopology 构建的系统模型判定；如需维护窗口临时禁用某组，可显式
// 注入 groupGate。
// ============================================================================
#pragma once

#include <memory>

#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/command/PlcAxisCommandWriter.h"
#include "infrastructure/plc_vnext/command/PlcGantryCommandWriter.h"
#include "infrastructure/plc_vnext/telemetry/AxisParameterReader.h"
#include "infrastructure/plc_vnext/telemetry/PlcSnapshotReader.h"
#include "infrastructure/plc_vnext/telemetry/SafetyStateReader.h"
#include "infrastructure/plc_vnext/topology/PlcTopologyReader.h"
#include "infrastructure/plc_vnext/transport/ConnectionMonitor.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"
#include "infrastructure/plc_vnext/transport/ModbusIoExecutor.h"

namespace plc_vnext {

class PlcRuntimeGateway : public IPlcRuntimeGateway {
public:
    /// 注入 transport client（测试用 FakeModbusClient）。Gateway 内部创建唯一的
    /// ModbusIoExecutor 作为所有 reader/writer 的共享串行化通道。groupGate 用于
    /// 龙门组的提交准入；缺省（空函数）时放行协议定义的 Group 0/1。
    explicit PlcRuntimeGateway(
        transport::IModbusClientPtr client,
        command::PlcGantryCommandWriter::GroupGate groupGate = {});

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
    // 共享串行化通道：所有 reader/writer 的唯一 I/O 入口（实现 IModbusClient）。
    std::shared_ptr<transport::ModbusIoExecutor> m_io;
    // 连接监控直接观察底层 client（executor 只是串行化层，连接状态在底层）。
    // mutable：connectionState()（const）查询前同步一次底层连接状态。
    mutable transport::ConnectionMonitor m_monitor;
    topology::PlcTopologyReader m_topology;
    telemetry::PlcSnapshotReader m_telemetry;
    telemetry::SafetyStateReader m_safety;   // 阶段 2：急停只读（共享 m_io 通道）
    telemetry::AxisParameterReader m_paramReader;  // 阶段 3：参数区读回（共享 m_io 通道）
    command::PlcAxisCommandWriter m_axisWriter;
    command::PlcGantryCommandWriter m_gantryWriter;
};

}  // namespace plc_vnext
