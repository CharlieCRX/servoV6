#pragma once

#include "domain/entity/AxisId.h"
#include "application_vnext/control/OperationState.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

// ═══════════════════════════════════════════════════════════════════
// UDP 协议层：命令码、JSON 字段名常量与 motor 映射
// 参考: docs/architecture/UDP通讯层设计文档.md §2, §3.3.2
// ═══════════════════════════════════════════════════════════════════

// ============================================================
// 命令码枚举
// ============================================================
enum class UdpCmd : int {
    MOVE_TO_REL_TARGET = 0,  // 基于相对零点的绝对位置移动
    MOVE_OFFSET        = 1,  // 相对偏移移动
    GET_REL_POSITION   = 2,  // 获取当前相对位置
    SET_MOVE_SPEED     = 3,  // 设置位置移动速度
    GET_MOVE_SPEED     = 4,  // 获取位置移动速度
    SET_REL_ZERO       = 5,  // 设置相对零点
    QUERY_OPERATION    = 6,  // 查询异步操作状态（仅需 cmd + operationId）
};

/// 将 UdpCmd 转换为可读名称（用于日志/错误消息）
inline const char* udpCmdName(UdpCmd cmd) {
    switch (cmd) {
        case UdpCmd::MOVE_TO_REL_TARGET: return "MOVE_TO_REL_TARGET (cmd=0)";
        case UdpCmd::MOVE_OFFSET:        return "MOVE_OFFSET (cmd=1)";
        case UdpCmd::GET_REL_POSITION:   return "GET_REL_POSITION (cmd=2)";
        case UdpCmd::SET_MOVE_SPEED:     return "SET_MOVE_SPEED (cmd=3)";
        case UdpCmd::GET_MOVE_SPEED:     return "GET_MOVE_SPEED (cmd=4)";
        case UdpCmd::SET_REL_ZERO:       return "SET_REL_ZERO (cmd=5)";
        case UdpCmd::QUERY_OPERATION:    return "QUERY_OPERATION (cmd=6)";
    }
    return "UNKNOWN";
}

// ============================================================
// JSON 字段名常量 —— 避免字符串硬编码
// ============================================================
namespace UdpField {
    constexpr const char* CMD    = "cmd";
    constexpr const char* GROUP  = "group";
    constexpr const char* MOTOR  = "motor";
    constexpr const char* TARGET = "target";
    constexpr const char* OFFSET = "offset";
    constexpr const char* SPEED  = "speed";
    constexpr const char* RESULT = "result";
    constexpr const char* MSG    = "msg";
    constexpr const char* CURR   = "curr";   // 当前相对位置

    // ── Phase 5 异步 operationId 回包字段 ──
    constexpr const char* OP_ID     = "operationId";  // 运动/写入命令的异步回执标识
    constexpr const char* STATE     = "state";        // OperationState 可读名（Queued/Running/...）
    constexpr const char* OP_KIND   = "kind";         // OperationKind 可读名（Positioning/Jog/...）
    constexpr const char* SOURCE    = "source";       // 命令来源（UDP）
    constexpr const char* AXIS      = "axis";         // "A.R" / "A.X" ...
    constexpr const char* MOTION    = "motionState";  // PLC motionState（运行中反馈）
    constexpr const char* POS       = "position";     // 当前绝对位置（运行中反馈）
    constexpr const char* DIAG      = "diag";         // 状态诊断文本
}

// ============================================================
// motor 值 → domain_vnext::AxisFunction 映射（Phase 5：UDP -> ControlCommand）
//   旧协议 motor 值含义（与 motorName 一致）：
//     motor=0 → Y、1 → Z、2 → R、3 → X(逻辑轴)、4 → X1、5 → X2
// ============================================================
inline std::optional<domain_vnext::model::AxisFunction> motorToFunction(int motor) {
    using domain_vnext::model::AxisFunction;
    switch (motor) {
        case 0: return AxisFunction::Y;
        case 1: return AxisFunction::Z;
        case 2: return AxisFunction::R;
        case 3: return AxisFunction::X;
        case 4: return AxisFunction::X1;
        case 5: return AxisFunction::X2;
        default: return std::nullopt;
    }
}

/// group 名称 → 组索引（A=0 / B=1）。识别 "Machine_A"/"A"/"Machine_B"/"B" 等常用名。
/// 返回 std::nullopt 表示无法识别该分组。
inline std::optional<int> groupNameToIndex(const std::string& name) {
    if (name == "Machine_A" || name == "A" || name == "A组") return 0;
    if (name == "Machine_B" || name == "B" || name == "B组") return 1;
    return std::nullopt;
}

/// OperationState → 可读名（UDP queryOperation 回包 / 日志）。
inline const char* operationStateName(application_vnext::control::OperationState st) {
    using application_vnext::control::OperationState;
    switch (st) {
        case OperationState::Queued:           return "Queued";
        case OperationState::Accepted:         return "Accepted";
        case OperationState::Rejected:         return "Rejected";
        case OperationState::Running:          return "Running";
        case OperationState::Succeeded:        return "Succeeded";
        case OperationState::Failed:           return "Failed";
        case OperationState::Cancelled:        return "Cancelled";
        case OperationState::TimedOut:         return "TimedOut";
        case OperationState::CommitUncertain:  return "CommitUncertain";
    }
    return "?";
}

/// OperationKind → 可读名。
inline const char* operationKindName(application_vnext::control::OperationKind k) {
    using application_vnext::control::OperationKind;
    switch (k) {
        case OperationKind::Positioning:     return "Positioning";
        case OperationKind::Jog:             return "Jog";
        case OperationKind::GantryLifecycle: return "GantryLifecycle";
    }
    return "?";
}

// ============================================================
// R 轴 motor 值 —— 唯一支持的 motor ID
// ============================================================
constexpr int R_MOTOR_ID = 2;

/// motor 值 → AxisId 转换（仅 R 轴通过，其余返回 false）
inline bool motorToAxisId(int motor, AxisId& outId) {
    if (motor == R_MOTOR_ID) {
        outId = AxisId::R;
        return true;
    }
    return false;
}

/// 将 motor 值转换为人类可读的轴名称（用于错误消息）
inline const char* motorName(int motor) {
    switch (motor) {
        case 0: return "Y (motor=0)";
        case 1: return "Z (motor=1)";
        case 2: return "R (motor=2)";
        case 3: return "X (motor=3)";
        case 4: return "X1 (motor=4)";
        case 5: return "X2 (motor=5)";
        default: return "?";
    }
}