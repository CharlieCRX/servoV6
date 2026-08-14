// ============================================================================
// OperationState.h —— Phase 0：操作生命周期枚举 + 控制资源 / 租约模型
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》§6.1 / §7.1。
//
// 本头文件承载：
//   - OperationState：操作生命周期状态（UI 与 UDP 展示、UDP 回包）
//   - OperationKind：操作种类
//   - ControlResource：控制资源（租约键，轴资源 或 龙门组资源）
//   - OperationLease / ResourceIndex：一操作多资源的租约模型
//   - requiredResources()：由「命令 + 拓扑」计算操作所需的全部资源集合
//
// 纯头文件：不依赖 Qt / Modbus / 旧 domain/*。
// ============================================================================
#pragma once

#include <map>
#include <string>
#include <vector>

#include "application_vnext/control/ControlCommand.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace application_vnext::control {

/// 操作生命周期状态（UI 与 UDP 展示、UDP 回包）。
/// 提交后状态流：
///   Queued → Accepted/Rejected → Running → Succeeded/Failed/TimedOut/CommitUncertain
enum class OperationState {
    Queued,           // 已入队，等待唯一 tick 仲裁
    Accepted,         // 仲裁通过，已提交给 PLC（写入结果未知前）
    Rejected,         // 仲裁拒绝（资源占用 / 锁定 / 非法）
    Running,          // PLC 已确认进入执行（motionState / AckSeq 反馈）
    Succeeded,        // 完成（PLC 空闲 / 到位校验通过）
    Failed,           // 失败（限位 / 报警 / 超时等）
    Cancelled,        // 被急停 / Stop / 更高优先级取消
    TimedOut,         // TTL 超时未被执行（仅普通运动启动命令）
    CommitUncertain,  // 已写入但结果未知（通讯不确定，等待观察）
};

enum class OperationKind {
    Positioning,    // Abs / Rel
    Jog,            // 点动
    GantryLifecycle,// 建立 / 解除 / 掉电
};

/// 控制资源（租约键）：轴资源 或 龙门组资源。
/// 抽象为统一资源串，便于结构性互斥（资源集合重叠 → 天然互斥，不依赖 if/else）：
///   axis:A:X1 / axis:A:X2 / axis:A:X      —— 轴资源（按功能角色）
///   gantry:A:0                            —— 龙门组资源（建立/解除/逻辑轴运动均申请）
struct ControlResource {
    std::string key;                       // "axis:A:X" / "gantry:A:0"

    /// 组字母（A / B），与 PlcGroupIndex 0..1 对应。
    static std::string groupLetter(plc_vnext::contracts::PlcGroupIndex g) {
        return (g.value() == 0) ? "A" : "B";
    }

    /// 从 AxisTarget 构造轴资源键（按功能角色命名，如 "axis:A:X"）。
    static ControlResource ofAxis(const AxisTarget& t) {
        return {"axis:" + groupLetter(t.group) + ":"
                + std::string(domain_vnext::model::axisFunctionName(t.function))};
    }

    /// 从组号构造龙门组资源键（如 "gantry:A:0"）。
    static ControlResource ofGantry(plc_vnext::contracts::PlcGroupIndex g) {
        return {"gantry:" + groupLetter(g) + ":" + std::to_string(g.value())};
    }

    bool operator==(const ControlResource&) const = default;
    bool operator<(const ControlResource& o) const { return key < o.key; }
};

/// 一个操作的运行租约：**一个操作可占多个资源**（决定占用者与能否抢占）。
struct OperationLease {
    std::string operationId;
    ControlSource owner = ControlSource::Ui;
    std::vector<ControlResource> resources;   // 操作占用的全部资源（可多个）
    AxisTarget target;                        // 仅用于展示 / 回包
    OperationKind kind = OperationKind::Positioning;
};

/// 资源索引：resource.key -> 持有该资源的 operationId。
/// 仲裁据此判断占用/抢占；龙门与成员轴的互斥由「资源集合重叠」天然保证。
using ResourceIndex = std::map<std::string, std::string>;

/// 由「命令 + 拓扑」计算该操作需要的全部资源集合（供仲裁申请租约）。
///
/// 规则（§7.1 / §7.2）：
///   - 急停 / 释放急停：不占资源（空集合）；
///   - StopMotion / StopJog：**不申请新租约**（空集合）——它们定位已有会话并调用
///     requestStop()，即使目标资源已被该会话占用也必须可执行；不能因资源被占而被拒；
///   - 龙门建立 / 解除：占完整资源集合 {gantry:A:0, axis:A:X, axis:A:X1, axis:A:X2}，
///     与龙门期间成员轴单轴命令**结构性互斥**（资源集合重叠，而非特殊 if/else）；
///   - 逻辑轴 X 运动：同龙门生命周期，占完整集合；
///   - 其他单轴命令：只占一个轴资源。
///
/// 参数 `topo` 目前用于组号校验与后续槽位解析（Phase 1+ 用 topology 解析实际
/// slot 时启用）；Phase 0 的验收按功能角色命名资源键，故暂以 `(void)topo` 标记。
inline std::vector<ControlResource> requiredResources(
    const ControlCommand& cmd,
    const plc_vnext::contracts::TopologySnapshot& topo) {
    (void)topo;  // Phase 0 按功能角色命名；后续阶段据此解析实际 slot
    switch (cmd.action) {
        case ControlAction::EmergencyStop:
        case ControlAction::ReleaseEmergencyStop:
        case ControlAction::StopMotion:
        case ControlAction::StopJog:
            // 急停 / 释放 / Stop 不占新租约、不受 TTL 丢弃。
            return {};

        case ControlAction::GantryEnableAndCouple:
        case ControlAction::GantryDecoupleAndDisable:
        case ControlAction::StartAbsMove:
        case ControlAction::StartRelMove:
        case ControlAction::StartJogForward:
        case ControlAction::StartJogBackward:
            if (cmd.target.function == domain_vnext::model::AxisFunction::X
                || cmd.action == ControlAction::GantryEnableAndCouple
                || cmd.action == ControlAction::GantryDecoupleAndDisable) {
                // 逻辑轴运动 / 龙门生命周期：占龙门组 + 逻辑轴 + 两个成员轴
                // （结构性互斥：期间排斥同组其他生命周期 / 逻辑操作及 X1/X2 单轴命令）。
                return {
                    ControlResource::ofGantry(cmd.target.group),
                    ControlResource::ofAxis({cmd.target.group, domain_vnext::model::AxisFunction::X}),
                    ControlResource::ofAxis({cmd.target.group, domain_vnext::model::AxisFunction::X1}),
                    ControlResource::ofAxis({cmd.target.group, domain_vnext::model::AxisFunction::X2}),
                };
            }
            return {ControlResource::ofAxis(cmd.target)};

        default:
            // Set* / Enable* 等：以目标轴为资源。
            return {ControlResource::ofAxis(cmd.target)};
    }
}

}  // namespace application_vnext::control
