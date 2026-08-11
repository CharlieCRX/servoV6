// ============================================================================
// ConnectionMonitor.cpp —— Step 5 transport: 连接监控实现
// ============================================================================
#include "infrastructure/plc_vnext/transport/ConnectionMonitor.h"

namespace plc_vnext::transport {

ConnectionMonitor::ConnectionMonitor(IModbusClientPtr client)
    : m_client(std::move(client)) {
    refresh();
}

void ConnectionMonitor::onTransactionResult(
    const contracts::CommunicationResult& result) {
    std::lock_guard<std::mutex> lock(m_mtx);

    if (result.ok()) {
        // 成功：恢复已连接，清零失败计数与退避。
        m_connected = true;
        m_consecutiveFailures = 0;
        m_backoffMs = 0;
        return;
    }

    // 失败：计入连续失败；连接性失败明确标记为断连。
    ++m_consecutiveFailures;
    m_backoffMs = computeBackoff(m_consecutiveFailures);

    if (result.isDisconnected() || result.status == contracts::CommunicationResult::Status::NetworkError) {
        m_connected = false;
    }
    // Timeout / Busy / ProtocolError / InvalidResponse：连接未必断开，
    // 保留 m_connected 现状，仅累计失败进入退避，避免误判为已断线。
}

void ConnectionMonitor::refresh() {
    if (!m_client) return;
    std::lock_guard<std::mutex> lock(m_mtx);
    m_connected = m_client->isConnected();
}

void ConnectionMonitor::requestReconnect() {
    // 只委托给底层连接层；本层不重放、不重发任何命令。
    if (m_client) m_client->requestReconnect();
}

bool ConnectionMonitor::isConnected() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_connected;
}

unsigned ConnectionMonitor::consecutiveFailures() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_consecutiveFailures;
}

bool ConnectionMonitor::isBackoffActive() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_backoffMs > 0;
}

unsigned ConnectionMonitor::backoffMs() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_backoffMs;
}

unsigned ConnectionMonitor::computeBackoff(unsigned failures) const {
    // 指数退避：base << (n-1)，封顶 kMaxBackoffMs。
    unsigned long long value = kBaseBackoffMs;
    for (unsigned i = 1; i < failures; ++i) {
        value <<= 1;
        if (value >= kMaxBackoffMs) break;
    }
    if (value > kMaxBackoffMs) value = kMaxBackoffMs;
    return static_cast<unsigned>(value);
}

}  // namespace plc_vnext::transport
