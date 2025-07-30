#ifndef I_REGISTER_ACCESSOR_H
#define I_REGISTER_ACCESSOR_H

#include <cstdint>

class IRegisterAccessor {
public:
    virtual ~IRegisterAccessor() = default;

    // 单个16位读写，regType自由指定，方便访问不同寄存器类型
    virtual bool readUInt16(int mID, int regType, int reg, uint16_t& outVal) = 0;
    virtual bool writeUInt16(int mID, int regType, int reg, uint16_t val) = 0;

    // 32位读写（两个连续16位寄存器，小端序）
    virtual bool readUInt32(int mID, int regType, int reg, uint32_t& outVal) = 0;
    virtual bool writeUInt32(int mID, int regType, int reg, uint32_t val) = 0;

    // 64位读写（四个连续16位寄存器，小端序）
    virtual bool readUInt64(int mID, int regType, int reg, uint64_t& outVal) = 0;
    virtual bool writeUInt64(int mID, int regType, int reg, uint64_t val) = 0;
};

#endif // I_REGISTER_ACCESSOR_H
