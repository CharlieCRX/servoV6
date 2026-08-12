// ============================================================================
// SystemCommand.h —— P4 command: 统一命令 variant（命令产出边界）
// ============================================================================
// 设计稿 §3 command/SystemCommand.h：
//   variant<AxisCommandEnvelope, GantryAction, EStopCommand>
//  - AxisCommandEnvelope：单轴命令（含目标槽位）；
//   - GantryAction       ：龙门请求（组号 + GantryRequest 事务，请求已含自增 seq）；
//   - EStopCommand       ：全局急停（M224 锁存 / M225 沿解除）。
// 上层把领域意图统一装进本 variant，交 CommandMapper/驱动适配消费。
// 纯领域：只依赖自身 + model + state + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <variant>

#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/state/SafetyStateMachine.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace domain_vnext::command {

/// 龙门动作：组号 + 已由 GantryCouplingStateMachine 生成的 GantryRequest 事务。
struct GantryAction {
    plc_vnext::contracts::PlcGroupIndex group;
    plc_vnext::contracts::GantryRequest request;
};

/// 统一命令变体。驱动适配（P5/app）经 std::visit 分发。
using SystemCommand = std::variant<
    model::AxisCommandEnvelope,   // 单轴命令（slot + AxisCommand）
    GantryAction,                 // 龙门请求（组事务）
    state::EStopCommand>;         // 全局急停

}  // namespace domain_vnext::command
