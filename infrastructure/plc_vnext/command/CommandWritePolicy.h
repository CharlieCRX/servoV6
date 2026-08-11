// ============================================================================
// CommandWritePolicy.h —— Step 8 command: 命令写入分类
// ============================================================================
// 决定 writer 对某类命令应如何写入：
//   - LevelHold    保持电平 / 可重复写（使能/点动/心跳/参数写）。
//   - SelfReset    PLC 自动复位，只写 ON，等待 PLC 读回清 OFF（触发/终止/清除）。
// 本类为纯函数、无状态、无 I/O；不 include Modbus/Qt/Domain。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/contracts/PlcCommand.h"

namespace plc_vnext::command {

enum class CommandWriteClass { LevelHold, SelfReset };

/// 命令写入分类：决定 writer 是否允许重复写 / 是否需要边沿 / 是否只写 ON。
class CommandWritePolicy {
public:
    static CommandWriteClass classify(contracts::PlcAxisCommandKind kind) {
        switch (kind) {
            case contracts::PlcAxisCommandKind::EnableAxis:
            case contracts::PlcAxisCommandKind::EnableMotor:
            case contracts::PlcAxisCommandKind::JogForward:
            case contracts::PlcAxisCommandKind::JogBackward:
            case contracts::PlcAxisCommandKind::JogHeartbeat:
            // 参数写（REAL 保持寄存器）同样可重复写，归入 LevelHold。
            case contracts::PlcAxisCommandKind::SetManualSpeed:
            case contracts::PlcAxisCommandKind::SetPositioningSpeed:
            case contracts::PlcAxisCommandKind::SetAbsTarget:
            case contracts::PlcAxisCommandKind::SetRelTarget:
                return CommandWriteClass::LevelHold;

            // 触发/终止/清除：PLC 自动复位，只写 ON，客户端无需配对 OFF。
            case contracts::PlcAxisCommandKind::TriggerAbsMove:
            case contracts::PlcAxisCommandKind::TriggerRelMove:
            case contracts::PlcAxisCommandKind::StopAbsMove:
            case contracts::PlcAxisCommandKind::StopRelMove:
            case contracts::PlcAxisCommandKind::ClearAbsPosition:
            case contracts::PlcAxisCommandKind::ClearRelZero:
            case contracts::PlcAxisCommandKind::SetRelZero:
                return CommandWriteClass::SelfReset;
        }
        // 未知/新增类别保守归为 LevelHold（可重复写、无触发副作用）。
        return CommandWriteClass::LevelHold;
    }

    [[nodiscard]] static bool isLevelHold(contracts::PlcAxisCommandKind kind) {
        return classify(kind) == CommandWriteClass::LevelHold;
    }

    [[nodiscard]] static bool isSelfReset(contracts::PlcAxisCommandKind kind) {
        return classify(kind) == CommandWriteClass::SelfReset;
    }
};

}  // namespace plc_vnext::command
