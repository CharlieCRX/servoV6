#pragma once

enum class AxisId {
    Y,
    Z,
    R,
    X,   // 逻辑龙门轴（联动模式下使用）
    X1,  // 物理龙门轴1（解耦模式下使用）
    X2   // 物理龙门轴2（解耦模式下使用）
};
