// infrastructure/plc/protocol/RegisterCodec.h
#pragma once
#include <vector>
#include <cstdint>
#include <stdexcept>
#include <cstring>
#include "EndianPolicy.h"
#include "RegisterMetadata.h"
#include "ProtocolProfile.h"
#include "MemorySnapshot.h"
#include "PlcSnapshot.h"
#include "PlcValue.h"

namespace plc::protocol {

/**
 * @class RegisterCodec
 * @brief Modbus 寄存器编解码引擎 (Register Codec Engine)
 *
 * @details
 * 本类是一个完全无状态的静态工具类，负责在底层 Modbus 寄存器数据 (uint16_t 数组) 
 * 与上层业务强类型数据 (bool, int32_t, float, PlcValue) 之间进行安全、精确的双向转换。
 * * 设计上严格遵循分层架构：
 * - Level 1: 基础单字编解码（不受字节序影响）
 * - Level 2: 核心数学引擎（由端序策略 EndianPolicy 严格驱动）
 * - Level 3: 业务门面层（结合寄存器元数据与设备 Profile 实现端序的动态决议）
 */
class RegisterCodec {
public:
  /// @name Level 1: 基础单寄存器编解码 (Single Register Codec)
  /// @brief 处理单字或单比特的简单映射，不受大小端策略影响。
  /// @{

  /// @brief 编码布尔值为寄存器字 (0x0001 或 0x0000)
  static std::vector<uint16_t> encodeBool(bool value) {
    return { static_cast<uint16_t>(value ? 1 : 0) };
  }

  /// @brief 解码寄存器字为布尔值 (非 0 即为 true)
  /// @throws std::invalid_argument 如果输入寄存器列表为空
  static bool decodeBool(const std::vector<uint16_t>& regs) {
    if (regs.empty()) throw std::invalid_argument("Empty registers provided to decodeBool");
    return regs[0] != 0;
  }

  /// @brief 编码 16位无符号整数为寄存器字
  static std::vector<uint16_t> encodeUint16(uint16_t value) {
    return { value };
  }

  /// @brief 解码寄存器字为 16位无符号整数
  /// @throws std::invalid_argument 如果输入寄存器列表为空
  static uint16_t decodeUint16(const std::vector<uint16_t>& regs) {
    if (regs.empty()) throw std::invalid_argument("Empty registers provided to decodeUint16");
    return regs[0];
  }

  /// @}


  /// @name Level 2: 多字数学拼装引擎 (Endian-Driven Math Engine)
  /// @brief 纯粹由 EndianPolicy 驱动的核心编解码算法，供基础层或 TDD 测试直接调用。
  /// @{

