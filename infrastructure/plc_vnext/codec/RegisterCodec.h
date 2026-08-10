// ============================================================================
// RegisterCodec.h —— Step 2 codec: BOOL/INT16/DINT/REAL 编解码
// ============================================================================
// 无状态静态工具类。字序/字节序严格受 EndianPolicy 驱动，语义与旧
// infrastructure/plc/protocol/RegisterCodec.h 的 Level1/Level2 完全一致，
// 但错误返回统一用 std::optional<DecodeError>（nullopt = 成功），不再向上抛
// 未分类异常；且输入使用 std::span 避免无谓拷贝。
//
// 纯基础类型：无地址、无轴名、无 Modbus/Qt/Domain 依赖。
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "infrastructure/plc_vnext/codec/DecodeError.h"
#include "infrastructure/plc_vnext/codec/EndianPolicy.h"

namespace plc_vnext::codec {

class RegisterCodec {
public:
    // ---- Level 1：单字 / 单比特，不受端序影响 ----

    static std::vector<uint16_t> encodeBool(bool value);
    /// 非 0 即 true。不足 1 字返回 DecodeError（TooFewRegisters）。
    static std::optional<DecodeError> decodeBool(std::span<const uint16_t> regs,
                                                 bool& out);

    static std::vector<uint16_t> encodeInt16(int16_t value);
    static std::optional<DecodeError> decodeInt16(std::span<const uint16_t> regs,
                                                  int16_t& out);

    // ---- Level 2：多字拼装，端序驱动 ----

    /// 将 32 位有符号整数按策略拆解为两个寄存器字。
    static std::vector<uint16_t> encodeInt32(int32_t value, EndianPolicy policy);

    /// 将两个寄存器字按策略重组为 32 位有符号整数。不足 2 字返回 DecodeError。
    static std::optional<DecodeError> decodeInt32(
        std::span<const uint16_t> regs, EndianPolicy policy, int32_t& out);

    static std::vector<uint16_t> encodeFloat(float value, EndianPolicy policy);

    static std::optional<DecodeError> decodeFloat(
        std::span<const uint16_t> regs, EndianPolicy policy, float& out);
};

}  // namespace plc_vnext::codec
