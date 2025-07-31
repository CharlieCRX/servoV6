// domain/MotorRegisterAccessor.h
#pragma once
#include "ICommProtocol.h"
#include <vector>

class MotorRegisterAccessor {
public:
    explicit MotorRegisterAccessor(ICommProtocol* protocol);

    // 寄存器写入操作 (使用HOLDING_REGISTER)
    bool writeReg16(int32_t motorID, uint32_t regAddr, uint16_t value);
    bool writeReg32(int32_t motorID, uint32_t regAddr, uint32_t value);
    bool writeReg64(int32_t motorID, uint32_t regAddr, uint64_t value);

    // 寄存器读取操作 (使用INPUT_REGISTER)
    bool readReg16(int32_t motorID, uint32_t regAddr, uint16_t& outVal);
    bool readReg32(int32_t motorID, uint32_t regAddr, uint32_t& outVal);
    bool readReg64(int32_t motorID, uint32_t regAddr, uint64_t& outVal);

    // 批量寄存器操作
    bool readRegBlock(int32_t motorID, uint32_t startAddr, uint16_t* buffer, size_t count);
    bool writeRegBlock(int32_t motorID, uint32_t startAddr, const uint16_t* values, size_t count);

private:
    ICommProtocol* m_protocol; // 协议实现（不拥有所有权）

    // 寄存器组合与拆分辅助函数
    uint64_t combineRegistersTo64(const std::vector<uint16_t>& data) const;
    std::vector<uint16_t> splitToRegisters(uint64_t value, size_t count) const;
};
