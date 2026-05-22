// infrastructure/plc/protocol/RegisterCodec.h
#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include "EndianPolicy.h"
#include "RegisterMetadata.h"
#include "ProtocolProfile.h"

namespace plc::protocol {

class RegisterCodec {
public:
  // ========================================================================
  // Level 1: 基础单寄存器编解码 (不受大小端影响)
  // ========================================================================
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

  // ========================================================================
  // Level 2: 核心数学拼装引擎 (纯粹的 EndianPolicy 驱动，供基础 TDD 测试调用)
  // ========================================================================
  static std::vector<uint16_t> encodeInt32(int32_t value, EndianPolicy policy) {
    // 标准提取 4 个字节 (MSB -> LSB)
    uint8_t A = (value >> 24) & 0xFF;
    uint8_t B = (value >> 16) & 0xFF;
    uint8_t C = (value >> 8)  & 0xFF;
    uint8_t D = value & 0xFF;

    uint16_t highWord = 0;
    uint16_t lowWord = 0;

    // 1. 应用 ByteOrder (字内处理)
    if (policy.byteOrder == ByteOrder::BigEndian) {
      highWord = (static_cast<uint16_t>(A) << 8) | B;
      lowWord  = (static_cast<uint16_t>(C) << 8) | D;
    } else { // LittleEndian
      highWord = (static_cast<uint16_t>(B) << 8) | A;
      lowWord  = (static_cast<uint16_t>(D) << 8) | C;
    }

    // 2. 应用 WordOrder (字间拼装)
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

    // 1. 应用 WordOrder 找到高低字
    if (policy.wordOrder == WordOrder::HighWordFirst) {
      highWord = registers[0];
      lowWord  = registers[1];
    } else {
      highWord = registers[1];
      lowWord  = registers[0];
    }

    uint8_t A{}, B{}, C{}, D{};

    // 2. 应用 ByteOrder 提取原始字节
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

    // 3. 组装标准 Int32
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

  // ========================================================================
  // Level 3: 业务代理层 (元数据 + Profile 驱动，你的 Driver 主要调用这里)
  // ========================================================================
  
  // 策略决议逻辑
  static EndianPolicy resolvePolicy(const RegisterInfo& reg, const ProtocolProfile& profile) {
    // 如果寄存器自身有 override 则用自身的，否则降级使用当前 PLC profile 默认配置
    return reg.endianOverride.value_or(profile.defaultEndian);
  }

  // 高级 API 门面：Int32
  static std::vector<uint16_t> encode(int32_t value, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return encodeInt32(value, resolvePolicy(reg, profile));
  }

  static int32_t decodeInt32(const std::vector<uint16_t>& regs, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return decodeInt32(regs, resolvePolicy(reg, profile));
  }

  // 高级 API 门面：Float32
  static std::vector<uint16_t> encode(float value, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return encodeFloat(value, resolvePolicy(reg, profile));
  }

  static float decodeFloat(const std::vector<uint16_t>& regs, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return decodeFloat(regs, resolvePolicy(reg, profile));
  }
};

} // namespace plc::protocol