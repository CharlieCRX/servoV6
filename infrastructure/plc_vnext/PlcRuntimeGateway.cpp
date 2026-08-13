// ============================================================================
// PlcRuntimeGateway.cpp —— Step 10 Gateway: 组合门面实现
// ============================================================================
#include "infrastructure/plc_vnext/PlcRuntimeGateway.h"

#include <string>
#include <utility>

#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/layout/SystemCoilLayout.h"

namespace plc_vnext {
namespace {

// 默认龙门组准入策略：当前 PLC 事实仅 A 组（Group 0）开放。
// 用于 Gateway 未显式注入 gate 时的默认值，防止"B 组禁用"只依赖调用方记得传。
bool defaultGantryGroupGate(contracts::PlcGroupIndex g) {
    return g.value() == 0;
}

}  // namespace

PlcRuntimeGateway::PlcRuntimeGateway(
    transport::IModbusClientPtr client,
    command::PlcGantryCommandWriter::GroupGate groupGate)
    // 创建唯一共享串行化通道，注入所有 reader/writer（executor 实现 IModbusClient，
    // shared_ptr 向上转型）；连接监控直接观察底层 client。
    : m_io(std::make_shared<transport::ModbusIoExecutor>(client)),
      m_monitor(client),
      m_topology(m_io),
      m_telemetry(m_io),
      m_safety(m_io),
      m_paramReader(m_io),
      m_axisWriter(m_io),
      m_gantryWriter(m_io,
                     groupGate ? std::move(groupGate)
                               : command::PlcGantryCommandWriter::GroupGate(
                                     defaultGantryGroupGate)) {}

contracts::ReadResult<contracts::TopologySnapshot> PlcRuntimeGateway::readTopology() {
    // 只透传 topology 双读 reader 的结果（Transport/Decode/RevisionChanged）。
    return m_topology.read();
}

contracts::ReadResult<contracts::RuntimeSnapshot> PlcRuntimeGateway::readRuntime() {
    auto snap = m_telemetry.read();
    if (snap.quality == contracts::SnapshotQuality::Trusted) {
        return contracts::ReadResult<contracts::RuntimeSnapshot>::success(std::move(snap));
    }
    // 非 Trusted（Partial/TransportFailed/Stale）：不提供“可信完整快照”。
    // Gateway 不做业务降级决策，统一以 Transport 失败上报，由上层决定策略。
    return contracts::ReadResult<contracts::RuntimeSnapshot>::failure(
        contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::Transport,
        "PlcRuntimeGateway: runtime snapshot not trusted (quality=" +
            std::to_string(static_cast<int>(snap.quality)) + ")");
}

contracts::ReadResult<contracts::SafetySnapshot> PlcRuntimeGateway::readSafety() {
    // 经共享 m_io 串行通道读取 M224/M225（与 readTopology/readRuntime 同一通道）。
    auto snap = m_safety.read();
    if (snap.trusted) {
        return contracts::ReadResult<contracts::SafetySnapshot>::success(std::move(snap));
    }
    // 未知急停状态（通讯/空数据）→ 以 Transport 失败上报，绝不冒充“正常”。
    return contracts::ReadResult<contracts::SafetySnapshot>::failure(
        contracts::ReadResult<contracts::SafetySnapshot>::FailureKind::Transport,
        snap.diagnostic.empty()
            ? "PlcRuntimeGateway: safety snapshot not trusted"
            : snap.diagnostic);
}

contracts::ReadResult<contracts::AxisParameterSnapshot>
PlcRuntimeGateway::readAxisParameters(contracts::PlcAxisSlot slot) {
    // 阶段 3：参数区读回确认。经共享 m_io 串行通道读取单槽位参数区。
    auto snap = m_paramReader.read(slot.value());
    if (snap.trusted) {
        return contracts::ReadResult<contracts::AxisParameterSnapshot>::success(
            std::move(snap));
    }
    return contracts::ReadResult<contracts::AxisParameterSnapshot>::failure(
        contracts::ReadResult<contracts::AxisParameterSnapshot>::FailureKind::Transport,
        "PlcRuntimeGateway: axis parameter snapshot not trusted (slot=" +
            std::to_string(slot.value()) + ")");
}

contracts::CommunicationResult PlcRuntimeGateway::triggerEmergencyStop() {
    // 设备急停：M224=ON（锁存）。经共享 m_io 通道，只提交不做读回。
    return m_io->writeSingleCoil(
        static_cast<uint16_t>(layout::emergencyStop().value()), true);
}

contracts::CommunicationResult PlcRuntimeGateway::requestEmergencyStopRelease() {
    // 解除急停：M225=ON（PLC 自复位，只写 ON；解除后 M224/M225 自动 OFF）。
    return m_io->writeSingleCoil(
        static_cast<uint16_t>(layout::emergencyStopRelease().value()), true);
}


contracts::CommunicationResult PlcRuntimeGateway::writeAxis(
    contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) {
    // 单轴写经共享通道：单笔写自动受 m_io 全局闸门约束，不与龙门成组提交交叉。
    return m_axisWriter.write(slot, cmd);
}

contracts::CommunicationResult PlcRuntimeGateway::submitGantryRequest(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    // 兼容入口：只透传底层通讯结果；提交阶段信息见 submitGantryRequestDetailed()。
    return submitGantryRequestDetailed(g, req).result;
}

contracts::GantrySubmitResult PlcRuntimeGateway::submitGantryRequestDetailed(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    // 组事务闸门：把 Command→RequestSeq 包进共享通道的全局临界区，保证单轴写 /
    // telemetry 读 / 其它龙门提交不会插入其间（跨组件成组原子）。执行期间锁被
    // 持有，其它线程的任何 I/O 都必须等待。返回完整提交阶段与本次 requestSeq，
    // 供上层/ack reader 区分"确定未提交"与"提交结果未知"（CommitUncertain）。
    return m_io->executeGroup([&] { return m_gantryWriter.submitDetailed(g, req); });
}

contracts::ConnectionState PlcRuntimeGateway::connectionState() const {
    // 每次查询前与底层同步一次连接状态（断连/重连由 client 驱动）。
    m_monitor.refresh();
    contracts::ConnectionState state;
    state.connected = m_monitor.isConnected();
    return state;
}

void PlcRuntimeGateway::requestReconnect() {
    // 只委托 transport 连接层（ConnectionMonitor → IModbusClient）；本层不重放命令。
    m_monitor.requestReconnect();
}

}  // namespace plc_vnext
