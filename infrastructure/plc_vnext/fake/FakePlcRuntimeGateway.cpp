// ============================================================================
// FakePlcRuntimeGateway.cpp —— Step 11 fake: 高层假实现
// ============================================================================
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"

#include <utility>

#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"

namespace plc_vnext::fake {

// ============================================================================
// 状态脚本
// ============================================================================
void FakePlcRuntimeGateway::setTopologySnapshot(contracts::TopologySnapshot snap) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_topology = std::move(snap);
}

void FakePlcRuntimeGateway::setRuntimeSnapshot(contracts::RuntimeSnapshot snap) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_runtime = std::move(snap);
}

void FakePlcRuntimeGateway::setSafetySnapshot(contracts::SafetySnapshot snap) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_safety = std::move(snap);
}

void FakePlcRuntimeGateway::setConnected(bool connected) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_connected = connected;
}

// ============================================================================
// 故障脚本
// ============================================================================
void FakePlcRuntimeGateway::scriptTopologyReadFailure(
    contracts::ReadResult<contracts::TopologySnapshot>::FailureKind kind,
    std::string diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_topoFailure = kind;
    m_topoDiag = std::move(diagnostic);
}

void FakePlcRuntimeGateway::clearTopologyReadFailure() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_topoFailure.reset();
    m_topoDiag.clear();
}

void FakePlcRuntimeGateway::scriptRuntimeReadFailure(
    contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind kind,
    std::string diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_runtimeFailure = kind;
    m_runtimeDiag = std::move(diagnostic);
}

void FakePlcRuntimeGateway::clearRuntimeReadFailure() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_runtimeFailure.reset();
    m_runtimeDiag.clear();
}

void FakePlcRuntimeGateway::scriptSafetyReadFailure(
    contracts::ReadResult<contracts::SafetySnapshot>::FailureKind kind,
    std::string diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_safetyFailure = kind;
    m_safetyDiag = std::move(diagnostic);
}

void FakePlcRuntimeGateway::clearSafetyReadFailure() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_safetyFailure.reset();
    m_safetyDiag.clear();
}

void FakePlcRuntimeGateway::scriptWriteAxisFailure(
    contracts::CommunicationResult::Status status, std::string diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_writeAxisFailure = status;
    m_writeAxisDiag = std::move(diagnostic);
}

void FakePlcRuntimeGateway::clearWriteAxisFailure() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_writeAxisFailure.reset();
    m_writeAxisDiag.clear();
}

void FakePlcRuntimeGateway::scriptGantrySubmitFailure(
    contracts::GantrySubmitState state, std::string diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_gantryFailure = state;
    m_gantryDiag = std::move(diagnostic);
}

void FakePlcRuntimeGateway::clearGantrySubmitFailure() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_gantryFailure.reset();
    m_gantryDiag.clear();
}

// ============================================================================
// 记录访问器
// ============================================================================
std::vector<FakePlcRuntimeGateway::WrittenAxis> FakePlcRuntimeGateway::writtenAxis() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_writtenAxis;
}

std::vector<FakePlcRuntimeGateway::GantrySubmission>
FakePlcRuntimeGateway::gantrySubmissions() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_gantrySubmissions;
}

unsigned FakePlcRuntimeGateway::requestReconnectCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_reconnectCount;
}

// ============================================================================
// IPlcRuntimeGateway
// ============================================================================
contracts::CommunicationResult FakePlcRuntimeGateway::failedResult(
    contracts::CommunicationResult::Status status, const std::string& diagnostic) {
    contracts::CommunicationResult r;
    r.status = status;
    r.diagnostic = diagnostic;
    return r;
}

