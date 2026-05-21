#pragma once
#include <string>

enum class AxisId {
    Y,
    Z,
    R,
    X,   // 逻辑龙门轴（联动模式下使用）
    X1,  // 物理龙门轴1（解耦模式下使用）
    X2   // 物理龙门轴2（解耦模式下使用）
};

/// @brief 将 AxisId 枚举值转换为可读字符串（用于日志输出）
inline const char* axisIdToString(AxisId id) {
    switch (id) {
        case AxisId::Y:  return "Y";
        case AxisId::Z:  return "Z";
        case AxisId::R:  return "R";
        case AxisId::X:  return "X";
        case AxisId::X1: return "X1";
        case AxisId::X2: return "X2";
    }
    return "?";
}
