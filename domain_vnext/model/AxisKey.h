// ============================================================================
// AxisKey.h —— P1 model: 轴的领域身份
// ============================================================================
// 纯领域值对象：(PlcGroupIndex, AxisFunction)。轴的实体本体放在全局 16 槽位
// 注册表，AxisKey 用于「分组功能视图 -> 全局轴实体」的查找键。
// 依赖方向：domain_vnext -> plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace domain_vnext::model {

/// 轴的领域身份 = (PLC 组号, 轴功能)。
struct AxisKey {
    plc_vnext::contracts::PlcGroupIndex group;
    AxisFunction function;

    friend bool operator==(AxisKey a, AxisKey b) {
        return a.group == b.group && a.function == b.function;
    }
    friend bool operator!=(AxisKey a, AxisKey b) { return !(a == b); }
    friend bool operator<(AxisKey a, AxisKey b) {
        if (a.group == b.group) {
            return static_cast<int>(a.function) < static_cast<int>(b.function);
        }
        return a.group < b.group;
    }
};

}  // namespace domain_vnext::model
