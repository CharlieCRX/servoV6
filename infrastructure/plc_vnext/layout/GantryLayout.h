// ============================================================================
// GantryLayout.h —— Step 3 layout: 龙门 Command/Status/Param 组级布局
// ============================================================================
// 与 tools/plc_read_validate.py 及地址表 §6/7/8 完全一致：
//   GantryCommand 基址 D180、步长 4；Command +0，RequestSeq(DINT) +1..+2；
//   GantryStatus  基址 D190、步长 18（A 组 D190..D207，B 组 D208..D225）；
//   GantryParam   基址 D1600、步长 22（A 组 D1600..D1621，B 组 D1622..D1643）。
// 纯布局：无 Modbus 库 / Qt / Domain；不出现业务轴名。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/layout/RegisterAddress.h"

namespace plc_vnext::layout {

/// GantryCommand[g] 的字段地址（基址 D180 + 4*g）。
struct GantryCommandLayout {
    HoldingAddress command;     // +0：INT，0无/1建立/2解除/3安全复位
    HoldingAddress requestSeq;  // +1..+2：DINT，低字在前
};

/// GantryCommand[g] 组级布局。
constexpr GantryCommandLayout gantryCommand(int g) {
    const HoldingAddress base(180 + 4 * g);
    return GantryCommandLayout{base, HoldingAddress(base.value() + 1)};
}

/// GantryStatus[g] 基址：D190 + 18*g。
constexpr HoldingAddress gantryStatusBase(int g) { return HoldingAddress(190 + 18 * g); }

/// GantryParam[g] 基址：D1600 + 22*g。
constexpr HoldingAddress gantryParamBase(int g)  { return HoldingAddress(1600 + 22 * g); }

}  // namespace plc_vnext::layout
