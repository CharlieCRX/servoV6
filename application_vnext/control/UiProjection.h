// ============================================================================
// UiProjection.h —— Phase 2：统一状态快照 -> UI 只读投影
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 2 / §6.1。
//
// 目标：UI 开始展示统一快照（只读），但仍由旧 Core 发控制，仅用于对照验证
// 投影正确性。本头文件把 ControlStateStore 发布的 ControlStateSnapshot 投影为
// UI 友好的只读视图：
//   - 快照自带 group/role/hmiVisible，UI 无需自行推导轴映射；
//   - locked 派生：!trusted || 全局锁定 || 急停 || 安全不可信 -> 普通轴操作应
//     表现为锁定/不可用（反映仲裁规则 §7.2「全局锁定 → 普通控制 Rejected」）；
//   - motionState / 龙门 state / OperationState 的数值 -> 可读名称。
//
// 纯 C++：不依赖 Qt / Modbus / 旧 domain/* / policy 层。只依赖 control 层
// 自身的 ControlStateStore.h（快照类型）与 OperationState.h。
// ============================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "application_vnext/control/ControlStateStore.h"
#include "application_vnext/control/OperationState.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace application_vnext::control {

/// 单个轴的 UI 只读投影（由 AxisUiState + 全局锁定/安全派生）。
/// 不携带任何可写句柄：Phase 2 UI 严格只读，不提交命令。
struct AxisUiView {
    int16_t slot = 0;                 // PLC 槽位下标
    std::string groupLetter;          // "A" / "B"（快照自带，UI 无需推导）
    std::string roleName;             // "X" / "X1" / "X2" / "Y" / "Z" / "R"
    std::string displayName;          // "A.Y" / "B.X1" ...
    bool hmiVisible = false;          // 拓扑是否允许 UI 显示
    bool bound = false;               // 该功能是否注册为轴实体
    bool trusted = false;             // 本次反馈是否可信
    bool locked = true;               // 普通轴操作是否应被锁定（不可用）
    float absPosition = 0.0f;
    float relPosition = 0.0f;
    float manualSpeed = 0.0f;
    float positioningSpeed = 0.0f;
    float absMoveTarget = 0.0f;       // 最近一次 SetAbsTarget 预填目标（§5.3）
    float relMoveTarget = 0.0f;       // 最近一次 SetRelTarget 预填距离（§5.3）
    int16_t motionState = 0;          // D128 原样
    std::string motionStateName;      // 可读名
    int16_t motionLimit = 0;          // D144 原样（0=无 1=正软 2=负软 3=正硬 4=负硬）
    std::string motionLimitName;      // 限位可读名
    uint16_t alarmWord = 0;

    // ---- 软限位配置（参数区，与运行反馈同帧读取）----
    float softNegLimit = 0.0f;        // 软件负限位（EU）
    float softPosLimit = 0.0f;        // 软件正限位（EU）
    uint16_t softLimitControl = 0;    // 控制字 bit0正 bit1负
    bool softLimitTrusted = false;    // 参数区是否可信
    bool leased = false;              // 是否被某操作占用
    std::string leaseOwnerName;       // "UI"/"Joystick"/"UDP"/"Maintenance"
};

/// 单个龙门组的 UI 只读投影。
struct GantryUiView {
    std::string groupLetter;
    bool trusted = false;
    int16_t state = 0;                // 0未配置 1已解除 2建立中 3已联动 4解除中 5故障
    std::string stateName;
    int16_t internalStep = 0;
    int16_t commandResult = 0;
    int16_t commandErrorCode = 0;
    bool logicalControlAllowed = false;
    bool memberControlAllowed = false;
    bool readyToCouple = false;
    bool readyToDecouple = false;
    bool x1InGear = false;
    bool x2InGear = false;
    float logicalPosition = 0.0f;
    float skew = 0.0f;
    bool fault = false;
    int16_t faultCode = 0;
    bool lifecycleLeased = false;
};

/// 单次操作的 UI 只读投影。
struct OperationView {
    std::string operationId;
    std::string sourceName;           // "UI"/"Joystick"/"UDP"/"Maintenance"
    std::string axis;                 // "A.Y" ...
    std::string kindName;             // "Positioning"/"Jog"/"GantryLifecycle"
    OperationState state = OperationState::Queued;
    std::string stateName;            // "已排队"/"等待 PLC"/...（中文可读名）
    std::string diag;
    int16_t motionState = 0;
    float position = 0.0f;
};

