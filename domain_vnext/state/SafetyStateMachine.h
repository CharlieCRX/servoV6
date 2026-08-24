// ============================================================================
// SafetyStateMachine.h —— P2 state: 全局急停五态状态机
// ============================================================================
// 保留旧 EmergencyStopController 的五态语义（设计稿 §4.4），实现重写并归入
// domain_vnext。映射：EStopCommand{true} -> 写 M224 ON（锁存保持）；
//                     {false} -> 写 M225 上升沿解除（PLC 自复位并清 M224）。
// 反馈：M224 ON 即视为已急停；isSystemLocked() 拦截全部轴控制，但遥测仍可读。
// 纯领域：只依赖自身 + model/SafetyState.h。
// ============================================================================
#pragma once

#include <optional>
#include <string>

#include "domain_vnext/model/SafetyState.h"
#include "infrastructure/logger/Logger.h"

namespace domain_vnext::state {

/// 安全域操作拒绝原因（领域独立枚举，语义对齐旧 domain/safety/SafetyRejection.h）。
enum class SafetyRejection {
    None,                       // 无拒绝（操作允许）
    SystemSafetyLocked,         // 系统处于锁定态
    AlreadyInState,             // 幂等保护：请求状态 = 当前状态
    InvalidStateTransition,     // 非法状态跃迁
    NotSynchronized,            // 尚未同步 PLC 真实安全状态，拒绝所有请求
    NotEmergencyStopped,        // 尝试解除急停但系统并未处于急停状态
};

inline const char* safetyRejectionName(SafetyRejection r) {
    switch (r) {
        case SafetyRejection::None: return "None";
        case SafetyRejection::SystemSafetyLocked: return "SystemSafetyLocked";
        case SafetyRejection::AlreadyInState: return "AlreadyInState";
        case SafetyRejection::InvalidStateTransition: return "InvalidStateTransition";
        case SafetyRejection::NotSynchronized: return "NotSynchronized";
        case SafetyRejection::NotEmergencyStopped: return "NotEmergencyStopped";
    }
    return "?";
}

/// 急停命令：active=true 写 M224 锁存保持；false 写 M225 上升沿解除。
struct EStopCommand {
    bool active = false;
};

/// 全局急停五态状态机。
class SafetyStateMachine {
public:
    SafetyStateMachine() = default;

    model::SafetyState state() const { return state_; }

    /// 系统是否处于「锁定」状态（任何运动都不允许）。
    bool isSystemLocked() const {
        return state_ == model::SafetyState::NotSynchronized ||
               state_ == model::SafetyState::EmergencyStopping ||
               state_ == model::SafetyState::EmergencyStopped ||
               state_ == model::SafetyState::ReleasingEmergencyStop;
    }
    bool isEmergencyStopped() const { return state_ == model::SafetyState::EmergencyStopped; }
    bool isTransitioning() const {
        return state_ == model::SafetyState::EmergencyStopping ||
               state_ == model::SafetyState::ReleasingEmergencyStop;
    }
    bool isNotSynchronized() const { return state_ == model::SafetyState::NotSynchronized; }

    // --- 意图生成 ---
    SafetyRejection requestEmergencyStop();
    SafetyRejection requestReleaseEmergencyStop();

    /// 唯一 PLC 反馈入口（M224「设备急停中」状态）；首次调用完成同步。
    void applyFeedback(bool plcEmergencyStopped);

