// ============================================================================
// ModbusRequest.h —— Step 5 transport: 单次 Modbus 读/写请求值对象
// ============================================================================
// 用标签化联合表达"一次 Modbus 事务"，供 ModbusIoExecutor 在单通道上串行执行。
// 本类型只承载 0 基址原始线圈/寄存器语义，不认识槽位/轴/业务。
// ============================================================================
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

namespace plc_vnext::transport {

struct ModbusRequest {
    enum class Type {
        ReadCoils,              // FC01
        ReadHoldingRegisters,   // FC03
        WriteSingleCoil,        // FC05
        WriteSingleRegister,    // FC06
        WriteMultipleRegisters  // FC10
    };

    Type type = Type::ReadHoldingRegisters;

    /// 0 基址起始地址（读：起始地址；写：目标地址/起始地址）
    uint16_t address = 0;

    /// 读数量（ReadCoils / ReadHoldingRegisters）；WriteMultipleRegisters 时等于 values.size()
    uint16_t count = 0;

    /// WriteSingleCoil：true=ON(0xFF00) / false=OFF(0x0000)
    bool coilValue = false;

    /// WriteSingleRegister：单个 16 位值
    uint16_t registerValue = 0;

    /// WriteMultipleRegisters：连续 16 位值序列
    std::vector<uint16_t> values;

    // —— 便捷工厂，便于构造器使用 ——
    static ModbusRequest readCoils(uint16_t addr, uint16_t n) {
        ModbusRequest r;
        r.type = Type::ReadCoils;
        r.address = addr;
        r.count = n;
        return r;
    }
    static ModbusRequest readHoldingRegisters(uint16_t addr, uint16_t n) {
        ModbusRequest r;
        r.type = Type::ReadHoldingRegisters;
        r.address = addr;
        r.count = n;
        return r;
    }
    static ModbusRequest writeSingleCoil(uint16_t addr, bool value) {
        ModbusRequest r;
        r.type = Type::WriteSingleCoil;
        r.address = addr;
        r.coilValue = value;
        return r;
    }
    static ModbusRequest writeSingleRegister(uint16_t addr, uint16_t value) {
        ModbusRequest r;
        r.type = Type::WriteSingleRegister;
        r.address = addr;
        r.registerValue = value;
        return r;
    }
    static ModbusRequest writeMultipleRegisters(uint16_t addr,
                                                std::vector<uint16_t> vals) {
        ModbusRequest r;
        r.type = Type::WriteMultipleRegisters;
        r.address = addr;
        r.values = std::move(vals);
        r.count = static_cast<uint16_t>(r.values.size());
        return r;
    }
};

}  // namespace plc_vnext::transport
