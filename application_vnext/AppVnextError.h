// ============================================================================
// AppVnextError.h —— P5 app: 应用层错误聚合
// ============================================================================
// 复用 domain_vnext 各领域层的错误类型（不重新发明），SystemManagerVnext 在
// 跨越「查找 / 领域意图校验 / 映射 / 通讯」多个层级时，用本 variant 聚合
// 所有可能的失败，不做类型擦除。std::monostate 代表「成功，无错误」。
// 纯 C++：只依赖自身 + domain_vnext + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <cstdint>
#include <variant>

#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/state/AxisStateMachine.h"
#include "domain_vnext/state/GantryCouplingStateMachine.h"
#include "domain_vnext/state/SafetyStateMachine.h"
#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"

namespace application_vnext {

/// 尚未成功 boot（readTopology 失败 / 未调用 boot）。控制与龙门一律拒绝。
struct AppNotBooted {};

/// 指定 (组, 功能) 未注册为轴实体。
struct AxisNotFound {
    domain_vnext::model::AxisFunction function = domain_vnext::model::AxisFunction::X;
};

/// 该组未开放控制（配置无效 / 降级锁定），禁止普通控制。
struct AxisNotReady {
    domain_vnext::model::AxisFunction function = domain_vnext::model::AxisFunction::X;
};

/// 领域单轴状态机拒绝了该运动意图（系统锁定 / 龙门未同步 / 轴忙）。
struct SubmitRejectedState {
    domain_vnext::state::AxisStateMachine::SubmitResult result =
        domain_vnext::state::AxisStateMachine::SubmitResult::RejectedSystemLocked;
};

/// 映射层不支持该命令（如 ResetAlarm 按 PLC 能力拒绝）。
struct CommandUnsupported {
    domain_vnext::model::AxisCommandKind kind = domain_vnext::model::AxisCommandKind::EnableAxis;
};

/// 单轴写入通讯失败（命令已生成但未送达 PLC）。
struct CommFailed {
    plc_vnext::contracts::CommunicationResult result;
};

/// 安全域拒绝（未同步 / 幂等冲突 / 非法跃迁）。
struct SafetyRejected {
    domain_vnext::state::SafetyRejection rejection = domain_vnext::state::SafetyRejection::None;
};

/// 龙门联动状态机拒绝请求（未同步 / 故障 / 状态冲突 / 准入不满足）。
struct GantryRequestRejected {
    domain_vnext::state::GantryCouplingStateMachine::RequestResult result =
        domain_vnext::state::GantryCouplingStateMachine::RequestResult::RejectedUnconfigured;
};

/// 龙门请求提交通讯失败（Command/RequestSeq 未确定性送达 PLC）。
struct GantryCommFailed {
    plc_vnext::contracts::GantrySubmitResult result;
};

/// 应用层统一错误聚合类型。std::monostate 代表成功。
using AppVnextError = std::variant<
    AppNotBooted,
    AxisNotFound,
    AxisNotReady,
    SubmitRejectedState,
    CommandUnsupported,
    CommFailed,
    SafetyRejected,
    GantryRequestRejected,
    GantryCommFailed>;

/// 应用层统一结果：monostate=成功，否则为具体错误。
using AppVnextResult = std::variant<std::monostate, AppVnextError>;

/// 判断结果是否成功。
[[nodiscard]] inline bool appResultOk(const AppVnextResult& r) {
    return std::holds_alternative<std::monostate>(r);
}

/// 从结果取具体错误；成功时返回 nullptr。在内层 AppVnextError 中查找。
template <typename E>
[[nodiscard]] inline const E* appErrorOf(const AppVnextResult& r) {
    const AppVnextError* inner = std::get_if<AppVnextError>(&r);
    if (!inner) return nullptr;
    return std::get_if<E>(inner);
}

}  // namespace application_vnext