    // --- 命令消费 ---
    bool hasPendingCommand() const { return pending_.has_value(); }
    EStopCommand popPendingCommand();

private:
    model::SafetyState state_ = model::SafetyState::NotSynchronized;
    std::optional<EStopCommand> pending_;
};

inline SafetyRejection SafetyStateMachine::requestEmergencyStop() {
    const auto ctx = LogContext{"System", "Safety", "estop"};
    // 尚未同步 PLC 状态，拒绝所有操作
    if (state_ == model::SafetyState::NotSynchronized) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "SafetyState", ctx,
                               "requestEmergencyStop rejected reason=NotSynchronized");
        return SafetyRejection::NotSynchronized;
    }
    // 幂等：已在急停流程中
    if (state_ == model::SafetyState::EmergencyStopping ||
        state_ == model::SafetyState::EmergencyStopped) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "SafetyState", ctx,
                               std::string("requestEmergencyStop rejected reason=AlreadyInState state=")
                               + model::safetyStateName(state_));
        return SafetyRejection::AlreadyInState;
    }
    // 冲突：正在解除急停，不允许反向操作
    if (state_ == model::SafetyState::ReleasingEmergencyStop) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "SafetyState", ctx,
                               "requestEmergencyStop rejected reason=InvalidStateTransition state=ReleasingEmergencyStop");
        return SafetyRejection::InvalidStateTransition;
    }
    // 通过：Running -> 生成急停意图
    const auto before = state_;
    pending_ = EStopCommand{true};
    state_ = model::SafetyState::EmergencyStopping;
    Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "SafetyState", ctx,
                           std::string("requestEmergencyStop accepted state=")
                           + model::safetyStateName(before)
                           + "->" + model::safetyStateName(state_));
    return SafetyRejection::None;
}

inline SafetyRejection SafetyStateMachine::requestReleaseEmergencyStop() {
    const auto ctx = LogContext{"System", "Safety", "estop"};
    // 尚未同步 PLC 状态，拒绝所有操作
    if (state_ == model::SafetyState::NotSynchronized) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "SafetyState", ctx,
                               "requestReleaseEmergencyStop rejected reason=NotSynchronized");
        return SafetyRejection::NotSynchronized;
    }
    // 前置条件：只有 EmergencyStopped 状态才能解除
    if (state_ != model::SafetyState::EmergencyStopped) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "SafetyState", ctx,
                               std::string("requestReleaseEmergencyStop rejected reason=NotEmergencyStopped state=")
                               + model::safetyStateName(state_));
        return SafetyRejection::NotEmergencyStopped;
    }
    const auto before = state_;
    pending_ = EStopCommand{false};
    state_ = model::SafetyState::ReleasingEmergencyStop;
    Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "SafetyState", ctx,
                           std::string("requestReleaseEmergencyStop accepted state=")
                           + model::safetyStateName(before)
                           + "->" + model::safetyStateName(state_));
    return SafetyRejection::None;
}

inline void SafetyStateMachine::applyFeedback(bool plcEmergencyStopped) {
    const auto before = state_;
    switch (state_) {
        case model::SafetyState::NotSynchronized:
            // 首次同步：PLC Feedback 是唯一真相来源
            state_ = plcEmergencyStopped ? model::SafetyState::EmergencyStopped
                                         : model::SafetyState::Running;
            break;
        case model::SafetyState::EmergencyStopping:
            // 等待 PLC 确认急停完成
            if (plcEmergencyStopped) {
                state_ = model::SafetyState::EmergencyStopped;
            }
            break;
        case model::SafetyState::EmergencyStopped:
            // 安全锁存态：不因 PLC 反馈恢复而自动退出，须走显式解除流程
            break;
        case model::SafetyState::ReleasingEmergencyStop:
            // 等待 PLC 确认急停解除 -> 直接恢复 Running
            if (!plcEmergencyStopped) {
                state_ = model::SafetyState::Running;
            }
            break;
        case model::SafetyState::Running:
            // 物理急停按钮被按下，PLC 直接反馈 true，Controller 永远相信 PLC
            if (plcEmergencyStopped) {
                state_ = model::SafetyState::EmergencyStopped;
            }
            break;
    }
    if (before != state_) {
        Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "SafetyState",
                               LogContext{"System", "Safety", "feedback"},
                               std::string("feedback emergencyStop=")
                               + std::to_string(plcEmergencyStopped)
                               + " state=" + model::safetyStateName(before)
                               + "->" + model::safetyStateName(state_));
    }
}

inline EStopCommand SafetyStateMachine::popPendingCommand() {
    auto cmd = *pending_;
    pending_.reset();
    return cmd;
}

}  // namespace domain_vnext::state
