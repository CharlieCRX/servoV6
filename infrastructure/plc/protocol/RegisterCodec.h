#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

enum class Endianness {
    BigEndian,         // ABCD (标准 Modbus)
    LittleEndian,      // DCBA
    BigEndianSwap,     // CDAB (高低字交换)
    LittleEndianSwap   // BADC
};

class RegisterCodec {
public:
    static std::vector<uint16_t> encodeBool(bool value) {
        return { static_cast<uint16_t>(value ? 1 : 0) };
    }

    static bool decodeBool(const std::vector<uint16_t>& regs) {
        if (regs.empty()) throw std::invalid_argument("Empty registers");
        return regs[0] != 0;
    }

    static std::vector<uint16_t> encodeUint16(uint16_t value) {
        return { value };
    }

    static uint16_t decodeUint16(const std::vector<uint16_t>& regs) {
        if (regs.empty()) throw std::invalid_argument("Empty registers");
        return regs[0];
    }
};