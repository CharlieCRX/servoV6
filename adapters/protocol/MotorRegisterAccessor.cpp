// domain/MotorRegisterAccessor.cpp
#include "MotorRegisterAccessor.h"
#include "Logger.h" // 假设有日志工具
#include "RegisterType.h"

MotorRegisterAccessor::MotorRegisterAccessor(ICommProtocol* protocol)
    : m_protocol(protocol)
{
    if (protocol == nullptr) {
        LOG_CRITICAL("指针为空，程序终止！");
        std::terminate(); // 或 exit(EXIT_FAILURE);
    }
}

// 组合多个16位寄存器为64位值
uint64_t MotorRegisterAccessor::combineRegistersTo64(const std::vector<uint16_t>& data) const {
    uint64_t result = 0;
    for (size_t i = 0; i < data.size(); ++i) {
        result |= static_cast<uint64_t>(data[i]) << (16 * i);
    }
    return result;
}

// 将值拆分为多个16位寄存器
std::vector<uint16_t> MotorRegisterAccessor::splitToRegisters(uint64_t value, size_t count) const {
    std::vector<uint16_t> result;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        result.push_back(static_cast<uint16_t>((value >> (16 * i)) & 0xFFFF));
    }
    return result;
}

// ========== 16位寄存器操作 ==========
bool MotorRegisterAccessor::writeReg16(int32_t motorID, uint32_t regAddr, uint16_t value) {
    RegisterBlock regData;
    regData.data.push_back(value);

    bool ret = m_protocol->write(motorID, RegisterType::HOLDING_REGISTER, regAddr, regData);
    if (ret) {
        LOG_INFO("写入 uint16 寄存器[0x{:04X}]: {} (0x{:04X})", regAddr, value, value);
    } else {
        LOG_ERROR("写入 uint16 寄存器失败[0x{:04X}]", regAddr);
    }
    return ret;
}

bool MotorRegisterAccessor::readReg16(int32_t motorID, uint32_t regAddr, uint16_t& outVal) {
    RegisterBlock regData;
    if (!m_protocol->read(motorID, RegisterType::INPUT_REGISTER, regAddr, regAddr, regData)) {
        return false;
    }
    if (regData.data.size() < 1) {
        LOG_ERROR("读取 uint16 寄存器[0x{:04X}] 返回数据不足", regAddr);
        return false;
    }

    outVal = regData.data[0];
    LOG_INFO("读取 uint16 寄存器[0x{:04X}]: {} (0x{:04X})", regAddr, outVal, outVal);
    return true;
}

// ========== 32位寄存器操作 ==========
bool MotorRegisterAccessor::writeReg32(int32_t motorID, uint32_t regAddr, uint32_t value) {
    auto registers = splitToRegisters(value, 2);
    RegisterBlock regData;
    regData.data = registers;

    bool ret = m_protocol->write(motorID, RegisterType::HOLDING_REGISTER, regAddr, regData);
    if (ret) {
        LOG_INFO("写入 uint32 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:08X})",
                 regAddr, regAddr + 1, value, value);
    } else {
        LOG_ERROR("写入 uint32 寄存器失败[0x{:04X}~0x{:04X}]", regAddr, regAddr + 1);
    }
    return ret;
}

bool MotorRegisterAccessor::readReg32(int32_t motorID, uint32_t regAddr, uint32_t& outVal) {
    RegisterBlock regData;
    if (!m_protocol->read(motorID, RegisterType::INPUT_REGISTER, regAddr, regAddr + 1, regData)) {
        return false;
    }
    if (regData.data.size() < 2) {
        LOG_ERROR("读取 uint32 寄存器[0x{:04X}~0x{:04X}] 返回数据不足", regAddr, regAddr + 1);
        return false;
    }

    uint64_t val = combineRegistersTo64(regData.data);
    outVal = static_cast<uint32_t>(val);
    LOG_INFO("读取 uint32 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:08X})",
             regAddr, regAddr + 1, outVal, outVal);
    return true;
}

// ========== 64位寄存器操作 ==========
bool MotorRegisterAccessor::writeReg64(int32_t motorID, uint32_t regAddr, uint64_t value) {
    auto registers = splitToRegisters(value, 4);
    RegisterBlock regData;
    regData.data = registers;

    bool ret = m_protocol->write(motorID, RegisterType::HOLDING_REGISTER, regAddr, regData);
    if (ret) {
        LOG_INFO("写入 uint64 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:016X})",
                 regAddr, regAddr + 3, value, value);
    } else {
        LOG_ERROR("写入 uint64 寄存器失败[0x{:04X}~0x{:04X}]", regAddr, regAddr + 3);
    }
    return ret;
}

bool MotorRegisterAccessor::readReg64(int32_t motorID, uint32_t regAddr, uint64_t& outVal) {
    RegisterBlock regData;
    if (!m_protocol->read(motorID, RegisterType::INPUT_REGISTER, regAddr, regAddr + 3, regData)) {
        return false;
    }
    if (regData.data.size() < 4) {
        LOG_ERROR("读取 uint64 寄存器[0x{:04X}~0x{:04X}] 返回数据不足", regAddr, regAddr + 3);
        return false;
    }

    outVal = combineRegistersTo64(regData.data);
    LOG_INFO("读取 uint64 寄存器[0x{:04X}~0x{:04X}]: {} (0x{:016X})",
             regAddr, regAddr + 3, outVal, outVal);
    return true;
}

// ========== 批量寄存器操作 ==========
bool MotorRegisterAccessor::readRegBlock(int32_t motorID, uint32_t startAddr, uint16_t* buffer, size_t count) {
    RegisterBlock regData;
    if (!m_protocol->read(motorID, RegisterType::INPUT_REGISTER, startAddr, startAddr + count - 1, regData)) {
        return false;
    }
    if (regData.data.size() < count) {
        LOG_ERROR("批量读取寄存器[0x{:04X}~0x{:04X}] 返回数据不足",
                 startAddr, startAddr + count - 1);
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        buffer[i] = regData.data[i];
    }
    return true;
}

bool MotorRegisterAccessor::writeRegBlock(int32_t motorID, uint32_t startAddr, const uint16_t* values, size_t count) {
    RegisterBlock regData;
    regData.data.assign(values, values + count);
    return m_protocol->write(motorID, RegisterType::HOLDING_REGISTER, startAddr, regData);
}
