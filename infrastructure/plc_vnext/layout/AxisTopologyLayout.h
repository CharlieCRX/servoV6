// ============================================================================
// AxisTopologyLayout.h —— Step 3 layout: AxisTopology 头部/组/角色地址
// ============================================================================
// 与 tools/plc_read_validate.py 的拓扑常量及地址表 §5 完全一致：
//   头部基址 D1400；Group 基址 D1408、步长 83；组头占 3 D；
//   Role 步长 10、共 8 项；roleBase(g,r) = groupBase(g) + 3 + 10*r。
//   ConfigValid 为 D1576.bit0（BOOL，PLC 写）；ConfigErrorCode 为 D1577。
// 纯布局：无 Modbus 库 / Qt / Domain；不出现业务轴名。
// ============================================================================
#pragma once

#include <cstdint>

#include "infrastructure/plc_vnext/layout/RegisterAddress.h"

namespace plc_vnext::layout {

// ---- 头部字段（相对 D1400） ----
constexpr HoldingAddress topologyMagic()          { return HoldingAddress(1400); }  // D1400..D1401 DINT
constexpr HoldingAddress topologySchemaVersion()  { return HoldingAddress(1402); }  // D1402 INT
constexpr HoldingAddress topologyRevision()       { return HoldingAddress(1404); }  // D1404..D1405 DINT
constexpr HoldingAddress topologyConfigCRC()      { return HoldingAddress(1574); }  // D1574..D1575 DINT
constexpr HoldingAddress topologyConfigValid()    { return HoldingAddress(1576); }  // D1576 BOOL
constexpr HoldingAddress topologyConfigErrorCode(){ return HoldingAddress(1577); }  // D1577 INT

/// ConfigValid 是 D1576 的 bit0（PLC 写、只读）。
constexpr int topologyConfigValidBit() { return 0; }

// ---- 组 / 角色 ----
/// 组基址：groupBase(g) = D1408 + 83*g，g=0..1。
constexpr HoldingAddress groupBase(int g) { return HoldingAddress(1408 + 83 * g); }

/// 角色基址：roleBase(g,r) = groupBase(g) + 3 + 10*r，r=0..7。
constexpr HoldingAddress roleBase(int g, int r) {
    return HoldingAddress(groupBase(g).value() + 3 + 10 * r);
}

// ---- 拓扑数量与尺寸（schema 常量；来自验证脚本与地址表 §5）----
/// 组数量（当前 A/B 两组，g=0..1）。
constexpr int kTopologyGroupCount = 2;
/// 角色 ABI 容量（ST_GroupAxisMap.Role[8]）。
constexpr int kTopologyRoleCount = 8;
/// 组头占用字数（3 D）。
constexpr int kTopologyGroupHeaderWords = 3;
/// 每个 role 占用字数（10 D）。
constexpr int kTopologyRoleWords = 10;
/// 头部+两组总长：D1400..D1577 = 178 D。
constexpr int topologyTotalWords() { return 178; }

/// 当前支持（兼容）的最大 SchemaVersion（地址表 §5 已确认）。
constexpr int16_t kTopologySchemaVersion = 1;

/// 已确认的 AxisTopology Magic（2026 现场与 PLC 协议约定值 0x013527C6）。
/// 已从真实 PLC 对拍确认并冻结；TopologyValidator 据此做 MagicMismatch 拒绝。
constexpr int32_t kTopologyMagic = static_cast<int32_t>(0x013527C6);

}  // namespace plc_vnext::layout