contracts::ReadResult<contracts::TopologySnapshot> FakePlcRuntimeGateway::readTopology() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_topoFailure.has_value()) {
        return contracts::ReadResult<contracts::TopologySnapshot>::failure(
            *m_topoFailure, m_topoDiag);
    }
    if (m_topology.has_value()) {
        return contracts::ReadResult<contracts::TopologySnapshot>::success(*m_topology);
    }
    // 未脚本化 → Transport 失败，强制 application 测试显式声明预期快照。
    return contracts::ReadResult<contracts::TopologySnapshot>::failure(
        contracts::ReadResult<contracts::TopologySnapshot>::FailureKind::Transport,
        "FakePlcRuntimeGateway: no topology snapshot scripted");
}

contracts::ReadResult<contracts::RuntimeSnapshot> FakePlcRuntimeGateway::readRuntime() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_runtimeFailure.has_value()) {
        return contracts::ReadResult<contracts::RuntimeSnapshot>::failure(
            *m_runtimeFailure, m_runtimeDiag);
    }
    if (m_runtime.has_value()) {
        // 与真实 Gateway 语义一致：仅可信快照以 success 返回，非 Trusted 以
        // Transport 失败上报（上层决定降级策略），不把不可信数据冒充正常。
        if (m_runtime->quality == contracts::SnapshotQuality::Trusted) {
            return contracts::ReadResult<contracts::RuntimeSnapshot>::success(*m_runtime);
        }
        return contracts::ReadResult<contracts::RuntimeSnapshot>::failure(
            contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::Transport,
            "FakePlcRuntimeGateway: runtime snapshot not trusted");
    }
    return contracts::ReadResult<contracts::RuntimeSnapshot>::failure(
        contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::Transport,
        "FakePlcRuntimeGateway: no runtime snapshot scripted");
}

contracts::ReadResult<contracts::SafetySnapshot> FakePlcRuntimeGateway::readSafety() {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_safetyFailure.has_value()) {
        return contracts::ReadResult<contracts::SafetySnapshot>::failure(
            *m_safetyFailure, m_safetyDiag);
    }
    if (m_safety.has_value() && m_safety->trusted) {
        return contracts::ReadResult<contracts::SafetySnapshot>::success(*m_safety);
    }
    return contracts::ReadResult<contracts::SafetySnapshot>::failure(
        contracts::ReadResult<contracts::SafetySnapshot>::FailureKind::Transport,
        m_safety.has_value() ? "FakePlcRuntimeGateway: safety snapshot not trusted"
                             : "FakePlcRuntimeGateway: no safety snapshot scripted");
}


contracts::CommunicationResult FakePlcRuntimeGateway::writeAxis(
    contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) {
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_writtenAxis.push_back(WrittenAxis{slot, cmd});
        if (m_writeAxisFailure.has_value()) {
            return failedResult(*m_writeAxisFailure, m_writeAxisDiag);
        }
    }
    // 只提交，不做读回（与真实 writer 契约一致）。
    return contracts::CommunicationResult::sent();
}

contracts::CommunicationResult FakePlcRuntimeGateway::submitGantryRequest(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    return submitGantryRequestDetailed(g, req).result;
}

contracts::GantrySubmitResult FakePlcRuntimeGateway::submitGantryRequestDetailed(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    contracts::GantrySubmitResult out;
    out.requestSeq = req.requestSeq;
    {
        std::lock_guard<std::mutex> lock(m_mtx);
        m_gantrySubmissions.push_back(GantrySubmission{g, req});
        if (m_gantryFailure.has_value()) {
            out.state = *m_gantryFailure;
            out.result = failedResult(contracts::CommunicationResult::Status::NetworkError,
                                      m_gantryDiag);
            return out;
        }
    }
    out.state = contracts::GantrySubmitState::Submitted;
    out.result = contracts::CommunicationResult::sent();
    return out;
}

contracts::ConnectionState FakePlcRuntimeGateway::connectionState() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (m_connected) {
        return contracts::ConnectionState::connectedState("Fake connected");
    }
    return contracts::ConnectionState::disconnectedState("Fake disconnected");
}

void FakePlcRuntimeGateway::requestReconnect() {
    std::lock_guard<std::mutex> lock(m_mtx);
    ++m_reconnectCount;
}

}  // namespace plc_vnext::fake