/// 一次完整投影结果（connection / safety / 全局锁定 + 轴/龙门/操作）。
struct ProjectedUiState {
    bool connected = false;
    std::string connectionDiagnostic;
    bool emergencyStop = false;
    bool safetyTrusted = false;
    bool globallyLocked = true;
    std::vector<AxisUiView> axes;
    std::vector<GantryUiView> gantries;
    std::vector<OperationView> operations;
};

/// 纯函数投影入口（无状态，可安全并发；由 Qt 适配器在 GUI 线程调用）。
struct UiProjection {
    /// 由快照 + 全局锁定投影整份 UI 状态。
    static ProjectedUiState project(const ControlStateSnapshot& snap, bool globallyLocked);

    /// 单轴投影；locked = !trusted || globallyLocked || emergencyStop || !safetyTrusted。
    static AxisUiView projectAxis(const AxisUiState& a,
                                  bool globallyLocked, bool emergencyStop,
                                  bool safetyTrusted);

    /// 单龙门组投影（GantryUiState 按组索引存放，需显式传入组号以推导字母）。
    static GantryUiView projectGantry(const GantryUiState& g,
                                       plc_vnext::contracts::PlcGroupIndex groupIndex);

    /// 单操作条目投影。
    static OperationView projectOperation(const OperationEntry& op);

    /// 组号 -> 字母（A/B）。
    static std::string groupLetter(plc_vnext::contracts::PlcGroupIndex g);

    /// D128 motionState（0..6）可读名（复制语义，不依赖 policy 层）。
    static std::string motionStateName(int16_t s);

    /// D144 motionLimit（0..4）可读名（无限位/正软/负软/正硬/负硬）。
    static std::string motionLimitName(int16_t v);

    /// 龙门 state（0..5）可读名。
    static std::string gantryStateName(int16_t state);

    /// OperationState 可读名。
    static std::string operationStateName(OperationState s);

    /// OperationKind 可读名。
    static std::string operationKindName(OperationKind k);
};

// ============================================================================
// 实现（inline）
// ============================================================================

inline std::string UiProjection::groupLetter(plc_vnext::contracts::PlcGroupIndex g) {
    return (g.value() == 0) ? "A" : "B";
}

inline std::string UiProjection::motionStateName(int16_t s) {
    switch (s) {
        case 0:  return "NotEnabled(0)";
        case 1:  return "EnabledMotorOff(1)";
        case 2:  return "MotorIdle(2)";
        case 3:  return "JogForward(3)";
        case 4:  return "JogBackward(4)";
        case 5:  return "AbsMove(5)";
        case 6:  return "RelMove(6)";
        default: return "?";
    }
}

inline std::string UiProjection::motionLimitName(int16_t v) {
    switch (v) {
        case 0:  return "无限位";
        case 1:  return "正软限位";
        case 2:  return "负软限位";
        case 3:  return "正硬限位";
        case 4:  return "负硬限位";
        default: return "未知限位";
    }
}

inline std::string UiProjection::gantryStateName(int16_t state) {
    switch (state) {
        case 0:  return "未配置";
        case 1:  return "已解除";
        case 2:  return "建立中";
        case 3:  return "已联动";
        case 4:  return "解除中";
        case 5:  return "故障";
        default: return "?";
    }
}

inline std::string UiProjection::operationStateName(OperationState s) {
    switch (s) {
        case OperationState::Queued:           return "已排队";
        case OperationState::Accepted:         return "等待 PLC";
        case OperationState::Rejected:         return "已拒绝";
        case OperationState::Running:          return "执行中";
        case OperationState::Succeeded:        return "成功";
        case OperationState::Failed:           return "失败";
        case OperationState::Cancelled:        return "已取消";
        case OperationState::TimedOut:         return "超时";
        case OperationState::CommitUncertain:  return "结果未定";
    }
    return "?";
}

inline std::string UiProjection::operationKindName(OperationKind k) {
    switch (k) {
        case OperationKind::Positioning:    return "Positioning";
        case OperationKind::Jog:            return "Jog";
        case OperationKind::GantryLifecycle:return "GantryLifecycle";
    }
    return "?";
}

