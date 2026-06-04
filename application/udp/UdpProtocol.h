#pragma once

#include "domain/entity/AxisId.h"

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