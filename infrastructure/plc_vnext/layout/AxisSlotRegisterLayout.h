// ============================================================================
// AxisSlotRegisterLayout.h —— Step 3 layout: 槽位 0..15 单轴地址公式
// ============================================================================
// 地址公式与《PLC变量协议_Modbus最终地址表.md》§3.1 及
// tools/plc_read_validate.py STANDARD_AXIS_BLOCKS 完全一致（0 基址）：
//   REAL 每项占 2 D；INT/WORD 每项占 1 D。
// 槽位越界（非 0..15）由 contracts::PlcAxisSlot 保证；公式本身为 constexpr。
// 纯布局：无 Modbus 库 / Qt / Domain；不出现业务轴名（x_axis/y_axis/...）。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/layout/RegisterAddress.h"

namespace plc_vnext::layout {

// ---- REAL 反馈字段：每项占 2 个 D ----
constexpr HoldingAddress manualSpeed(int slot)      { return HoldingAddress(0    + 2 * slot); }  // D0
constexpr HoldingAddress positioningSpeed(int slot) { return HoldingAddress(32   + 2 * slot); }  // D32
constexpr HoldingAddress absPosition(int slot)      { return HoldingAddress(64   + 2 * slot); }  // D64
constexpr HoldingAddress relPosition(int slot)      { return HoldingAddress(96   + 2 * slot); }  // D96

// ---- INT / WORD 反馈字段：每项占 1 个 D ----
constexpr HoldingAddress motionState(int slot)      { return HoldingAddress(128  + slot); }      // D128
constexpr HoldingAddress motionLimit(int slot)      { return HoldingAddress(144  + slot); }      // D144
constexpr HoldingAddress alarmWord(int slot)        { return HoldingAddress(160  + slot); }      // D160

// ---- REAL 写入目标字段：每项占 2 个 D ----
constexpr HoldingAddress absPosTarget(int slot)     { return HoldingAddress(1096 + 2 * slot); }  // D1096
constexpr HoldingAddress relPosTarget(int slot)     { return HoldingAddress(1128 + 2 * slot); }  // D1128

}  // namespace plc_vnext::layout
