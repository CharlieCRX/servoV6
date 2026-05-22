#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>

enum class ByteOrder {
  BigEndian,    // 字内高字节在前 (AB)
  LittleEndian  // 字内低字节在前 (BA)
};

enum class WordOrder {
  HighWordFirst, // 多寄存器中，高位字在前
  LowWordFirst   // 多寄存器中，低位字在前
};

struct EndianPolicy {
  ByteOrder byteOrder;
  WordOrder wordOrder;
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

  static std::vector<uint16_t> encodeInt32(int32_t value, EndianPolicy policy) {
    // 标准提取 4 个字节 (从高位到低位: MSB -> LSB)
    uint8_t A = (value >> 24) & 0xFF;
    uint8_t B = (value >> 16) & 0xFF;
    uint8_t C = (value >> 8) & 0xFF;
    uint8_t D = value & 0xFF;

    uint16_t highWord = 0;
    uint16_t lowWord = 0;

    // 1. 应用 ByteOrder (解决单寄存器内的字节顺序)
    if (policy.byteOrder == ByteOrder::BigEndian) {
      highWord = (static_cast<uint16_t>(A) << 8) | B;
      lowWord  = (static_cast<uint16_t>(C) << 8) | D;
    } else { // LittleEndian
      highWord = (static_cast<uint16_t>(B) << 8) | A;
      lowWord  = (static_cast<uint16_t>(D) << 8) | C;
    }

    // 2. 应用 WordOrder (解决多个寄存器之间的顺序)
    if (policy.wordOrder == WordOrder::HighWordFirst) {
      return { highWord, lowWord };
    } else { // LowWordFirst
      return { lowWord, highWord };
    }
  }

  static int32_t decodeInt32(const std::vector<uint16_t>& registers, EndianPolicy policy) {
    if (registers.size() < 2) throw std::invalid_argument("Requires at least 2 registers for Int32");

    uint16_t highWord = 0;
    uint16_t lowWord = 0;

    // 1. 根据 WordOrder 定位高位字与低位字
    if (policy.wordOrder == WordOrder::HighWordFirst) {
      highWord = registers[0];
      lowWord  = registers[1];
    } else {
      highWord = registers[1];
      lowWord  = registers[0];
    }

    uint8_t A{}, B{}, C{}, D{};

    // 2. 根据 ByteOrder 还原具体字节
    if (policy.byteOrder == ByteOrder::BigEndian) {
      A = (highWord >> 8) & 0xFF;
      B = highWord & 0xFF;
      C = (lowWord >> 8) & 0xFF;
      D = lowWord & 0xFF;
    } else { // LittleEndian
      B = (highWord >> 8) & 0xFF;
      A = highWord & 0xFF;
      D = (lowWord >> 8) & 0xFF;
      C = lowWord & 0xFF;
    }

    // 3. 拼装标准的 Int32 (MSB到LSB拼接)
    return (static_cast<int32_t>(A) << 24) |
         (static_cast<int32_t>(B) << 16) |
         (static_cast<int32_t>(C) << 8)  |
         static_cast<int32_t>(D);
  }

  static std::vector<uint16_t> encodeFloat(float value, EndianPolicy policy) {
    uint32_t raw{};
    std::memcpy(&raw, &value, sizeof(float));
    return encodeInt32(static_cast<int32_t>(raw), policy);
  }

  static float decodeFloat(const std::vector<uint16_t>& registers, EndianPolicy policy) {
    uint32_t raw = static_cast<uint32_t>(decodeInt32(registers, policy));
    float value{};
    std::memcpy(&value, &raw, sizeof(float));
    return value;
  }
};