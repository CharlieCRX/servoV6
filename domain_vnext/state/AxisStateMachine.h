// ============================================================================
// AxisStateMachine.h —— P2 state: 单轴意图校验状态机
// ============================================================================
// 单轴链路：意图 -> 校验 -> 命令入 CommandOutbox（设计稿 §3 state/AxisStateMachine.h）。
//   - 运动类意图（定位触发 / 点动启动）受系统急停锁定、龙门同步与轴忙状态约束；
//   - 普通参数写与停止类命令始终放行（保证 SetAbsDistance/TriggerAbsMove 等四接口
//     独立解耦，见设计稿 §4.2「解耦铁律」：写入失败不得继续触发）。
// 纯领域：只依赖自身 + model + state/CommandOutbox.h。
// ============================================================================
#pragma once

#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/model/GantryStatus.h"
#include "domain_vnext/state/CommandOutbox.h"

namespace domain_vnext::state {

/// 单轴意图校验状态机。
class AxisStateMachine {
public:
    enum class SubmitResult {
        Accepted,              // 校验通过，命令已入 Outbox
        RejectedSystemLocked,  // 系统急停锁定，禁止新的运动意图
        RejectedGantryLocked,  // 龙门未同步，禁止龙门相关运动意图
        RejectedAxisBusy,      // 轴正在运动中，禁止新的定位触发 / 点动启动
    };

    /// @param requiresGantrySync 该轴运动是否依赖龙门同步（X1/X2/X 逻辑轴为 true，
    ///                           Y/Z/R 独立轴为 false；P3 由 SystemBoot 按角色注入）。
    explicit AxisStateMachine(bool requiresGantrySync = false)
        : requiresGantrySync_(requiresGantrySync) {}

    /// 意图入口：校验通过则写入 outbox（并同步更新本地忙状态）。
    SubmitResult submit(const model::AxisCommand& intent, CommandOutbox& outbox);

    // --- 外部注入（P4 由 FeedbackDispatcher 接入）---
    void setSystemLocked(bool locked) { systemLocked_ = locked; }
    void setGantryState(model::GantryCouplingState coupling) { gantry_ = coupling; }

    bool isBusy() const { return busy_; }

private:
    static bool isNewMotion(const model::AxisCommand& cmd);
    void applyBusy(const model::AxisCommand& cmd);

    bool requiresGantrySync_;
    bool systemLocked_ = false;
    bool busy_ = false;
    model::GantryCouplingState gantry_ = model::GantryCouplingState::Unconfigured;
};

inline bool AxisStateMachine::isNewMotion(const model::AxisCommand& cmd) {
    switch (cmd.kind) {
        case model::AxisCommandKind::TriggerAbsMove:
        case model::AxisCommandKind::TriggerRelMove:
            return true;
        case model::AxisCommandKind::JogForward:
        case model::AxisCommandKind::JogBackward:
            return cmd.level;  // 点动 ON 才视为「启动运动」
        default:
            return false;
    }
}

inline void AxisStateMachine::applyBusy(const model::AxisCommand& cmd) {
    switch (cmd.kind) {
        case model::AxisCommandKind::TriggerAbsMove:
        case model::AxisCommandKind::TriggerRelMove:
            busy_ = true;
            break;
        case model::AxisCommandKind::StopAbsMove:
        case model::AxisCommandKind::StopRelMove:
            busy_ = false;
            break;
        case model::AxisCommandKind::JogForward:
        case model::AxisCommandKind::JogBackward:
            busy_ = cmd.level;  // ON -> 忙；OFF -> 停止
            break;
        default:
            break;
    }
}

inline AxisStateMachine::SubmitResult AxisStateMachine::submit(
    const model::AxisCommand& intent, CommandOutbox& outbox) {
    if (isNewMotion(intent)) {
        if (systemLocked_) {
            return SubmitResult::RejectedSystemLocked;
        }
        if (requiresGantrySync_ && gantry_ == model::GantryCouplingState::Unconfigured) {
            return SubmitResult::RejectedGantryLocked;
        }
        if (busy_) {
            return SubmitResult::RejectedAxisBusy;
        }
    }
    applyBusy(intent);
    outbox.push(intent);
    return SubmitResult::Accepted;
}

}  // namespace domain_vnext::state
