// ============================================================================
// EndianPolicy.h —— Step 2 codec: 端序策略
// ============================================================================
// 语义与旧 infrastructure/plc/protocol/EndianPolicy.h 完全一致：
//   - ByteOrder::BigEndian    表示"字内高字节在前"(AB)
//   - ByteOrder::LittleEndian 表示"字内低字节在前"(BA)
//   - WordOrder::HighWordFirst 表示"多寄存器中高位字在前"
//   - WordOrder::LowWordFirst  表示"多寄存器中低位字在前"
//
// 四种组合对应已知厂商布局（汇川 H5U 默认 = BigEndian + LowWordFirst，即 CDAB）：
//   ABCD = BigEndian   + HighWordFirst
//   CDAB = BigEndian   + LowWordFirst
//   DCBA = LittleEndian + LowWordFirst
//   BADC = LittleEndian + HighWordFirst
//
// 纯基础类型：不依赖 Modbus / Qt / Domain；不出现任何地址常量或业务名称。
// ============================================================================
#pragma once

namespace plc_vnext::codec {

enum class ByteOrder {
    /// 字内高字节在前 (AB)，如 0x1234 以字节流 0x12 0x34 表示
    BigEndian,
    /// 字内低字节在前 (BA)，如 0x1234 以字节流 0x34 0x12 表示
    LittleEndian
};

enum class WordOrder {
    /// 多寄存器中，高位字在前（低地址寄存器放高 16 位）
    HighWordFirst,
    /// 多寄存器中，低位字在前（低地址寄存器放低 16 位）
    LowWordFirst
};

struct EndianPolicy {
    ByteOrder byteOrder;
    WordOrder wordOrder;

    /// C++20 值语义相等比较（聚合类型需显式 default）
    friend constexpr bool operator==(const EndianPolicy& a, const EndianPolicy& b) = default;
};

}  // namespace plc_vnext::codec
