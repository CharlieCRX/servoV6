// ============================================================================
// ControlCommand.h —— Phase 0：统一命令模型
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§4.1。
//
// UI / 摇杆 / UDP 是三个「命令来源」，PLC 反馈与应用会话状态是唯一「状态来源」。
// 本头文件定义三种来源统一使用的命令模型：不携带 PLC 槽位/地址，只描述业务意图，
// 最终只由 MotionControlService 的唯一调度循环执行。
//
// 纯头文件：不依赖 Qt / Modbus / 旧 domain/*。只依赖：
//   - domain_vnext::model::AxisFunction（轴功能角色）
//   - plc_vnext::contracts::PlcGroupIndex（强类型组号）
// ============================================================================
#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::control {

/// 命令来源（全链路追踪与仲裁用）。
enum class ControlSource {
    Ui,           // QML 按钮 / 面板
    Joystick,     // 摇杆 / 手柄
    Udp,          // UDP 数据包
    Maintenance,  // 维护模式（仅 PhysicalAxisCommissioningService 经由此源）
};

/// 业务目标轴：组 + 功能角色。不携带 PLC 槽位/地址。
struct AxisTarget {
    plc_vnext::contracts::PlcGroupIndex group{0};
    domain_vnext::model::AxisFunction function = domain_vnext::model::AxisFunction::X;
};

/// 统一业务动作。`Set*` 仅用于界面预填值；`Start*Move` 自身携带目标与速度为
/// 一次原子业务意图（见 §5.3 目标值归属）。
enum class ControlAction {
    SetManualSpeed,
    SetPositioningSpeed,
    SetAbsTarget,
    SetRelTarget,

    StartJogForward,
    StartJogBackward,
    StopJog,

    StartAbsMove,
    StartRelMove,
    StopMotion,

    EnableAxis,
    EnableMotor,

    GantryEnableAndCouple,
    GantryDecoupleAndDisable,

    EmergencyStop,
    ReleaseEmergencyStop,
};

/// `Start*Move` 携带的目标与速度（原子业务意图，见 §5.3 目标值归属）。
/// 避免「目标由某来源设置、速度由另一来源设置」的竞争；Phase 3 执行时
/// 一次性落到 PLC（先写目标 D 寄存器，再触发 M 寄存器），不改分步来源。
struct MotionRequest {
    float target = 0.0f;   ///< 绝对/相对目标（EU）
    float speed  = 0.0f;   ///< 定位速度（EU/s）
};

/// 统一控制命令：不携带 PLC 地址，只描述业务意图。
struct ControlCommand {
    std::string operationId;                       // 全链路追踪、UDP 回包关联
    ControlSource source = ControlSource::Ui;
    AxisTarget target;
    ControlAction action{};
    float value = 0.0f;                            // SetManualSpeed/SetPositioningSpeed/SetAbsTarget/SetRelTarget
    bool level = false;                            // EnableAxis/EnableMotor 的 on；StartJog 预留
    std::optional<MotionRequest> motion;           // StartAbsMove/StartRelMove 的原子 目标+速度 负载
    std::chrono::steady_clock::time_point createdAt = std::chrono::steady_clock::now();
    std::chrono::milliseconds ttl{1000};           // 超时未被执行则丢弃（TimedOut）
};

/// 可读名称（日志 / UDP 回包 / 诊断）。
inline const char* controlSourceName(ControlSource s) {
    switch (s) {
        case ControlSource::Ui:         return "UI";
        case ControlSource::Joystick:   return "Joystick";
        case ControlSource::Udp:        return "UDP";
        case ControlSource::Maintenance:return "Maintenance";
    }
    return "?";
}

inline const char* controlActionName(ControlAction a) {
    switch (a) {
        case ControlAction::SetManualSpeed:      return "SetManualSpeed";
        case ControlAction::SetPositioningSpeed: return "SetPositioningSpeed";
        case ControlAction::SetAbsTarget:        return "SetAbsTarget";
        case ControlAction::SetRelTarget:        return "SetRelTarget";
        case ControlAction::StartJogForward:     return "StartJogForward";
        case ControlAction::StartJogBackward:    return "StartJogBackward";
        case ControlAction::StopJog:             return "StopJog";
        case ControlAction::StartAbsMove:        return "StartAbsMove";
        case ControlAction::StartRelMove:        return "StartRelMove";
        case ControlAction::StopMotion:          return "StopMotion";
        case ControlAction::EnableAxis:          return "EnableAxis";
        case ControlAction::EnableMotor:         return "EnableMotor";
        case ControlAction::GantryEnableAndCouple:   return "GantryEnableAndCouple";
        case ControlAction::GantryDecoupleAndDisable:return "GantryDecoupleAndDisable";
        case ControlAction::EmergencyStop:       return "EmergencyStop";
        case ControlAction::ReleaseEmergencyStop:return "ReleaseEmergencyStop";
    }
    return "?";
}

/// 轴目标的可读名（如 "A.Y"），用于日志 / OperationEntry.axis。
inline std::string axisTargetName(const AxisTarget& t) {
    const char g = (t.group.value() == 0) ? 'A' : 'B';
    return std::string(1, g) + "." + std::string(domain_vnext::model::axisFunctionName(t.function));
}

}  // namespace application_vnext::control
