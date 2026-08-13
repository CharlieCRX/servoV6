// ============================================================================
// CommissioningTypes.h —— 阶段3：单轴物理调试的类型定义（操作 + 闸门原因）
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§10.3“阶段3：单轴受控写入”与阶段3流程：
//   - CommissioningOperation：可执行的操作类型（Step 3.0）；
//   - CommissioningGateReason：写入闸门拒绝时返回的明确原因。
// 纯 C++：只依赖自身，不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

namespace application_vnext::commissioning {

/// 单轴调试操作类型（Step 3.0 建议定义）。
enum class CommissioningOperation {
    ParameterWrite,       // 参数写入（手动速度 / 定位速度 / 绝对目标 / 相对目标）
    Enable,               // 轴控 / 电机使能
    Jog,                  // 点动（正 / 反，含心跳会话）
    Move,                 // 相对 / 绝对定位
    Stop,                 // 停止（点动 / 定位）
    ReleaseEmergencyStop, // 解除急停（M225，急停期间唯一允许的写）
};

/// 写入闸门拒绝原因（Step 3.0 明确列表）。
enum class CommissioningGateReason {
    Ok,
    Disconnected,      // PLC 未连接
    TopologyInvalid,   // 拓扑读取失败 / Magic / SchemaVersion 不符 / ConfigValid=false
    RevisionChanged,   // Revision 相对上次已变化
    RuntimeUntrusted,  // 运行快照不可信 / 目标槽位反馈不可信
    SafetyUnknown,     // 急停状态不可读
    EmergencyStop,     // M224=true，普通操作拒绝
    AxisAlarm,         // 目标轴报警
    SlotNotAllowed,    // slot 非 0/1（本阶段仅允许 slot 0、slot 1）
    Busy,              // 当前存在其它点动会话
};

/// 闸门原因的稳定文本（用于日志 / 探针输出）。
inline const char* gateReasonText(CommissioningGateReason r) {
    switch (r) {
        case CommissioningGateReason::Ok:               return "Ok";
        case CommissioningGateReason::Disconnected:     return "Disconnected";
        case CommissioningGateReason::TopologyInvalid:  return "TopologyInvalid";
        case CommissioningGateReason::RevisionChanged:  return "RevisionChanged";
        case CommissioningGateReason::RuntimeUntrusted: return "RuntimeUntrusted";
        case CommissioningGateReason::SafetyUnknown:    return "SafetyUnknown";
        case CommissioningGateReason::EmergencyStop:    return "EmergencyStop";
        case CommissioningGateReason::AxisAlarm:        return "AxisAlarm";
        case CommissioningGateReason::SlotNotAllowed:   return "SlotNotAllowed";
        case CommissioningGateReason::Busy:             return "Busy";
    }
    return "?";
}

}  // namespace application_vnext::commissioning
