// ============================================================================
// JoystickCommandBuilder.h —— Phase 4：摇杆 -> ControlCommand 纯命令构造
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 4：
//   `MotionController` 不再持有 QtAxisViewModel，改为提交 ControlCommand。
//
// 本头把「摇杆动作 -> 统一业务意图命令」抽成纯函数（无 Qt、无旧 domain/*、
// 无 InputEvent），便于在无 Qt 的 application_vnext_tests 通道直接单测。
// `MotionController`（QObject 薄壳）只负责选轴/死区/模式状态，命令一律经
// 本 builder 构造后 submit 给 MotionControlService，绝不在摇杆侧写 PLC。
//
// 依赖方向：presentation/input -> application_vnext::control + domain_vnext + plc_vnext。
// ============================================================================
#pragma once

#include "application_vnext/control/ControlCommand.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace presentation::input::joystick {

/// 默认摇杆分组。真实 MotionController 会从 AxisSelectionModel 读取当前 A/B 组；
/// 本 helper 仅供旧测试和显式构造 A 组命令时使用。
inline plc_vnext::contracts::PlcGroupIndex joystickGroup() {
    return plc_vnext::contracts::PlcGroupIndex(0);
}

/// JOG 前进：提交 Joystick 源的 StartJogForward（业务意图，不携带 PLC 地址）。
inline application_vnext::control::ControlCommand makeJogForwardCommand(
    const application_vnext::control::AxisTarget& t) {
    application_vnext::control::ControlCommand cmd;
    cmd.source = application_vnext::control::ControlSource::Joystick;
    cmd.target = t;
    cmd.action = application_vnext::control::ControlAction::StartJogForward;
    return cmd;
}

/// JOG 后退：提交 Joystick 源的 StartJogBackward。
inline application_vnext::control::ControlCommand makeJogBackwardCommand(
    const application_vnext::control::AxisTarget& t) {
    application_vnext::control::ControlCommand cmd;
    cmd.source = application_vnext::control::ControlSource::Joystick;
    cmd.target = t;
    cmd.action = application_vnext::control::ControlAction::StartJogBackward;
    return cmd;
}

/// 停止点动：提交 Joystick 源的 StopJog（按 owner 过滤，只停本会话、不误停他轴）。
inline application_vnext::control::ControlCommand makeStopJogCommand(
    const application_vnext::control::AxisTarget& t) {
    application_vnext::control::ControlCommand cmd;
    cmd.source = application_vnext::control::ControlSource::Joystick;
    cmd.target = t;
    cmd.action = application_vnext::control::ControlAction::StopJog;
    return cmd;
}

/// 定位触发：提交 Joystick 源的 StartAbsMove / StartRelMove（携带目标+速度为原子意图）。
/// 调用方必须显式提供正定位速度（speed），不允许省略/默认 0 —— 协调层 execute()
/// 会对 speed<=0 权威拒绝，杜绝任何来源把定位速度写成 0 覆盖 PLC。
inline application_vnext::control::ControlCommand makePositionCommand(
    const application_vnext::control::AxisTarget& t, bool abs,
    float target, float speed) {
    application_vnext::control::ControlCommand cmd;
    cmd.source = application_vnext::control::ControlSource::Joystick;
    cmd.target = t;
    cmd.action = abs ? application_vnext::control::ControlAction::StartAbsMove
                     : application_vnext::control::ControlAction::StartRelMove;
    cmd.motion = application_vnext::control::MotionRequest{target, speed};
    return cmd;
}

}  // namespace presentation::input::joystick
