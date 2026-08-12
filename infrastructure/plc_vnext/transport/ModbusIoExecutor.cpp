// ============================================================================
// ModbusIoExecutor.cpp —— Step 5 transport: 单通道串行化 I/O 实现
// ============================================================================
#include "infrastructure/plc_vnext/transport/ModbusIoExecutor.h"

namespace plc_vnext::transport {

ModbusIoExecutor::ModbusIoExecutor(IModbusClientPtr client)
    : m_client(std::move(client)) {}

contracts::CommunicationResult ModbusIoExecutor::execute(
    const ModbusRequest& request, ModbusResponse& response) {
    // 整笔事务在互斥锁临界区内完成：从发起请求到填好响应之间，
    // 其它线程的任何 I/O 请求都必须等待，从根本上杜绝事务交叉。
    // recursive_mutex：允许 executeGroup 组锁内重入单笔 execute。
    std::lock_guard<std::recursive_mutex> lock(m_ioMutex);

    // 复位响应，避免上一次读回的数据残留被误当作本次结果。
    response = ModbusResponse{};

    switch (request.type) {
        case ModbusRequest::Type::ReadCoils: {
            auto res = m_client->readCoils(request.address, request.count,
                                           response.bits);
            response.result = res;
            if (!res.ok()) response.bits.clear();
            return res;
        }
        case ModbusRequest::Type::ReadHoldingRegisters: {
            auto res = m_client->readHoldingRegisters(request.address,
                                                      request.count,
                                                      response.words);
            response.result = res;
            if (!res.ok()) response.words.clear();
            return res;
        }
        case ModbusRequest::Type::WriteSingleCoil: {
            auto res = m_client->writeSingleCoil(request.address,
                                                 request.coilValue);
            response.result = res;
            return res;
        }
        case ModbusRequest::Type::WriteSingleRegister: {
            auto res = m_client->writeSingleRegister(request.address,
                                                     request.registerValue);
            response.result = res;
            return res;
        }
        case ModbusRequest::Type::WriteMultipleRegisters: {
            auto res = m_client->writeMultipleRegisters(request.address,
                                                        request.values);
            response.result = res;
            return res;
        }
    }
    // 不可达：switch 已覆盖全部枚举项。
    return contracts::CommunicationResult::disconnected(
        "ModbusIoExecutor: 未知请求类型");
}

contracts::CommunicationResult ModbusIoExecutor::readCoils(
    uint16_t startAddress, uint16_t count, std::vector<uint8_t>& payload) {
    ModbusResponse resp;
    auto res = execute(ModbusRequest::readCoils(startAddress, count), resp);
    payload = std::move(resp.bits);
    return res;
}

contracts::CommunicationResult ModbusIoExecutor::readHoldingRegisters(
    uint16_t startAddress, uint16_t count, std::vector<uint16_t>& payload) {
    ModbusResponse resp;
    auto res = execute(
        ModbusRequest::readHoldingRegisters(startAddress, count), resp);
    payload = std::move(resp.words);
    return res;
}

contracts::CommunicationResult ModbusIoExecutor::writeSingleCoil(
    uint16_t address, bool value) {
    ModbusResponse resp;
    return execute(ModbusRequest::writeSingleCoil(address, value), resp);
}

contracts::CommunicationResult ModbusIoExecutor::writeSingleRegister(
    uint16_t address, uint16_t value) {
    ModbusResponse resp;
    return execute(ModbusRequest::writeSingleRegister(address, value), resp);
}

contracts::CommunicationResult ModbusIoExecutor::writeMultipleRegisters(
    uint16_t startAddress, const std::vector<uint16_t>& values) {
    ModbusResponse resp;
    return execute(ModbusRequest::writeMultipleRegisters(startAddress, values),
                   resp);
}

bool ModbusIoExecutor::isConnected() const {
    return m_client && m_client->isConnected();
}

void ModbusIoExecutor::requestReconnect() {
    if (m_client) m_client->requestReconnect();
}

}  // namespace plc_vnext::transport
