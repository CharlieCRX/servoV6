// ============================================================================
// RawRegisterBlock.h —— Step 2 codec: 原始寄存器缓冲
// ============================================================================
// 只保存原始 uint16_t（Holding 寄存器）与 uint8_t bit（Coil）缓冲，以及各自
// 的起始地址与访问边界；不做任何业务语义解释。后续 telemetry/topology 解码器
// 通过 getWords/getBit 按地址安全读取。
//
// 越界访问返回 std::nullopt，不抛异常。
// 纯基础类型：无地址公式、无轴名、无 Modbus/Qt/Domain 依赖。
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace plc_vnext::codec {

class RawRegisterBlock {
public:
    RawRegisterBlock() = default;

    /// wordStart: 这批 Holding 寄存器在 PLC 协议地址空间中的起始地址（0 基址）
    /// bitStart : 这批 Coil 位在 PLC 协议地址空间中的起始地址
    /// bitCount : 位缓冲覆盖的总位数
    ///
    /// 一致性校验：bitCount 必须 >= 0 且 <= bits.size() * 8（ceil(bitCount/8) 字节
    /// 必须能由 bits 缓冲容纳）。违例视为编程契约错误，抛 std::invalid_argument，
    /// 避免后续 getBit 越界访问 bits_。
    RawRegisterBlock(int wordStart,
                     std::vector<uint16_t> words,
                     int bitStart,
                     std::vector<uint8_t> bits,
                     int bitCount);

    /// 按 Holding 寄存器地址读取 [address, address+count) 的原始字。
    /// 越界/长度不足返回 nullopt。
    [[nodiscard]] std::optional<std::span<const uint16_t>>
    getWords(int address, int count) const;

    /// 按 Coil 位地址读取单个 bit。越界返回 nullopt。
    [[nodiscard]] std::optional<bool> getBit(int address) const;

    [[nodiscard]] int wordStart() const { return wordStart_; }
    [[nodiscard]] int wordCount() const { return static_cast<int>(words_.size()); }
    [[nodiscard]] int bitStart() const { return bitStart_; }
    [[nodiscard]] int bitCount() const { return bitCount_; }

private:
    int wordStart_ = 0;
    std::vector<uint16_t> words_;
    int bitStart_ = 0;
    std::vector<uint8_t> bits_;
    int bitCount_ = 0;
};

}  // namespace plc_vnext::codec
