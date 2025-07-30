#ifndef I_REGISTER_ACCESSOR_H
#define I_REGISTER_ACCESSOR_H

#include "RegisterBlock.h"
#include <cstdint>
class IRegisterAccessor {
public:
    virtual ~IRegisterAccessor() = default;

    // 读取寄存器块，mID为设备ID，regType为寄存器类型，startReg/stopReg为地址区间
    virtual bool read(int mID, int regType, int startReg, int stopReg, RegisterBlock& out) = 0;

    // 写寄存器，mID为设备ID，regType为寄存器类型，reg为起始寄存器地址，in为数据
    virtual bool write(int mID, int regType, int reg, const RegisterBlock& in) = 0;

    // 读取单个16位寄存器
    virtual bool readUInt16(int mID, int regType, int startReg, uint16_t& outVal) = 0;

    // 读取32位寄存器（两个连续16位寄存器）
    virtual bool readUInt32(int mID, int regType, int startReg, uint32_t& outVal) = 0;

    // 读取64位寄存器（四个连续16位寄存器）
    virtual bool readUInt64(int mID, int regType, int startReg, uint64_t& outVal) = 0;
};

#endif // I_REGISTER_ACCESSOR_H
