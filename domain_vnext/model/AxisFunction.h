// ============================================================================
// AxisFunction.h —— P1 model: 轴功能角色枚举
// ============================================================================
// 纯领域值对象：表达轴的「功能身份」。轴实体本体放在全局 16 槽位注册表，
// 分组只是「功能视图」。功能绑定与 PLC 约定：
//   Role[0]=X1、Role[1]=X2、Role[2]=Y、Role[3]=Z、Role[4]=R、Role[5]=X(逻辑轴)
// 纯 DTO：不依赖旧 domain/*、Qt、Modbus；只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <optional>
#include <string_view>

namespace domain_vnext::model {

/// 轴功能角色：逻辑轴 X 与各物理轴 X1/X2/Y/Z/R。
enum class AxisFunction {
    X,   // 逻辑轴（SYN0，龙门联动后的统一运动入口，slot13）
    X1,
    X2,
    Y,
    Z,
    R,
};

/// 将 PLC 拓扑 Role 下标（0..5）映射为 AxisFunction。
///   Role[0]=X1、Role[1]=X2、Role[2]=Y、Role[3]=Z、Role[4]=R、Role[5]=X(逻辑轴)
inline std::optional<AxisFunction> axisFunctionFromRoleIndex(int idx) {
    switch (idx) {
        case 0: return AxisFunction::X1;
        case 1: return AxisFunction::X2;
        case 2: return AxisFunction::Y;
        case 3: return AxisFunction::Z;
        case 4: return AxisFunction::R;
        case 5: return AxisFunction::X;
        default: return std::nullopt;
    }
}

/// 可读名称（用于日志/诊断）。
inline std::string_view axisFunctionName(AxisFunction f) {
    switch (f) {
        case AxisFunction::X:  return "X";
        case AxisFunction::X1: return "X1";
        case AxisFunction::X2: return "X2";
        case AxisFunction::Y:  return "Y";
        case AxisFunction::Z:  return "Z";
        case AxisFunction::R:  return "R";
    }
    return "?";
}

}  // namespace domain_vnext::model
