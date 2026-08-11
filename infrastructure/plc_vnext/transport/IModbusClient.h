// ============================================================================
// IModbusClient.h —— Step 5 transport: Modbus 客户端窄接口
// ============================================================================
// transport 是唯一的设备 I/O 抽象。本接口不认识槽位/轴/拓扑/联动等任何业务；
// 只表达"读/写某 0 基址 Modbus 地址的一段原始线圈/保持寄存器，并报告通讯结果"。
//
// 与旧 infrastructure/plc/protocol/IModbusClient.h 形状对齐，但：
//   1) 返回值改用 contracts::CommunicationResult（独立定义，零 include 依赖），
//      不再依赖 infrastructure/ISystemDriver.h，从而与旧协议/ISystemDriver 解耦。
//   2) 地址一律为 0 基址（透传，不做 +1 / 40001 偏移，Modbus 协议偏移由 layout 层负责）。
//
// 语义边界（与 contracts::CommunicationResult 一致）：
//   成功 = 收到正常 Modbus 响应且无异常码（Sent）；不表达 PLC 执行结果，
//   不表达物理动作状态（那是 telemetry / pollFeedback 的职责）。
// ============================================================================
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"

namespace plc_vnext::transport {

class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    // ═══════════════════════════════════════════
    //  连接管理（供 ConnectionMonitor 委托 / 查询）
    // ═══════════════════════════════════════════

    /// @brief 当前 TCP 连接状态；线程安全
    [[nodiscard]] virtual bool isConnected() const = 0;

    /// @brief 请求立即重连（跳过自动重连等待间隔）；委托给底层连接层
    virtual void requestReconnect() = 0;

    // ═══════════════════════════════════════════
    //  读通道
    // ═══════════════════════════════════════════

    /// @brief FC01 —— 读线圈（0 基址；位数据 MSB 打包进 payload）
    /// @param[out] payload 仅当 result.ok() 时有效
    [[nodiscard]] virtual contracts::CommunicationResult readCoils(
        uint16_t startAddress, uint16_t count, std::vector<uint8_t>& payload) = 0;

    /// @brief FC03 —— 读保持寄存器（0 基址）
    /// @param[out] payload 仅当 result.ok() 时有效
    [[nodiscard]] virtual contracts::CommunicationResult readHoldingRegisters(
        uint16_t startAddress, uint16_t count, std::vector<uint16_t>& payload) = 0;

    // ═══════════════════════════════════════════
    //  写通道
    // ═══════════════════════════════════════════

    /// @brief FC05 —— 写单个线圈（0 基址）
    [[nodiscard]] virtual contracts::CommunicationResult writeSingleCoil(
        uint16_t address, bool value) = 0;

    /// @brief FC06 —— 写单个保持寄存器（0 基址）
    [[nodiscard]] virtual contracts::CommunicationResult writeSingleRegister(
        uint16_t address, uint16_t value) = 0;

    /// @brief FC10 —— 写多个连续保持寄存器（0 基址）
    [[nodiscard]] virtual contracts::CommunicationResult writeMultipleRegisters(
        uint16_t startAddress, const std::vector<uint16_t>& values) = 0;
};

/// transport 层的实现都以 shared_ptr 注入，便于测试时替换为 FakeModbusClient。
using IModbusClientPtr = std::shared_ptr<IModbusClient>;

}  // namespace plc_vnext::transport
