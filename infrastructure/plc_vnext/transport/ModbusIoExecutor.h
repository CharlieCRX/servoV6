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
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"
#include "infrastructure/plc_vnext/transport/ModbusRequest.h"
#include "infrastructure/plc_vnext/transport/ModbusResponse.h"

namespace plc_vnext::transport {

class ModbusIoExecutor {
public:
    /// @param client 底层设备 I/O 客户端（Fake 或真实实现），生命周期由调用方持有
    explicit ModbusIoExecutor(IModbusClientPtr client);

    // —— 核心：串行执行一笔 Modbus 事务 ——
    contracts::CommunicationResult execute(const ModbusRequest& request,
                                           ModbusResponse& response);

    // —— 便捷类型化入口（同样串行） ——
    contracts::CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                             std::vector<uint8_t>& payload);
    contracts::CommunicationResult readHoldingRegisters(uint16_t startAddress,
                                                        uint16_t count,
                                                        std::vector<uint16_t>& payload);
    contracts::CommunicationResult writeSingleCoil(uint16_t address, bool value);
    contracts::CommunicationResult writeSingleRegister(uint16_t address,
                                                       uint16_t value);
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t startAddress, const std::vector<uint16_t>& values);

private:
    IModbusClientPtr m_client;
    std::mutex m_ioMutex;  // 单通道串行化闸门
};

}  // namespace plc_vnext::transport
