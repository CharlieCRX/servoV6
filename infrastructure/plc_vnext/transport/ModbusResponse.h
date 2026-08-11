// ============================================================================
// ModbusResponse.h —— Step 5 transport: 单次 Modbus 响应值对象
// ============================================================================
// 与 ModbusRequest 配对：一次事务的通讯结果 + 读回 payload。
// 本类型只承载原始线圈/寄存器数据，不认识槽位/轴/业务。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"

namespace plc_vnext::transport {

struct ModbusResponse {
    contracts::CommunicationResult result = contracts::CommunicationResult::sent();

    /// FC01 读回的位数据（MSB 打包），仅当 result.ok() 时有意义
    std::vector<uint8_t> bits;

    /// FC03 读回的 16 位寄存器值，仅当 result.ok() 时有意义
    std::vector<uint16_t> words;

    [[nodiscard]] bool ok() const { return result.ok(); }
};

}  // namespace plc_vnext::transport
