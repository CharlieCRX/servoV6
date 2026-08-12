// ============================================================================
// SafetyState.h —— P1 model: 全局急停五态
// ============================================================================
// 保留旧 EmergencyStopController 的五态语义（§4.4）：
//   NotSynchronized / Running / EmergencyStopping / EmergencyStopped / ReleasingEmergencyStop
// 映射：EmergencyStopCommand{true}  -> 写 M224 ON（锁存保持）；
//       {false}                     -> 写 M225 上升沿解除（PLC 自复位并清 M224）。
// 纯领域值对象：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

namespace domain_vnext::model {

/// 系统安全状态 —— 五态工业安全状态机。
enum class SafetyState {
    NotSynchronized,        // 尚未同步 PLC 真实安全状态
    Running,                // 系统运行许可有效
    EmergencyStopping,      // 急停命令已下发，等待 PLC 停机确认
    EmergencyStopped,       // 系统已锁定
    ReleasingEmergencyStop, // 解除命令已下发，等待 PLC 解锁确认
};

/// 可读名称（用于日志/诊断）。
inline const char* safetyStateName(SafetyState s) {
    switch (s) {
        case SafetyState::NotSynchronized:        return "NotSynchronized";
        case SafetyState::Running:                return "Running";
        case SafetyState::EmergencyStopping:      return "EmergencyStopping";
        case SafetyState::EmergencyStopped:       return "EmergencyStopped";
        case SafetyState::ReleasingEmergencyStop: return "ReleasingEmergencyStop";
    }
    return "?";
}

}  // namespace domain_vnext::model
