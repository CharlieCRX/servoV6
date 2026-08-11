// ============================================================================
// FakeModbusClient.h —— Step 5 fake: 可脚本化的内存 Modbus 客户端
// ============================================================================
// 测试替身，供 topology/telemetry/gateway 全链路 TDD 使用。它：
//   - 用 map 模拟 PLC RAM（0 基址稀疏 holding registers + coils）；
//   - 可脚本化"通讯故障"（按队列弹一次，或按地址阈值永久失败）；
//   - 记录所有写操作（地址/值原样记录，供断言零基址透传与写入序列）；
//   - 线程安全（内部互斥锁），可配合 ModbusIoExecutor 做并发串行化测试。
//
// 与真实 LibModbusClient / 旧 AsioModbusTcpClient 共用 IModbusClient 接口。
// ============================================================================
#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::fake {

class FakeModbusClient : public transport::IModbusClient {
public:
    // —— 脚本化：寄存器/线圈预置 ——
    void setHoldingRegister(uint16_t address, uint16_t value);
    void setCoil(uint16_t address, bool value);

    // —— 脚本化：通讯故障 ——
    /// 队首一次故障（按提交顺序消费）；空队列则不注入
    void scriptTransportFailure(contracts::CommunicationResult::Status status,
                                const std::string& diagnostic = "");
    /// 地址 >= threshold 的操作全部失败（address 永远为 0 基址透传判断）
    void setFailureThreshold(uint16_t threshold);
    void clearFailureThreshold();

    // —— 脚本化：连接状态 ——
    void setConnected(bool connected);
    bool isConnected() const override;
    void requestReconnect() override;
    unsigned requestReconnectCount() const;

    // —— 写记录（供断言；返回快照，避免对内部 vector 的并发数据竞争） ——
    struct WrittenCoil {
        uint16_t address;
        bool value;
    };
    struct WrittenRegister {
        uint16_t address;
        uint16_t value;
    };
    struct WrittenMulti {
        uint16_t startAddress;
        std::vector<uint16_t> values;
    };
    std::vector<WrittenCoil> writtenCoils() const;
    std::vector<WrittenRegister> writtenRegisters() const;
    std::vector<WrittenMulti> writtenMulti() const;

    // —— 读回（校验写入是否生效） ——
    std::optional<uint16_t> readRegister(uint16_t address) const;
    std::optional<bool> readCoil(uint16_t address) const;

    /// 读操作调用计数（供断言"只提交不读回"等契约）。FC01/FC03 均计入。
    unsigned readCount() const;

    // —— IModbusClient ——
    contracts::CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                             std::vector<uint8_t>& payload) override;
    contracts::CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count,
        std::vector<uint16_t>& payload) override;
    contracts::CommunicationResult writeSingleCoil(uint16_t address,
                                                   bool value) override;
    contracts::CommunicationResult writeSingleRegister(uint16_t address,
                                                       uint16_t value) override;
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t startAddress, const std::vector<uint16_t>& values) override;

private:
    // 每次读操作前先弹一次故障队列，保证故障按提交顺序生效。
    contracts::CommunicationResult consumeScriptedFault();

    // 断连时返回 disconnected；否则返回 sent()（表示"放行"）。调用方须已持有锁。
    contracts::CommunicationResult ensureConnected();
    // 地址范围校验：start+count 不得越过 65536（0 基址 uint16 地址空间 0..65535），
    // 避免 uint16_t 回绕到 0。非法返回失败，否则返回 sent()。调用方须已持有锁。
    contracts::CommunicationResult ensureValidRange(uint32_t start, uint32_t count);

    mutable std::mutex m_mtx;

    std::map<uint16_t, uint16_t> m_regs;  // 0 基址 holding registers
    std::map<uint16_t, bool> m_coils;     // 0 基址 coils

    bool m_connected = true;
    unsigned m_reconnectCount = 0;
    unsigned m_readCount = 0;

    std::vector<contracts::CommunicationResult::Status> m_faultQueue;
    std::string m_faultDiag;
    bool m_hasThreshold = false;
    uint16_t m_threshold = 0;

    std::vector<WrittenCoil> m_writtenCoils;
    std::vector<WrittenRegister> m_writtenRegisters;
    std::vector<WrittenMulti> m_writtenMulti;
};

}  // namespace plc_vnext::fake
