#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>

enum class Endianness {
  BigEndian,         // ABCD (标准 Modbus)
  BigEndianSwap,     // CDAB (高低字交换)
  LittleEndian,      // DCBA
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


  static std::vector<uint16_t> encodeInt32(int32_t value, Endianness endianness) {
    uint8_t A = (value >> 24) & 0xFF;
    uint8_t B = (value >> 16) & 0xFF;
    uint8_t C = (value >> 8) & 0xFF;
    uint8_t D = value & 0xFF;

    uint8_t bytes[4] {};

    switch (endianness)
    {
    case Endianness::BigEndian:
      // ABCD
      bytes[0] = A;
      bytes[1] = B;
      bytes[2] = C;
      bytes[3] = D;
      break;
    
    case Endianness::BigEndianSwap:
      // CDAB
      bytes[0] = C;
      bytes[1] = D;
      bytes[2] = A;
      bytes[3] = B;
      break;
    
    case Endianness::LittleEndian:
      // DCBA
      bytes[0] = D;
      bytes[1] = C;
      bytes[2] = B;
      bytes[3] = A;
      break;

    case Endianness::LittleEndianSwap:
      // BADC
      bytes[0] = B;
      bytes[1] = A;
      bytes[2] = D;
      bytes[3] = C;
      break;

    }

    uint16_t reg0 = (bytes[0] << 8) | bytes[1];
    uint16_t reg1 = (bytes[2] << 8) | bytes[3];

    return {reg0, reg1};
  }



  static int32_t decodeInt32(
    const std::vector<uint16_t>& registers, 
    Endianness endianness) 
  {
    // 1. 寄存器 -> 字节
    uint8_t b0 = (registers[0] >> 8) & 0xFF;
    uint8_t b1 = registers[0] & 0xFF;
    uint8_t b2 = (registers[1] >> 8) & 0xFF;
    uint8_t b3 = registers[1] & 0xFF;

    // 2. 根据 endian 恢复 ABCD
    uint8_t A {};
    uint8_t B {};
    uint8_t C {};
    uint8_t D {};

    switch (endianness)
    {
    case Endianness::BigEndian:
      // ABCD
      A = b0;
      B = b1;
      C = b2;
      D = b3;
      break;

    case Endianness::BigEndianSwap:
      // CDAB
      C = b0;
      D = b1;
      A = b2;
      B = b3;
      break;

    case Endianness::LittleEndian:
      // DCBA
      D = b0;
      C = b1;
      B = b2;
      A = b3;
      break;

    case Endianness::LittleEndianSwap:
      // BADC
      B = b0;
      A = b1;
      D = b2;
      C = b3;
      break;
    }

    // 3. 拼装 int32
    int32_t result =
      (static_cast<int32_t>(A) << 24) |
      (static_cast<int32_t>(B) << 16) |
      (static_cast<int32_t>(C) << 8)  |
      static_cast<int32_t>(D);

    return result;
  }
};