// ============================================================================
// ModbusIoExecutor.h —— Step 5 transport: 单通道串行化 I/O
// ============================================================================
// 职责：把"并发提交的多线程事务"串行化为单通道（同一时刻只执行一笔整事务，
// 从发出请求到收齐响应之间不允许插入其它请求），防止事务交叉 / 帧交织。
//
// 底层 IModbusClient 的 readXxx/writeXxx 都是阻塞且自洽的原子调用；此处用
// 一把互斥锁把"整笔请求→响应"包成一个临界区即可保证串行。失败请求与成功请求
// 按提交顺序依次返回，不吞掉后续响应。
//
// transport 不认识槽位/轴；本类不缓存、不重放、不超时判定（超时在底层客户端）。
// ============================================================================
#pragma once

#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"
#include "infrastructure/plc_vnext/transport/ModbusRequest.h"
#include "infrastructure/plc_vnext/transport/ModbusResponse.h"

namespace plc_vnext::transport {

class ModbusIoExecutor : public IModbusClient {
public:
    /// @param client 底层设备 I/O 客户端（Fake 或真实实现），生命周期由调用方持有
    explicit ModbusIoExecutor(IModbusClientPtr client);

    // —— 核心：串行执行一笔 Modbus 事务 ——
    contracts::CommunicationResult execute(const ModbusRequest& request,
                                           ModbusResponse& response);

    // —— 便捷类型化入口（同样串行） ——
    contracts::CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                             std::vector<uint8_t>& payload) override;
    contracts::CommunicationResult readHoldingRegisters(uint16_t startAddress,
                                                        uint16_t count,
                                                        std::vector<uint16_t>& payload) override;
    contracts::CommunicationResult writeSingleCoil(uint16_t address, bool value) override;
    contracts::CommunicationResult writeSingleRegister(uint16_t address,
                                                       uint16_t value) override;
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t startAddress, const std::vector<uint16_t>& values) override;

    // —— 连接管理：委托底层客户端 ——
    bool isConnected() const override;
    void requestReconnect() override;

    // —— 组事务闸门（Step 10 共享通道的成组原子性）——
    // 在持有同一把全局锁的临界区内执行 fn（fn 内可再次调用本类单笔 I/O；
    // m_ioMutex 为递归锁，同线程可重入）。上层用它把"Command→RequestSeq 两笔
    // 事务"包成一个临界区，保证其它线程（单轴写 / telemetry 读 / 其它龙门提交）
    // 不会插入其间——单笔锁只保证单笔事务原子，无法保证多笔成组。
    template <typename F>
    auto executeGroup(F&& fn) -> decltype(fn()) {
        std::lock_guard<std::recursive_mutex> lock(m_ioMutex);
        return fn();
    }

private:
    IModbusClientPtr m_client;
    // 单通道串行化闸门。recursive_mutex：允许 executeGroup 组锁内重入单笔 execute。
    std::recursive_mutex m_ioMutex;
};

}  // namespace plc_vnext::transport
