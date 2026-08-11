// ============================================================================
// AsioModbusTcpClient.h —— Step 5 transport: 真实 socket 客户端（迁移待办桩）
// ============================================================================
// 本文件当前是"待定/迁移待办"桩：真实 Modbus TCP 客户端需要连接硬件，且要
// 复用旧的 Asio + Logger 实现（infrastructure/plc/protocol/AsioModbusTcpClient.*）。
//
// 依据《PLC通讯基础设施层重构——TDD实施文档.md》Step 5 完成条件：
//   "旧 AsioModbusTcpClient 迁移前后行为不变（用旧测试作回归锚点），
//    或在新 transport 下补齐等价测试。"
// 等价测试需要真实 PLC（属于 Step 11/12 的真实只读/写入验收活动），因此本
// 桩暂不编译进 plc_vnext 静态库，避免在 M1 无硬件阶段引入未验证的 asio+logger
// 大段实现。迁移时：
//   1) 把旧实现迁入本类并改为继承 transport::IModbusClient；
//   2) 返回值改用 contracts::CommunicationResult（去掉对 ISystemDriver 依赖）；
//   3) 保持 0 基址透传，业务偏移一律由 layout 层负责；
//   4) 复用旧 tests/infrastructure/protocol/test_asio_modbus_client.cpp 作回归锚点。
//
// 接口形状先与 transport::IModbusClient 对齐，便于上层（ModbusIoExecutor /
// ConnectionMonitor / FakeModbusClient）以同一抽象注入真实实现。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::transport {

/// @brief 真实 Modbus TCP 客户端（基于 Standalone Asio）。
/// 迁移完成前仅为接口占位；迁移见文件头注释。
class AsioModbusTcpClient final : public IModbusClient {
public:
    AsioModbusTcpClient() = default;

    // IModbusClient —— 迁移前暂不提供实现，编译期不被引用。
    bool isConnected() const override { return false; }
    void requestReconnect() override {}
    contracts::CommunicationResult readCoils(uint16_t, uint16_t,
                                             std::vector<uint8_t>&) override {
        return contracts::CommunicationResult::disconnected(
            "AsioModbusTcpClient: 迁移待办，未实现");
    }
    contracts::CommunicationResult readHoldingRegisters(
        uint16_t, uint16_t, std::vector<uint16_t>&) override {
        return contracts::CommunicationResult::disconnected(
            "AsioModbusTcpClient: 迁移待办，未实现");
    }
    contracts::CommunicationResult writeSingleCoil(uint16_t, bool) override {
        return contracts::CommunicationResult::disconnected(
            "AsioModbusTcpClient: 迁移待办，未实现");
    }
    contracts::CommunicationResult writeSingleRegister(uint16_t, uint16_t) override {
        return contracts::CommunicationResult::disconnected(
            "AsioModbusTcpClient: 迁移待办，未实现");
    }
    contracts::CommunicationResult writeMultipleRegisters(
        uint16_t, const std::vector<uint16_t>&) override {
        return contracts::CommunicationResult::disconnected(
            "AsioModbusTcpClient: 迁移待办，未实现");
    }
};

}  // namespace plc_vnext::transport
