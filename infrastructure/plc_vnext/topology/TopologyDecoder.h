// ============================================================================
// TopologyDecoder.h —— Step 6 topology: 原始寄存器块 -> TopologySnapshot
// ============================================================================
// 纯解码：把 D1400..D1577 的 178 个 holding 寄存器（低字在前 CDAB 字序）解码
// 为 TopologySnapshot。字段偏移/数量一律来自 layout::AxisTopologyLayout 的
// schema 常量，不写死 6/8 为跨版本事实。
//
// 长度不足（< topologyTotalWords()）返回 std::nullopt 并给出诊断，不抛异常。
// 本类不识别槽位/轴/业务，不校验语义（那属于 TopologyValidator）。
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace plc_vnext::topology {

class TopologyDecoder {
public:
    /// 解码整块拓扑寄存器（以 D1400 为基址、下标从 0 开始的 178 字）。
    /// 块过短返回 std::nullopt 并写入 diagnostic。
    [[nodiscard]] static std::optional<contracts::TopologySnapshot> decode(
        std::span<const uint16_t> regs, std::string& diagnostic);
};

}  // namespace plc_vnext::topology
