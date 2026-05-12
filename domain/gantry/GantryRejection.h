#pragma once

// 与 PLC 定义的联动拒绝原因（Gantry_Error_Code）保持一致，且扩展领域逻辑层的内部状态冲突等拦截原因
enum class GantryRejection {
    None = 0,
    PositionToleranceExceeded = 1, // 联动超差 (PLC: 1)
    X1NotEnabled = 2,              // X1 未使能 (PLC: 2)
    X2NotEnabled = 3,              // X2 未使能 (PLC: 3)
    X1NotStationary = 4,           // X1 未静止 (PLC: 4)
    X2NotStationary = 5,           // X2 未静止 (PLC: 5)
    
    // --- 以下为领域逻辑层拦截 ---
    StateConflict = 100,           // 内部状态机冲突（如：已在联动中再次请求联动）
    AxisStateError    = 101,              // 内部状态机错误（如：非法状态转换）
    UnknownError = 999
};