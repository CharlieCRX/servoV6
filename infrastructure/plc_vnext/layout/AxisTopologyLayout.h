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

}  // namespace plc_vnext::layout
