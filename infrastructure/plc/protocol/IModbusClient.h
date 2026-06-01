// infrastructure/plc/protocol/IModbusClient.h
// P4: Modbus 客户端抽象接口（读写合并 + CommunicationResult 错误返回）
//
// 为 PlcPoller（读）和 PlcDevice（写）提供统一的 Modbus 传输能力。
// 所有方法返回 CommunicationResult，表达通讯层面的成功/失败。

#pragma once
#include "infrastructure/ISystemDriver.h"  // CommunicationResult
#include <vector>
#include <cstdint>

namespace plc::protocol {

/**
 * @brief Modbus 客户端抽象接口（读写合并）
 *
 * @details
 * 为 PlcPoller（读）和 PlcDevice（写）提供统一的 Modbus 传输能力。
 * 所有方法返回 CommunicationResult，表达通讯层面的成功/失败。
 *
 * 读通道:
 *   - readCoils()           FC01 — 读线圈
 *   - readHoldingRegisters() FC03 — 读保持寄存器
 *
 * 写通道:
 *   - writeSingleCoil()      FC05 — 写单个线圈
 *   - writeSingleRegister()  FC06 — 写单个保持寄存器
 *   - writeMultipleRegisters() FC10 — 写多个保持寄存器
 *
 * 设计原则:
 *   1. CommunicationResult 只表达"Modbus 帧是否成功送达/收到有效响应"
 *   2. 不表达 PLC 执行结果——那是 pollFeedback 的职责（参照 ISystemDriver 设计）
 *   3. 读接口通过引用参数返回 payload，CommunicationResult 表达通讯结果
 */
class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    // ═══════════════════════════════════════
    //  读通道 — PlcPoller 使用
    // ═══════════════════════════════════════

    /// @brief FC01 — 读线圈 (Read Coils)
    /// @param startAddress 起始线圈地址
    /// @param count 读取数量
    /// @param[out] payload 位数据（MSB 打包），仅在 CommunicationResult::ok() 时有效
    /// @return 通讯结果，含成功/失败/可重试信息
    virtual CommunicationResult readCoils(
        uint16_t startAddress,
        uint16_t count,
        std::vector<uint8_t>& payload) = 0;

    /// @brief FC03 — 读保持寄存器 (Read Holding Registers)
    /// @param startAddress 起始寄存器地址
    /// @param count 读取数量
    /// @param[out] payload 16 位寄存器值数组，仅在 CommunicationResult::ok() 时有效
    /// @return 通讯结果
    virtual CommunicationResult readHoldingRegisters(
        uint16_t startAddress,
        uint16_t count,
        std::vector<uint16_t>& payload) = 0;

    // ═══════════════════════════════════════
    //  写通道 — PlcDevice 使用
    // ═══════════════════════════════════════

    /// @brief FC05 — 写单个线圈 (Write Single Coil)
    /// @param address 线圈地址
    /// @param value   true = ON (0xFF00), false = OFF (0x0000)
    /// @return 通讯结果
    virtual CommunicationResult writeSingleCoil(
        uint16_t address,
        bool value) = 0;

    /// @brief FC06 — 写单个保持寄存器 (Write Single Holding Register)
    /// @param address 寄存器地址
    /// @param value   16 位值
    /// @return 通讯结果
    virtual CommunicationResult writeSingleRegister(
        uint16_t address,
        uint16_t value) = 0;

    /// @brief FC10 — 写多个保持寄存器 (Write Multiple Holding Registers)
    /// @param startAddress 起始寄存器地址
    /// @param values       连续的 16 位值序列
    /// @return 通讯结果
    virtual CommunicationResult writeMultipleRegisters(
        uint16_t startAddress,
        const std::vector<uint16_t>& values) = 0;
};

} // namespace plc::protocol
