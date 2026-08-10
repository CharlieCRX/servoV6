// ============================================================================
// RegisterCodec.cpp —— Step 2 codec: BOOL/INT16/DINT/REAL 编解码实现
// ============================================================================
// 语义对齐旧 infrastructure/plc/protocol/RegisterCodec.h 的 Level1/Level2 静态
// 算法，以及 tools/plc_read_validate.py --selftest 的字序断言：
//   1.0f = {0x0000, 0x3F80}（低字在前）  25.8f = {0x6666, 0x41CE}
//   DINT 0x12345678 ⇄ {0x5678, 0x1234}    -65536 ⇄ {0x0000, 0xFFFF}
// 错误统一返回 std::optional<DecodeError>，不抛未分类异常。
// ============================================================================
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"

#include <cstring>

namespace plc_vnext::codec {

// ---------------------------------------------------------------------------
// Level 1：单字 / 单比特（不受端序影响）
// ---------------------------------------------------------------------------
std::vector<uint16_t> RegisterCodec::encodeBool(bool value) {
    return {static_cast<uint16_t>(value ? 1 : 0)};
}

std::optional<DecodeError> RegisterCodec::decodeBool(
    std::span<const uint16_t> regs, bool& out) {
    if (regs.empty()) {
        return DecodeError{DecodeErrorKind::TooFewRegisters, 0,
                           "decodeBool: requires at least 1 register"};
    }
    out = regs[0] != 0;
    return std::nullopt;
}

std::vector<uint16_t> RegisterCodec::encodeInt16(int16_t value) {
    return {static_cast<uint16_t>(value)};
}

std::optional<DecodeError> RegisterCodec::decodeInt16(
    std::span<const uint16_t> regs, int16_t& out) {
    if (regs.empty()) {
        return DecodeError{DecodeErrorKind::TooFewRegisters, 0,
                           "decodeInt16: requires at least 1 register"};
    }
    out = static_cast<int16_t>(regs[0]);
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Level 2：多字拼装（严格端序驱动）
// ---------------------------------------------------------------------------
std::vector<uint16_t> RegisterCodec::encodeInt32(int32_t value,
                                                 EndianPolicy policy) {
    const uint32_t u = static_cast<uint32_t>(value);
    const uint8_t A = static_cast<uint8_t>((u >> 24) & 0xFFu);
    const uint8_t B = static_cast<uint8_t>((u >> 16) & 0xFFu);
    const uint8_t C = static_cast<uint8_t>((u >> 8) & 0xFFu);
    const uint8_t D = static_cast<uint8_t>(u & 0xFFu);

    uint16_t highWord = 0;
    uint16_t lowWord = 0;
    if (policy.byteOrder == ByteOrder::BigEndian) {
        highWord = static_cast<uint16_t>((static_cast<uint16_t>(A) << 8) | B);
        lowWord = static_cast<uint16_t>((static_cast<uint16_t>(C) << 8) | D);
    } else {  // LittleEndian：字内低字节在前
        highWord = static_cast<uint16_t>((static_cast<uint16_t>(B) << 8) | A);
        lowWord = static_cast<uint16_t>((static_cast<uint16_t>(D) << 8) | C);
    }

    if (policy.wordOrder == WordOrder::HighWordFirst) {
        return {highWord, lowWord};
    }
    return {lowWord, highWord};  // LowWordFirst：低地址放低位字
}

std::optional<DecodeError> RegisterCodec::decodeInt32(
    std::span<const uint16_t> regs, EndianPolicy policy, int32_t& out) {
    if (regs.size() < 2) {
        return DecodeError{DecodeErrorKind::TooFewRegisters,
                           static_cast<int>(regs.size()),
                           "decodeInt32: requires at least 2 registers"};
    }

    uint16_t highWord = 0;
    uint16_t lowWord = 0;
    if (policy.wordOrder == WordOrder::HighWordFirst) {
        highWord = regs[0];
        lowWord = regs[1];
    } else {  // LowWordFirst：低地址寄存器是低位字
        highWord = regs[1];
        lowWord = regs[0];
    }

    uint8_t A = 0, B = 0, C = 0, D = 0;
    if (policy.byteOrder == ByteOrder::BigEndian) {
        A = static_cast<uint8_t>((highWord >> 8) & 0xFFu);
        B = static_cast<uint8_t>(highWord & 0xFFu);
        C = static_cast<uint8_t>((lowWord >> 8) & 0xFFu);
        D = static_cast<uint8_t>(lowWord & 0xFFu);
    } else {  // LittleEndian
        B = static_cast<uint8_t>((highWord >> 8) & 0xFFu);
        A = static_cast<uint8_t>(highWord & 0xFFu);
        D = static_cast<uint8_t>((lowWord >> 8) & 0xFFu);
        C = static_cast<uint8_t>(lowWord & 0xFFu);
    }

    out = (static_cast<int32_t>(A) << 24) | (static_cast<int32_t>(B) << 16) |
          (static_cast<int32_t>(C) << 8) | static_cast<int32_t>(D);
    return std::nullopt;
}

std::vector<uint16_t> RegisterCodec::encodeFloat(float value,
                                                 EndianPolicy policy) {
    uint32_t raw = 0;
    std::memcpy(&raw, &value, sizeof(float));
    return encodeInt32(static_cast<int32_t>(raw), policy);
}

std::optional<DecodeError> RegisterCodec::decodeFloat(
    std::span<const uint16_t> regs, EndianPolicy policy, float& out) {
    int32_t raw = 0;
    if (auto err = decodeInt32(regs, policy, raw)) {
        return err;
    }
    const uint32_t rawU = static_cast<uint32_t>(raw);
    std::memcpy(&out, &rawU, sizeof(float));
    return std::nullopt;
}

}  // namespace plc_vnext::codec
