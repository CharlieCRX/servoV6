#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/fake/FakeModbusClient.h"

namespace plc_vnext::fake {

using contracts::CommunicationResult;

// ============================================================
//  脚本化：寄存器 / 线圈预置
// ============================================================
void FakeModbusClient::setHoldingRegister(uint16_t address, uint16_t value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_regs[address] = value;
}

void FakeModbusClient::setCoil(uint16_t address, bool value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_coils[address] = value;
}

// ============================================================
//  脚本化：通讯故障
// ============================================================
void FakeModbusClient::scriptTransportFailure(
    contracts::CommunicationResult::Status status, const std::string& diagnostic) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_faultQueue.push_back(status);
    m_faultDiag = diagnostic;
}

void FakeModbusClient::setFailureThreshold(uint16_t threshold) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_hasThreshold = true;
    m_threshold = threshold;
}

void FakeModbusClient::clearFailureThreshold() {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_hasThreshold = false;
}

// ============================================================
//  连接状态
// ============================================================
void FakeModbusClient::setConnected(bool connected) {
    std::lock_guard<std::mutex> lock(m_mtx);
    m_connected = connected;
}

bool FakeModbusClient::isConnected() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_connected;
}

void FakeModbusClient::requestReconnect() {
    std::lock_guard<std::mutex> lock(m_mtx);
    ++m_reconnectCount;
    // 模拟重连成功：默认恢复为已连接。
    m_connected = true;
}

unsigned FakeModbusClient::requestReconnectCount() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_reconnectCount;
}

// ============================================================
//  写记录访问器（返回快照，杜绝并发下对内部 vector 的数据竞争）
// ============================================================
std::vector<FakeModbusClient::WrittenCoil>
FakeModbusClient::writtenCoils() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_writtenCoils;
}

std::vector<FakeModbusClient::WrittenRegister>
FakeModbusClient::writtenRegisters() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_writtenRegisters;
}

std::vector<FakeModbusClient::WrittenMulti>
FakeModbusClient::writtenMulti() const {
    std::lock_guard<std::mutex> lock(m_mtx);
    return m_writtenMulti;
}

// ============================================================
//  读回
// ============================================================
std::optional<uint16_t> FakeModbusClient::readRegister(uint16_t address) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_regs.find(address);
    if (it == m_regs.end()) return std::nullopt;
    return it->second;
}

std::optional<bool> FakeModbusClient::readCoil(uint16_t address) const {
    std::lock_guard<std::mutex> lock(m_mtx);
    auto it = m_coils.find(address);
    if (it == m_coils.end()) return std::nullopt;
    return it->second;
}

// ============================================================
//  内部故障判定
// ============================================================
CommunicationResult FakeModbusClient::consumeScriptedFault() {
    if (!m_faultQueue.empty()) {
        auto status = m_faultQueue.front();
        m_faultQueue.erase(m_faultQueue.begin());
        std::string diag = m_faultDiag;
        m_faultDiag.clear();
        return CommunicationResult{status, 0, diag};
    }
    // 默认 Sent —— 表示"未注入故障"。
    return CommunicationResult::sent();
}

// ============================================================
//  连接 / 地址范围校验（放行返回 sent()）
// ============================================================
CommunicationResult FakeModbusClient::ensureConnected() {
    if (!m_connected) {
        return CommunicationResult::disconnected(
            "FakeModbusClient: 未连接，拒绝 I/O");
    }
    return CommunicationResult::sent();
}

CommunicationResult FakeModbusClient::ensureValidRange(uint32_t start,
                                                       uint32_t count) {
    // 0 基址 uint16 地址空间为 0..65535；start+count 越过 65536 即回绕，必须拒绝。
    if (count == 0 || start + count > 65536u) {
        return CommunicationResult{
            CommunicationResult::Status::ProtocolError, 0,
            "FakeModbusClient: 请求地址范围越界或 count 为 0"};
    }
    return CommunicationResult::sent();
}