  /// @brief 将 32位有符号整数按指定的端序策略拆解为两个 16位寄存器
  /// @param value  待编码的 32位整型实值
  /// @param policy 字节序与字序组合策略
  /// @return 包含两个 uint16_t 元素的向量
  static std::vector<uint16_t> encodeInt32(int32_t value, EndianPolicy policy) {
    // 标准提取 4 个字节 (MSB -> LSB)
    uint8_t A = (value >> 24) & 0xFF;
    uint8_t B = (value >> 16) & 0xFF;
    uint8_t C = (value >> 8)  & 0xFF;
    uint8_t D = value & 0xFF;

    uint16_t highWord = 0;
    uint16_t lowWord = 0;

    // 1. 应用 ByteOrder (字内重组)
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

  /// @brief 根据指定的端序策略，将两个 16位寄存器重组为 32位有符号整数
  /// @throws std::invalid_argument 如果传入寄存器数量不足 2 个
  static int32_t decodeInt32(const std::vector<uint16_t>& registers, EndianPolicy policy) {
    if (registers.size() < 2) throw std::invalid_argument("Requires at least 2 registers for Int32 decoding");

    uint16_t highWord = 0;
    uint16_t lowWord = 0;

    // 1. 应用 WordOrder 定位高低字
    if (policy.wordOrder == WordOrder::HighWordFirst) {
      highWord = registers[0];
      lowWord  = registers[1];
    } else {
      highWord = registers[1];
      lowWord  = registers[0];
    }

    uint8_t A{}, B{}, C{}, D{};

    // 2. 应用 ByteOrder 提取原始字节流
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

  /// @brief 将 IEEE-754 单精度浮点数按指定端序编码为寄存器数组
  static std::vector<uint16_t> encodeFloat(float value, EndianPolicy policy) {
    uint32_t raw{};
    std::memcpy(&raw, &value, sizeof(float));
    return encodeInt32(static_cast<int32_t>(raw), policy);
  }

  /// @brief 按指定端序从寄存器数组解码 IEEE-754 单精度浮点数
  static float decodeFloat(const std::vector<uint16_t>& registers, EndianPolicy policy) {
    uint32_t raw = static_cast<uint32_t>(decodeInt32(registers, policy));
    float value{};
    std::memcpy(&value, &raw, sizeof(float));
    return value;
  }

  /// @}


  /// @name Level 3: 业务门面代理层 (Business Facade Layer)
  /// @brief 结合元数据与设备配置进行自动策略决议，为 Driver 层提供高级 API 门面。
  /// @{
  
  /// @brief 端序策略决议器 (Policy Resolver)
  /// @details 优先使用寄存器元数据自身配置的特殊端序 (override)，否则降级使用设备 Profile 的全局默认端序。
  static EndianPolicy resolvePolicy(const RegisterInfo& reg, const ProtocolProfile& profile) {
    return reg.endianOverride.value_or(profile.defaultEndian);
  }

  // --- 高级 API 门面：Int32 ---

  static std::vector<uint16_t> encode(int32_t value, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return encodeInt32(value, resolvePolicy(reg, profile));
  }

  static int32_t decodeInt32(const std::vector<uint16_t>& regs, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return decodeInt32(regs, resolvePolicy(reg, profile));
  }

  // --- 高级 API 门面：Float32 ---

  static std::vector<uint16_t> encode(float value, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return encodeFloat(value, resolvePolicy(reg, profile));
  }

  static float decodeFloat(const std::vector<uint16_t>& regs, const RegisterInfo& reg, const ProtocolProfile& profile) {
    return decodeFloat(regs, resolvePolicy(reg, profile));
  }

  /// @}


  /// @name Level 3+: 运行时快照管线 (Snapshot & PlcValue Pipeline)
  /// @brief 提供标准 PlcValue 的出入口，串联内存快照机制，作为整个通信收发管线的核心转换站。
  /// @{

  /**
   * @brief 从内存快照中提取并解码出标准化 PlcValue 
   * @details 供 Driver 层的接收线程或 FeedbackDecoder 调用，将物理层快照翻译为业务语义。
   *
   * @param reg     寄存器元数据（决定目标地址、数据类型以及端序 override）
   * @param bits    Coil/离散量内存快照指针（仅 area==Coil 时有效，否则应传 nullptr）
   * @param words   保持/输入寄存器快照指针（仅 area==HoldingReg 时有效，否则应传 nullptr）
   * @param profile 目标 PLC 协议全局 Profile
   * @return PlcValue 标准化多态值
   * @throws std::invalid_argument 缺失对应区域的 snapshot 或请求了不支持的寄存器类型
   * @throws std::out_of_range     请求解析的寄存器地址超出了当前快照的边界
   */
  static PlcValue decode(const RegisterInfo& reg,
                          const RawBitSnapshot* bits,
                          const RawWordSnapshot* words,
                          const ProtocolProfile& profile) {
    // 1. 快照合规性校验
    if ((reg.area == RegisterArea::Coil && !bits) ||
        (reg.area == RegisterArea::HoldingReg && !words)) {
      throw std::invalid_argument("RegisterCodec::decode: Missing required snapshot for the specified register area.");
    }

    // 2. 解析 EndianPolicy
    EndianPolicy policy = resolvePolicy(reg, profile);

    // 3. 根据目标类型分发解码逻辑
    switch (reg.type) {
      case RegisterType::Bool: {
        if (!bits) throw std::invalid_argument("Coil bits snapshot required for Bool type decoding");
        auto bit = bits->getBit(reg.address);
        if (!bit.has_value()) throw std::out_of_range("Coil address out of snapshot boundary");
        return PlcValue{bit.value()};
      }

      case RegisterType::Int16: {
        if (!words) throw std::invalid_argument("Word snapshot required for Int16 type decoding");
        auto span = words->getWords(reg.address, 1);
        if (!span.has_value()) throw std::out_of_range("HoldingReg address out of snapshot boundary");
        return PlcValue{static_cast<int16_t>((*span)[0])};
      }

      case RegisterType::Float32: {
        if (!words) throw std::invalid_argument("Word snapshot required for Float32 type decoding");
        auto span = words->getWords(reg.address, 2);
        if (!span.has_value()) throw std::out_of_range("HoldingReg address out of snapshot boundary");
        
        // span 适配 L2 接口 (将 std::span<const uint16_t> 转为 std::vector)
        std::vector<uint16_t> tmp((*span).begin(), (*span).end());
        float value = decodeFloat(tmp, policy);
        return PlcValue{value};
      }

      default:
        throw std::invalid_argument("RegisterCodec::decode: Unsupported RegisterType encountered");
    }
  }

  /**
   * @brief 从 PlcSnapshot 便捷解码（v4 便捷重载）
   * @details 直接从一个完整的 PLC 现场照片中解码出业务值，
   *          省去手动拆解 bits/words 两个指针的麻烦。
   *
   * @param reg      寄存器元数据
   * @param snapshot 一次完整采集的 PLC 状态照片
   * @param profile  目标 PLC 协议全局 Profile
   * @return PlcValue 标准化多态值
   */
  static PlcValue decode(const RegisterInfo& reg,
                          const PlcSnapshot& snapshot,
                          const ProtocolProfile& profile) {
    return decode(reg, &snapshot.bits, &snapshot.words, profile);
  }

  /**
   * @brief 将业务层的 PlcValue 统一向下编码为 Modbus 物理寄存器字向量
   * @details 供 Driver 层的命令发送管线调用，负责装包前的序列化。
   * * @param value   上层下发的标准化 PlcValue 多态值
   * @param reg     目标寄存器元数据
   * @param profile 目标 PLC 协议全局 Profile
   * @return std::vector<uint16_t> 组装完毕、可直接发送至网络的载荷
   * @throws std::invalid_argument 传入的 PlcValue 类型当前版本不支持编码
   */
  static std::vector<uint16_t> encode(const PlcValue& value,
                                      const RegisterInfo& reg,
                                      const ProtocolProfile& profile) {
    EndianPolicy policy = resolvePolicy(reg, profile);

    // 分支 1: 处理 Bool 类型 (区分物理 Coil 与 HoldingReg 的特殊映射)
    if (isBool(value)) {
      bool b = getValue<bool>(value);
      // 若操作对象是真实线圈，且该 PLC 要求写线圈时使用 0xFF00/0x0000 语义（如 FC05 规范）
      if (reg.area == RegisterArea::Coil && profile.coilUsesFF00) {
        return { static_cast<uint16_t>(b ? 0xFF00 : 0x0000) };
      }
      return encodeBool(b);
    }

    // 分支 2: 处理 Int16 类型
    if (isInt16(value)) {
      return encodeUint16(static_cast<uint16_t>(getValue<int16_t>(value)));
    }

    // 分支 3: 处理 Float32 类型
    if (isFloat(value)) {
      return encodeFloat(getValue<float>(value), policy);
    }

    // 分支 4: 兜底拒接异常
    throw std::invalid_argument("RegisterCodec::encode: Unsupported PlcValue type for encoding pipeline");
  }

  /// @}
};

} // namespace plc::protocol
