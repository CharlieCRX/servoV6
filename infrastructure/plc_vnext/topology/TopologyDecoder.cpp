// ============================================================================
// TopologyDecoder.cpp —— Step 6 topology: 解码实现
// ============================================================================
// 与 tools/plc_read_validate.py _parse_axis_topology 及地址表 §5 完全一致：
//   - 头部 D1400 Magic/SchemaVersion/Revision，D1574 ConfigCRC，
//     D1576.bit0 ConfigValid，D1577 ConfigErrorCode
//   - group_base = D1408 + 83*g；组头 3 D；role_base = group_base + 3 + 10*r
// 端序采用项目 CDAB（BigEndian + LowWordFirst）。块过短返回 nullopt。
// ============================================================================
#include "infrastructure/plc_vnext/topology/TopologyDecoder.h"

#include <string>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"

namespace plc_vnext::topology {
namespace {

// 汇川 H5U 默认端序：CDAB（BigEndian + LowWordFirst）。
constexpr codec::EndianPolicy kCDAB{codec::ByteOrder::BigEndian,
                                    codec::WordOrder::LowWordFirst};

inline bool bitAt(uint16_t word, int bit) { return ((word >> bit) & 1u) != 0u; }

// 读取单个 INT 字；失败返回 false。
bool readInt16(const codec::RawRegisterBlock& blk, int addr, int16_t& out) {
    auto w = blk.getWords(addr, 1);
    if (!w) return false;
    return !codec::RegisterCodec::decodeInt16(*w, out).has_value();
}

// 读取低字在前的 DINT；失败返回 false。
bool readInt32(const codec::RawRegisterBlock& blk, int addr, int32_t& out) {
    auto w = blk.getWords(addr, 2);
    if (!w) return false;
    return !codec::RegisterCodec::decodeInt32(*w, kCDAB, out).has_value();
}

bool decodeHeader(const codec::RawRegisterBlock& blk, contracts::TopologyHeader& h) {
    const int base = layout::topologyMagic().value();
    if (!readInt32(blk, layout::topologyMagic().value(), h.magic)) return false;
    if (!readInt16(blk, layout::topologySchemaVersion().value(), h.schemaVersion)) return false;
    if (!readInt16(blk, base + 3, h.reserved)) return false;           // D1403
    if (!readInt32(blk, layout::topologyRevision().value(), h.revision)) return false;
    int32_t crc = 0;
    if (!readInt32(blk, layout::topologyConfigCRC().value(), crc)) return false;
    h.configCRC = static_cast<uint32_t>(crc);
    auto cv = blk.getWords(layout::topologyConfigValid().value(), 1);
    if (!cv) return false;
    h.configValid = bitAt((*cv)[0], layout::topologyConfigValidBit());
    if (!readInt16(blk, layout::topologyConfigErrorCode().value(), h.configErrorCode)) return false;
    return true;
}

bool decodeRole(const codec::RawRegisterBlock& blk, int g, int r,
                contracts::TopologyRole& role) {
    const int rb = layout::roleBase(g, r).value();
    auto w0 = blk.getWords(rb, 1);
    if (!w0) return false;
    role.valid = bitAt((*w0)[0], 0);
    role.hmiVisible = bitAt((*w0)[0], 1);
    if (!readInt16(blk, rb + 1, role.plcAxisIndex)) return false;
    if (!readInt16(blk, rb + 2, role.motorNo)) return false;
    if (!readInt32(blk, rb + 3, role.axisClass)) return false;
    if (!readInt32(blk, rb + 5, role.unitType)) return false;
    if (!readInt32(blk, rb + 7, role.motionMode)) return false;
    if (!readInt16(blk, rb + 9, role.reserved)) return false;
    return true;
}

bool decodeGroup(const codec::RawRegisterBlock& blk, int g, contracts::TopologyGroup& group) {
    const int gb = layout::groupBase(g).value();
    auto w0 = blk.getWords(gb, 1);
    if (!w0) return false;
    group.valid = bitAt((*w0)[0], 0);
    group.hmiVisible = bitAt((*w0)[0], 1);
    if (!readInt16(blk, gb + 1, group.groupCode)) return false;
    if (!readInt16(blk, gb + 2, group.reserved)) return false;
    group.roles.clear();
    group.roles.reserve(layout::kTopologyRoleCount);
    for (int r = 0; r < layout::kTopologyRoleCount; ++r) {
        contracts::TopologyRole role;
        if (!decodeRole(blk, g, r, role)) return false;
        group.roles.push_back(std::move(role));
    }
    return true;
}

}  // namespace

std::optional<contracts::TopologySnapshot> TopologyDecoder::decode(
    std::span<const uint16_t> regs, std::string& diagnostic) {
    if (regs.size() < static_cast<std::size_t>(layout::topologyTotalWords())) {
        diagnostic = "TopologyDecoder: block too short, expected " +
                     std::to_string(layout::topologyTotalWords()) + " registers, got " +
                     std::to_string(regs.size());
        return std::nullopt;
    }

    codec::RawRegisterBlock block(layout::topologyMagic().value(),
                                  std::vector<uint16_t>(regs.begin(), regs.end()),
                                  0, {}, 0);

    contracts::TopologySnapshot snap;
    if (!decodeHeader(block, snap.header)) {
        diagnostic = "TopologyDecoder: failed to decode header";
        return std::nullopt;
    }
    snap.groups.clear();
    snap.groups.reserve(layout::kTopologyGroupCount);
    for (int g = 0; g < layout::kTopologyGroupCount; ++g) {
        contracts::TopologyGroup group;
        if (!decodeGroup(block, g, group)) {
            diagnostic = "TopologyDecoder: failed to decode group " + std::to_string(g);
            return std::nullopt;
        }
        snap.groups.push_back(std::move(group));
    }
    return snap;
}

}  // namespace plc_vnext::topology