// ============================================================
//  IModbusClient 实现
// ============================================================
CommunicationResult FakeModbusClient::readCoils(uint16_t startAddress,
                                                uint16_t count,
                                                std::vector<uint8_t>& payload) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (auto r = ensureConnected(); !r.ok()) return r;
    if (auto r = ensureValidRange(startAddress, count); !r.ok()) return r;
    auto fault = consumeScriptedFault();
    if (!fault.ok()) return fault;
    if (m_hasThreshold && startAddress >= m_threshold) {
        return CommunicationResult{
            CommunicationResult::Status::NetworkError, 0,
            "FakeModbusClient: readCoils above failure threshold"};
    }

    payload.assign((count + 7u) / 8u, 0u);
    for (uint16_t i = 0; i < count; ++i) {
        auto it = m_coils.find(startAddress + i);
        if (it != m_coils.end() && it->second) {
            payload[i / 8u] |= static_cast<uint8_t>(1u << (i % 8u));
        }
    }
    return CommunicationResult::sent();
}

CommunicationResult FakeModbusClient::readHoldingRegisters(
    uint16_t startAddress, uint16_t count, std::vector<uint16_t>& payload) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (auto r = ensureConnected(); !r.ok()) return r;
    if (auto r = ensureValidRange(startAddress, count); !r.ok()) return r;
    auto fault = consumeScriptedFault();
    if (!fault.ok()) return fault;
    if (m_hasThreshold && startAddress >= m_threshold) {
        return CommunicationResult{
            CommunicationResult::Status::NetworkError, 0,
            "FakeModbusClient: readHoldingRegisters above failure threshold"};
    }

    payload.clear();
    payload.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        auto it = m_regs.find(startAddress + i);
        payload.push_back(it == m_regs.end() ? 0u : it->second);
    }
    return CommunicationResult::sent();
}

CommunicationResult FakeModbusClient::writeSingleCoil(uint16_t address,
                                                      bool value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (auto r = ensureConnected(); !r.ok()) return r;
    if (auto r = ensureValidRange(address, 1); !r.ok()) return r;
    auto fault = consumeScriptedFault();
    if (!fault.ok()) return fault;
    if (m_hasThreshold && address >= m_threshold) {
        return CommunicationResult{
            CommunicationResult::Status::NetworkError, 0,
            "FakeModbusClient: writeSingleCoil above failure threshold"};
    }
    m_coils[address] = value;
    m_writtenCoils.push_back(WrittenCoil{address, value});
    return CommunicationResult::sent();
}

CommunicationResult FakeModbusClient::writeSingleRegister(uint16_t address,
                                                          uint16_t value) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (auto r = ensureConnected(); !r.ok()) return r;
    if (auto r = ensureValidRange(address, 1); !r.ok()) return r;
    auto fault = consumeScriptedFault();
    if (!fault.ok()) return fault;
    if (m_hasThreshold && address >= m_threshold) {
        return CommunicationResult{
            CommunicationResult::Status::NetworkError, 0,
            "FakeModbusClient: writeSingleRegister above failure threshold"};
    }
    m_regs[address] = value;
    m_writtenRegisters.push_back(WrittenRegister{address, value});
    return CommunicationResult::sent();
}

CommunicationResult FakeModbusClient::writeMultipleRegisters(
    uint16_t startAddress, const std::vector<uint16_t>& values) {
    std::lock_guard<std::mutex> lock(m_mtx);
    if (auto r = ensureConnected(); !r.ok()) return r;
    if (auto r = ensureValidRange(startAddress,
                                  static_cast<uint32_t>(values.size()));
        !r.ok())
        return r;
    auto fault = consumeScriptedFault();
    if (!fault.ok()) return fault;
    if (m_hasThreshold && startAddress >= m_threshold) {
        return CommunicationResult{
            CommunicationResult::Status::NetworkError, 0,
            "FakeModbusClient: writeMultipleRegisters above failure threshold"};
    }
    for (size_t i = 0; i < values.size(); ++i) {
        m_regs[startAddress + static_cast<uint16_t>(i)] = values[i];
    }
    m_writtenMulti.push_back(WrittenMulti{startAddress, values});
    return CommunicationResult::sent();
}

}  // namespace plc_vnext::fake
