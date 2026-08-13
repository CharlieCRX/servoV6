// ============================================================================
// SystemCoilLayout.h —— Step 阶段3 layout: 系统级线圈（急停 M224/M225）地址公式
// ============================================================================
// 与《PLC变量协议_Modbus最终地址表.md》§4.4 一致（0 基址，M 编号 == 线圈地址）：
//   M224 = 设备急停（锁存，写 ON 触发；由外部急停按钮或上位机 M224=ON 置位）
//   M225 = 设备急停解除请求（PLC 自复位，上位机只写 ON，不补写 OFF）
// 纯布局：无 Modbus 库 / Qt / Domain。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/layout/RegisterAddress.h"

namespace plc_vnext::layout {

/// 设备急停（锁存，触发型线圈：写 ON 置位）。地址 0 基址 == M224。
constexpr CoilAddress emergencyStop()        { return CoilAddress(224); }

/// 设备急停解除请求（PLC 自复位：只写 ON，解除后 M224/M225 自动 OFF）。
constexpr CoilAddress emergencyStopRelease() { return CoilAddress(225); }

}  // namespace plc_vnext::layout
