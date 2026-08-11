// ============================================================================
// TelemetryFixture.h —— Step 7 test support: telemetry 寄存器夹具
// ============================================================================
// 供 telemetry 的 axis/gantry/reader 三个测试共用。字段偏移一律来自
// layout::AxisSlotRegisterLayout 与 layout::GantryLayout（与
// tools/plc_read_validate.py 一致），字序采用 CDAB 低字在前。
//   - 轴区块：覆盖 D0..D175，向量下标 == D 地址（基址 0）。
//   - 龙门状态块：覆盖 D190..D225，向量下标 0 == D190（GantryStatus 基址）。
// 本文件只放测试数据，不进入生产库（tests/ 目录）。
// ============================================================================
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

namespace plc_vnext::test {

inline void writeWord(std::vector<uint16_t>& r, int idx, uint16_t v) {
    r[idx] = v;
}

inline void writeInt16(std::vector<uint16_t>& r, int idx, int16_t v) {
    r[idx] = static_cast<uint16_t>(v);
}

/// 写入低字在前（CDAB）的 REAL。
inline void writeFloat(std::vector<uint16_t>& r, int idx, float v) {
    uint32_t raw = 0;
    std::memcpy(&raw, &v, sizeof(float));
    r[idx] = static_cast<uint16_t>(raw & 0xFFFFu);
    r[idx + 1] = static_cast<uint16_t>((raw >> 16) & 0xFFFFu);
}

/// 写入低字在前（CDAB）的 DINT。
inline void writeDintLowWordFirst(std::vector<uint16_t>& r, int idx, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    r[idx] = static_cast<uint16_t>(u & 0xFFFFu);
    r[idx + 1] = static_cast<uint16_t>((u >> 16) & 0xFFFFu);
}

inline void setBit(uint16_t& w, int bit, bool on) {
    if (on) {
        w = static_cast<uint16_t>(w | (1u << bit));
    } else {
        w = static_cast<uint16_t>(w & static_cast<uint16_t>(~(1u << bit)));
    }
}

/// 标准 16 槽位轴区块（D0..D175，全 0 基线）。
inline std::vector<uint16_t> makeAxisBlock() {
    return std::vector<uint16_t>(176, 0);
}

/// 龙门状态区块（D190..D225，全 0 基线；下标 0 == D190）。
inline std::vector<uint16_t> makeGantryBlock() {
    return std::vector<uint16_t>(36, 0);
}

}  // namespace plc_vnext::test
