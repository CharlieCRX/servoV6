// ============================================================================
// TopologyFixture.h —— Step 6 test support: 拓扑寄存器/快照测试夹具
// ============================================================================
// 供 topology 的 decoder/validator/reader 三个测试共用。所有偏移一律来自
// layout::AxisTopologyLayout 的 schema 常量（不写死 6/8 为跨版本事实），
// 字序采用 CDAB 低字在前（与 tools/plc_read_validate.py 一致）。
//
// 本文件只放测试数据，不进入生产库（tests/ 目录）。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"

namespace plc_vnext::test {

/// 已冻结的 Magic（与真实 PLC 对拍确认值 0x013527C6，见 layout::kTopologyMagic）。
constexpr int32_t kFixtureMagic = static_cast<int32_t>(0x013527C6);

inline void writeInt16(std::vector<uint16_t>& r, int idx, int16_t v) {
    r[idx] = static_cast<uint16_t>(v);
}

inline void writeDint(std::vector<uint16_t>& r, int idx, int32_t v) {
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

inline int roleOffset(int g, int r) {
    return layout::roleBase(g, r).value() - layout::topologyMagic().value();
}

inline int groupOffset(int g) {
    return layout::groupBase(g).value() - layout::topologyMagic().value();
}

/// 设置一个角色的字段（含 hmiVisible 镜像为 valid）。
inline void setRole(std::vector<uint16_t>& r, int g, int roleIdx, bool valid,
                    int16_t plcAxisIndex, int16_t motorNo, int32_t axisClass,
                    int32_t unitType, int32_t motionMode) {
    const int off = roleOffset(g, roleIdx);
    uint16_t w = r[off];
    setBit(w, 0, valid);
    setBit(w, 1, valid);
    r[off] = w;
    writeInt16(r, off + 1, plcAxisIndex);
    writeInt16(r, off + 2, motorNo);
    writeDint(r, off + 3, axisClass);
    writeDint(r, off + 5, unitType);
    writeDint(r, off + 7, motionMode);
}

inline void setGroupValid(std::vector<uint16_t>& r, int g, bool valid,
                          bool hmiVisible, int16_t groupCode) {
    const int off = groupOffset(g);
    uint16_t w = r[off];
    setBit(w, 0, valid);
    setBit(w, 1, hmiVisible);
    r[off] = w;
    writeInt16(r, off + 1, groupCode);
}

inline void setConfigValid(std::vector<uint16_t>& r, bool valid) {
    const int off = layout::topologyConfigValid().value() - layout::topologyMagic().value();
    uint16_t w = r[off];
    setBit(w, layout::topologyConfigValidBit(), valid);
    r[off] = w;
}

inline void setConfigErrorCode(std::vector<uint16_t>& r, int16_t code) {
    writeInt16(r, layout::topologyConfigErrorCode().value() - layout::topologyMagic().value(),
               code);
}

inline void setRoleReserved(std::vector<uint16_t>& r, int g, int roleIdx, int16_t v) {
    writeInt16(r, roleOffset(g, roleIdx) + 9, v);
}

inline void setRoleMotionMode(std::vector<uint16_t>& r, int g, int roleIdx, int32_t v) {
    writeDint(r, roleOffset(g, roleIdx) + 7, v);
}

/// 构造 178 字"干净合法基线"夹具（代码构造，非真实现场转储）：
/// Magic=0x013527C6, Schema=1, ConfigValid=true, A 组 Role[0] X1 / Role[1] X2 /
/// Role[5] SYN0 有效；其余角色与 B 组为无效角色（PlcAxisIndex=-1），无脏数据。
/// 用于构造 Validator 必须通过的合法 baseline，**不代表**真实 PLC 快照。
inline std::vector<uint16_t> makeValidTopologyRegisters(int32_t revision = 0) {
    std::vector<uint16_t> r(static_cast<std::size_t>(layout::topologyTotalWords()), 0);

    writeDint(r, 0, kFixtureMagic);                                     // D1400..1401
    writeInt16(r, layout::topologySchemaVersion().value() - layout::topologyMagic().value(), 1);  // D1402
    writeDint(r, layout::topologyRevision().value() - layout::topologyMagic().value(), revision);  // D1404..1405
    setConfigValid(r, true);
    setConfigErrorCode(r, 0);

    // A 组：valid, hmiVisible=true, GroupCode=0
    setGroupValid(r, 0, true, true, 0);
    setRole(r, 0, 0, true, 0, 1, 0, 0, 1);      // Role[0] X1（龙门X1）
    setRole(r, 0, 1, true, 1, 2, 0, 0, 2);      // Role[1] X2（龙门X2）
    setRole(r, 0, 2, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 3, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 4, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 5, true, 13, 0, 2, 0, 5);     // Role[5] SYN0（龙门逻辑轴，虚轴）
    setRole(r, 0, 6, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 7, false, -1, 0, 0, 0, 0);

    // B 组：禁用（invalid, hmiVisible=false, GroupCode=1），全部角色 PlcAxisIndex=-1
    setGroupValid(r, 1, false, false, 1);
    for (int i = 0; i < layout::kTopologyRoleCount; ++i) {
        setRole(r, 1, i, false, -1, 0, 0, 0, 0);
    }
    return r;
}

/// 版本化真实现场转储：按一次 `python plc_read_validate.py --only topology`
/// 的 D1400..D1577 输出重建的 178 字（CDAB 低字在前），**含现场未初始化残留**：
///   - A 组 Role[3] UnitType=851971(0x0D0003)、MotionMode=131072(0x020000)
///   - B 组 Role[0] MotionMode=16800(0x41A0)、Reserved=16800
/// 其余无效角色 PlcAxisIndex=-1。此转储应能 decode 且经 Validator 校验 0 issue，
/// 是 M2"真实 PLC topology 对拍一致"的证据 fixture（见 test_plc_topology_real_dump.cpp）。
inline std::vector<uint16_t> makeVersionedPlcTopologyDump(int32_t revision = 0) {
    std::vector<uint16_t> r(static_cast<std::size_t>(layout::topologyTotalWords()), 0);

    writeDint(r, 0, kFixtureMagic);                                     // D1400..1401
    writeInt16(r, layout::topologySchemaVersion().value() - layout::topologyMagic().value(), 1);
    writeDint(r, layout::topologyRevision().value() - layout::topologyMagic().value(), revision);
    setConfigValid(r, true);
    setConfigErrorCode(r, 0);

    // Group[0]（A 组）valid=true, hmiVisible=true, GroupCode=0
    setGroupValid(r, 0, true, true, 0);
    setRole(r, 0, 0, true, 0, 1, 0, 0, 1);          // X1
    setRole(r, 0, 1, true, 1, 2, 0, 0, 2);          // X2
    setRole(r, 0, 2, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 3, false, -1, 0, 0, 851971, 131072);  // 现场脏残留 UnitType/MotionMode
    setRole(r, 0, 4, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 5, true, 13, 0, 2, 0, 5);         // SYN0（龙门逻辑轴，虚轴）
    setRole(r, 0, 6, false, -1, 0, 0, 0, 0);
    setRole(r, 0, 7, false, -1, 0, 0, 0, 0);

    // Group[1]（B 组）valid=false, hmiVisible=false, GroupCode=1
    setGroupValid(r, 1, false, false, 1);
    setRole(r, 1, 0, false, -1, 0, 0, 0, 16800);   // 现场脏残留 MotionMode
    setRoleReserved(r, 1, 0, 16800);                  // 现场脏残留 Reserved
    for (int i = 1; i < layout::kTopologyRoleCount; ++i) {
        setRole(r, 1, i, false, -1, 0, 0, 0, 0);
    }
    return r;
}

}  // namespace plc_vnext::test
