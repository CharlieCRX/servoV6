// ============================================================================
// CommandMapper.h —— P4 command: domain command -> plc_vnext::contracts 映射
// ============================================================================
// 设计稿 §5.2 / §3 command/CommandMapper.h：
//   axis.outbox().drain() -> vector<AxisCommand>
//     -> CommandMapper::map -> vector<plc_vnext::contracts::PlcAxisCommand>
//     -> gateway.writeAxis(slot, cmd)
// 职责：
//   - 把领域 AxisCommandKind 映射为 plc_vnext::contracts::PlcAxisCommandKind（§4.2）；
//   - value/level 透传（参数写 -> realValue；保持电平线圈 -> boolValue）；
//   - ResetAlarm 按 PLC 能力返回 UnsupportedByPlcVersion（§7 注，M112 未实现）；
//   - A 组使能入口路由（§4.6a）：龙门成员 X1/X2 的 EnableAxis/EnableMotor
//     一律改写为组内逻辑轴槽位（如 slot13），独立轴仍写自身槽位。
// 龙门请求（GantryRequest）本身已是 plc_vnext::contracts 纯 DTO，无需翻译；
// 本映射器聚焦单轴命令。纯领域：只依赖自身 + model + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <optional>

#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"

namespace domain_vnext::command {

/// 单命令映射结果（区分 UnsupportedByPlcVersion / 未知类别）。
struct AxisMapResult {
    enum class Status {
        Mapped,                     // 已映射，cmd 有效
        UnsupportedByPlcVersion,    // PLC 版本/能力不支持（如 ResetAlarm M112 未实现）
        UnsupportedKind,            // 未知领域命令类别（不应出现，防御性）
    };
    Status status = Status::UnsupportedKind;
    plc_vnext::contracts::PlcAxisCommand cmd;

    [[nodiscard]] bool ok() const { return status == Status::Mapped; }
};

/// 携带有效写槽位的已映射命令（供 writeAxis 直接消费）。
struct MappedAxisCommand {
    plc_vnext::contracts::PlcAxisSlot slot;
    plc_vnext::contracts::PlcAxisCommand cmd;
};

/// 领域命令 -> plc_vnext contracts 映射器（纯函数，无状态）。
class CommandMapper {
public:
    /// 领域命令类别 -> plc 命令类别（§4.2 逐项）。ResetAlarm 亦返回，见 mapAxis 前置拦截。
    static std::optional<plc_vnext::contracts::PlcAxisCommandKind> mapKind(
        model::AxisCommandKind kind);

    /// 单命令映射：kind + value/level 透传。ResetAlarm -> UnsupportedByPlcVersion。
    static AxisMapResult mapAxis(const model::AxisCommand& cmd);

    /// 信封映射（含槽位）；不支持的命令返回 nullopt（调用方跳过）。
    static std::optional<MappedAxisCommand> mapEnvelope(
        const model::AxisCommandEnvelope& env);

    /// A 组使能入口路由（§4.6a）：龙门成员 X1/X2 的 EnableAxis/EnableMotor
    /// 改写为组内逻辑轴槽位（logicalAxisSlot）；否则返回自身槽位。
    static plc_vnext::contracts::PlcAxisSlot effectiveEnableSlot(
        model::AxisFunction fn,
        plc_vnext::contracts::PlcAxisCommandKind kind,
        plc_vnext::contracts::PlcAxisSlot ownSlot,
        std::optional<plc_vnext::contracts::PlcAxisSlot> logicalAxisSlot);
};

// ---------------------------------------------------------------------------
// 内联实现
// ---------------------------------------------------------------------------

inline std::optional<plc_vnext::contracts::PlcAxisCommandKind>
CommandMapper::mapKind(model::AxisCommandKind kind) {
    using D = model::AxisCommandKind;
    using P = plc_vnext::contracts::PlcAxisCommandKind;
    switch (kind) {
        case D::EnableAxis:            return P::EnableAxis;
        case D::ClearRelZero:          return P::ClearRelZero;
        case D::ClearAbsPosition:      return P::ClearAbsPosition;
        case D::TriggerAbsMove:        return P::TriggerAbsMove;
        case D::TriggerRelMove:        return P::TriggerRelMove;
        case D::JogForward:            return P::JogForward;
        case D::JogBackward:           return P::JogBackward;
        case D::ResetAlarm:            return P::ResetAlarm;
        case D::EnableMotor:           return P::EnableMotor;
        case D::StopRelMove:           return P::StopRelMove;
        case D::StopAbsMove:           return P::StopAbsMove;
        case D::SetRelZero:            return P::SetRelZero;
        case D::JogHeartbeat:          return P::JogHeartbeat;
        case D::ClearAlarmWord:        return P::ClearAlarmWord;
        case D::SetManualSpeed:        return P::SetManualSpeed;
        case D::SetPositioningSpeed:   return P::SetPositioningSpeed;
        case D::SetAbsDistance:        return P::SetAbsTarget;
        case D::SetRelDistance:        return P::SetRelTarget;
        case D::SetSoftNegLimit:       return P::SetSoftNegLimit;
        case D::SetSoftPosLimit:       return P::SetSoftPosLimit;
        case D::SetSoftLimitControl:   return P::SetSoftLimitControl;
        case D::SetRelZeroRecord:      return P::SetRelZeroRecord;
    }
    return std::nullopt;
}

inline AxisMapResult CommandMapper::mapAxis(const model::AxisCommand& cmd) {
    // ResetAlarm(M112)：当前地址表标注「不可用/已注释」，PLC 未实现复位逻辑。
    // 领域接口保留，映射层按 PLC 能力返回 UnsupportedByPlcVersion（§7 注）。
    if (cmd.kind == model::AxisCommandKind::ResetAlarm) {
        return {AxisMapResult::Status::UnsupportedByPlcVersion, {}};
    }
    const auto kind = mapKind(cmd.kind);
    if (!kind) {
        return {AxisMapResult::Status::UnsupportedKind, {}};
    }
    plc_vnext::contracts::PlcAxisCommand pc;
    pc.kind = *kind;
    pc.realValue = cmd.value;
    pc.boolValue = cmd.level;
    return {AxisMapResult::Status::Mapped, pc};
}

inline std::optional<MappedAxisCommand> CommandMapper::mapEnvelope(
    const model::AxisCommandEnvelope& env) {
    const auto r = mapAxis(env.cmd);
    if (!r.ok()) return std::nullopt;
    return MappedAxisCommand{env.slot, r.cmd};
}

inline plc_vnext::contracts::PlcAxisSlot CommandMapper::effectiveEnableSlot(
    model::AxisFunction fn,
    plc_vnext::contracts::PlcAxisCommandKind kind,
    plc_vnext::contracts::PlcAxisSlot ownSlot,
    std::optional<plc_vnext::contracts::PlcAxisSlot> logicalAxisSlot) {
    using P = plc_vnext::contracts::PlcAxisCommandKind;
    const bool isEnable =
        kind == P::EnableAxis || kind == P::EnableMotor;
    const bool isGantryMember =
        fn == model::AxisFunction::X1 || fn == model::AxisFunction::X2;
    if (isEnable && isGantryMember && logicalAxisSlot.has_value()) {
        return *logicalAxisSlot;
    }
    return ownSlot;
}

}  // namespace domain_vnext::command