inline AxisUiView UiProjection::projectAxis(const AxisUiState& a,
                                            bool globallyLocked,
                                            bool emergencyStop,
                                            bool safetyTrusted) {
    AxisUiView v;
    v.slot = a.slot;
    v.groupLetter = groupLetter(a.group);
    v.roleName = std::string(domain_vnext::model::axisFunctionName(a.role));
    v.displayName = v.groupLetter + "." + v.roleName;
    v.hmiVisible = a.hmiVisible;
    v.bound = a.bound;
    v.trusted = a.trusted;
    // 普通轴操作是否可用：反馈可信 + 未全局锁定 + 未急停 + 安全状态可信。
    v.locked = !a.trusted || globallyLocked || emergencyStop || !safetyTrusted;
    v.absPosition = a.absPosition;
    v.relPosition = a.relPosition;
    v.manualSpeed = a.manualSpeed;
    v.positioningSpeed = a.positioningSpeed;
    v.absMoveTarget = a.absMoveTarget;
    v.relMoveTarget = a.relMoveTarget;
    v.motionState = a.motionState;
    v.motionStateName = motionStateName(a.motionState);
    v.motionLimit = a.motionLimit;
    v.motionLimitName = motionLimitName(a.motionLimit);
    v.alarmWord = a.alarmWord;
    v.softNegLimit = a.softNegLimit;
    v.softPosLimit = a.softPosLimit;
    v.softLimitControl = a.softLimitControl;
    v.softLimitTrusted = a.softLimitTrusted;
    v.leased = a.leased;
    v.leaseOwnerName = a.leaseOwnerName;
    return v;
}

inline GantryUiView UiProjection::projectGantry(const GantryUiState& g,
                                                plc_vnext::contracts::PlcGroupIndex groupIndex) {
    GantryUiView v;
    v.groupLetter = groupLetter(groupIndex);
    v.trusted = g.trusted;
    v.state = g.state;
    v.stateName = gantryStateName(g.state);
    v.internalStep = g.internalStep;
    v.commandResult = g.commandResult;
    v.commandErrorCode = g.commandErrorCode;
    v.logicalControlAllowed = g.logicalControlAllowed;
    v.memberControlAllowed = g.memberControlAllowed;
    v.readyToCouple = g.readyToCouple;
    v.readyToDecouple = g.readyToDecouple;
    v.x1InGear = g.x1InGear;
    v.x2InGear = g.x2InGear;
    v.logicalPosition = g.logicalPosition;
    v.skew = g.skew;
    v.fault = g.fault;
    v.faultCode = g.faultCode;
    v.lifecycleLeased = g.lifecycleLeased;
    return v;
}

inline OperationView UiProjection::projectOperation(const OperationEntry& op) {
    OperationView v;
    v.operationId = op.operationId;
    v.sourceName = controlSourceName(op.source);
    v.axis = op.axis;
    v.kindName = operationKindName(op.kind);
    v.state = op.state;
    v.stateName = operationStateName(op.state);
    v.diag = op.diag;
    v.motionState = op.motionState;
    v.position = op.position;
    return v;
}

inline ProjectedUiState UiProjection::project(const ControlStateSnapshot& snap,
                                              bool globallyLocked) {
    ProjectedUiState out;
    out.connected = snap.connection.connected;
    out.connectionDiagnostic = snap.connection.diagnostic;
    out.emergencyStop = snap.safety.emergencyStop;
    out.safetyTrusted = snap.safety.trusted;
    out.globallyLocked = globallyLocked;

    out.axes.reserve(snap.axes.size());
    for (const auto& a : snap.axes) {
        out.axes.push_back(projectAxis(a, globallyLocked, out.emergencyStop,
                                       out.safetyTrusted));
    }
    out.gantries.reserve(snap.gantries.size());
    for (std::size_t i = 0; i < snap.gantries.size(); ++i) {
        out.gantries.push_back(projectGantry(
            snap.gantries[i],
            plc_vnext::contracts::PlcGroupIndex(static_cast<std::size_t>(i))));
    }
    out.operations.reserve(snap.operations.size());
    for (const auto& op : snap.operations) {
        out.operations.push_back(projectOperation(op));
    }
    return out;
}

}  // namespace application_vnext::control
