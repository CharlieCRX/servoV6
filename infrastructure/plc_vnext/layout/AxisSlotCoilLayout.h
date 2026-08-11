// ============================================================================
// AxisSlotCoilLayout.h —— Step 8 layout: 槽位 0..15 的 M 区线圈命令地址公式
// ============================================================================
// 地址公式与《PLC变量协议_Modbus最终地址表.md》§3.2 完全一致（0 基址，不加 1）：
//   M 区线圈地址 == M 编号；每种命令一组连续 16 个线圈，i=0..15。
// 槽位越界（非 0..15）由 contracts::PlcAxisSlot 保证；公式本身为 constexpr。
// 纯布局：无 Modbus 库 / Qt / Domain；不出现业务轴名（x_axis/y_axis/...）。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/layout/RegisterAddress.h"

namespace plc_vnext::layout {

// ---- 保持电平线圈：本机负责最终 OFF（按启停/按住状态写 ON/OFF）----
constexpr CoilAddress enableAxis(int slot)      { return CoilAddress(0    + slot); }  // M0..M15   使能轴控
constexpr CoilAddress jogForward(int slot)      { return CoilAddress(80   + slot); }  // M80..M95  点动正转
constexpr CoilAddress jogBackward(int slot)     { return CoilAddress(96   + slot); }  // M96..M111 点动反转
constexpr CoilAddress enableMotor(int slot)     { return CoilAddress(128  + slot); }  // M128..M143 使能电机
constexpr CoilAddress jogHeartbeat(int slot)    { return CoilAddress(192  + slot); }  // M192..M207 点动心跳（周期写 ON）

// ---- 边沿触发：客户端写 ON 后必须回写 OFF（禁止重放）----
constexpr CoilAddress triggerAbsMove(int slot)  { return CoilAddress(48   + slot); }  // M48..M63  绝对定位触发
constexpr CoilAddress triggerRelMove(int slot)  { return CoilAddress(64   + slot); }  // M64..M79  相对定位触发
constexpr CoilAddress stopRelMove(int slot)     { return CoilAddress(144  + slot); }  // M144..M159 相对定位终止
constexpr CoilAddress stopAbsMove(int slot)     { return CoilAddress(160  + slot); }  // M160..M175 绝对定位终止

// ---- PLC 自复位：只写 ON，PLC 执行后自复位读回 OFF ----
constexpr CoilAddress clearRelZero(int slot)    { return CoilAddress(16   + slot); }  // M16..M31  相对原点清除
constexpr CoilAddress clearAbsPosition(int slot){ return CoilAddress(32   + slot); }  // M32..M47  绝对位置清零
constexpr CoilAddress setRelZero(int slot)      { return CoilAddress(176  + slot); }  // M176..M191 相对原点设置

}  // namespace plc_vnext::layout
