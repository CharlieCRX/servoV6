// ============================================================================
// GantryCouplingStateMachine.h —— P2 state: 龙门联动状态机
// ============================================================================
// 设计稿 §4.5：从 GantryStatus（D190 + 18g）映射领域联动状态，并产出带 RequestSeq
// 的 GantryRequest{Couple|Decouple|Reset} 事务。requestSeq 由本状态机自增（会话级），
// 交给 gateway.submitGantryRequestDetailed()（P4）写 D180..D182。
// 事务闭环采用 2026-08-12 地址表 §10 多条件判定（替换旧单条件 ackSeq==seq && result==2）：
//   建立成功：AckSeq==seq && CommandResult==2 && State==3 && X1InGear && X2InGear
//             && LogicalControlAllowed==TRUE；
//   解除成功：AckSeq==seq && CommandResult==2 && State==1 && !X1InGear && !X2InGear
//             && MemberControlAllowed==TRUE。
// 准入（§10）：建立需 ConfigValid==TRUE && State==1 && ReadyToCouple==TRUE && !Fault；
//             解除基于 readyToDecouple / fault；复位用于故障处理。
// 纯领域：只依赖自身 + model + plc_vnext::contracts（GantryRequest 纯 DTO）。
// ============================================================================
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "domain_vnext/logging/DomainLogger.h"
#include "domain_vnext/model/GantryParam.h"
#include "domain_vnext/model/GantryStatus.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"

namespace domain_vnext::state {

using model::GantryCouplingState;
using model::gantryCouplingStateFromRaw;

/// 龙门联动状态机。
class GantryCouplingStateMachine {
public:
    enum class RequestResult {
        Accepted,             // 已产出请求
        RejectedUnconfigured, // 尚未同步，拒绝操作
        RejectedFault,        // 处于故障态，应先复位
        RejectedStateConflict,// 内部状态机冲突（已有进行中事务 / 反向进行中）
        RejectedNotReady,     // 准入条件不满足（ConfigValid / Ready / Fault）
        RejectedNotDecoupled, // 未处于联动态，无法解除
    };

    GantryCouplingState state() const { return state_; }
    bool isConfigured() const { return state_ != GantryCouplingState::Unconfigured; }
    bool hasOpenTransaction() const { return lastCommand_ != 0; }

    void setLogGroup(std::string group) { logGroup_ = std::move(group); }

    /// 注入龙门配置（D1600 参数区，只读）：提供 ConfigValid 准入来源。
    void applyConfig(const model::GantryParamModel& config) {
        if (!configSeen_ || configValid_ != config.valid) {
            Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "GantryState",
                                   LogContext{logGroup_, "Gantry", "config"},
                                   "configValid=" + std::to_string(config.valid));
            configSeen_ = true;
        }
        configValid_ = config.valid;
    }

    /// 唯一运行反馈入口（D190 + 18g），含建 / 解事务多条件闭环。
    void applyFeedback(const model::GantryStatusModel& status);

    // --- 意图生成（产出 GantryRequest + 自增 RequestSeq）---
    RequestResult requestCouple();
    RequestResult requestDecouple();
    RequestResult requestReset();

    // --- 请求消费 ---
    bool hasPendingRequest() const { return pending_.has_value(); }
    plc_vnext::contracts::GantryRequest popPendingRequest();

    /// 最近一次请求的 RequestSeq（供上层以 AckSeq 闭环确认本次提交）。
    int32_t lastRequestSeq() const { return lastRequestSeq_; }

private:
    void nextSeq() {
        ++requestSeq_;
        lastRequestSeq_ = requestSeq_;
    }

    model::GantryCouplingState state_ = model::GantryCouplingState::Unconfigured;
    bool configValid_ = false;
    bool readyToCouple_ = false;
    bool readyToDecouple_ = false;
    bool fault_ = false;

    int32_t requestSeq_ = 0;
    int32_t lastRequestSeq_ = 0;
    int32_t lastCommand_ = 0;  // 0=无事务；1=Couple；2=Decouple（对齐 GantryCommandKind）
    std::optional<plc_vnext::contracts::GantryRequest> pending_;
    std::string logGroup_ = "DOMAIN";
    bool configSeen_ = false;
};

inline void GantryCouplingStateMachine::applyFeedback(const model::GantryStatusModel& s) {
    const auto before = state_;
    const int32_t beforeCommand = lastCommand_;
    fault_ = s.fault;
    readyToCouple_ = s.readyToCouple;
    readyToDecouple_ = s.readyToDecouple;
    const auto raw = gantryCouplingStateFromRaw(s.rawState);

    // 无进行中事务：直接反映物理真相（含首次同步）。
    if (lastCommand_ == 0) {
        state_ = raw;
        return;
    }

    if (lastCommand_ == 1) {  // 建立事务
        const bool closed =
            s.ackSeq == lastRequestSeq_ && s.commandResult == 2 &&
            raw == GantryCouplingState::Coupled && s.x1InGear && s.x2InGear &&
            s.logicalControlAllowed;
        if (closed) {
            state_ = GantryCouplingState::Coupled;
            lastCommand_ = 0;
            Logger::logWithContext(
                LogLevel::INFO, LogLayer::DOM, "GantryState",
                LogContext{logGroup_, "Gantry", "feedback"},
                "couple transaction closed seq=" + std::to_string(lastRequestSeq_)
                + " ackSeq=" + std::to_string(s.ackSeq)
                + " commandResult=" + std::to_string(s.commandResult));
        } else if (raw == GantryCouplingState::CouplingRequested) {
            state_ = GantryCouplingState::CouplingRequested;  // 中间帧，继续等待
        } else {
            state_ = GantryCouplingState::Decoupled;  // PLC 拒绝 / 回退
            lastCommand_ = 0;
            Logger::logWithContext(
                LogLevel::WARN, LogLayer::DOM, "GantryState",
                LogContext{logGroup_, "Gantry", "feedback"},
                "couple transaction rolled back seq=" + std::to_string(lastRequestSeq_)
                + " raw=" + model::gantryCouplingStateName(raw)
                + " ackSeq=" + std::to_string(s.ackSeq)
                + " commandResult=" + std::to_string(s.commandResult)
                + " errorCode=" + std::to_string(s.commandErrorCode));
        }
    } else {  // 解除事务
        const bool closed =
            s.ackSeq == lastRequestSeq_ && s.commandResult == 2 &&
            raw == GantryCouplingState::Decoupled && !s.x1InGear && !s.x2InGear &&
            s.memberControlAllowed;
        if (closed) {
            state_ = GantryCouplingState::Decoupled;
            lastCommand_ = 0;
            Logger::logWithContext(
                LogLevel::INFO, LogLayer::DOM, "GantryState",
                LogContext{logGroup_, "Gantry", "feedback"},
                "decouple transaction closed seq=" + std::to_string(lastRequestSeq_)
                + " ackSeq=" + std::to_string(s.ackSeq)
                + " commandResult=" + std::to_string(s.commandResult));
        } else if (raw == GantryCouplingState::DecouplingRequested) {
            state_ = GantryCouplingState::DecouplingRequested;  // 中间帧，继续等待
        } else {
            state_ = GantryCouplingState::Coupled;  // PLC 拒绝 / 回退
            lastCommand_ = 0;
            Logger::logWithContext(
                LogLevel::WARN, LogLayer::DOM, "GantryState",
                LogContext{logGroup_, "Gantry", "feedback"},
                "decouple transaction rolled back seq=" + std::to_string(lastRequestSeq_)
                + " raw=" + model::gantryCouplingStateName(raw)
                + " ackSeq=" + std::to_string(s.ackSeq)
                + " commandResult=" + std::to_string(s.commandResult)
                + " errorCode=" + std::to_string(s.commandErrorCode));
        }
    }
    if (before != state_ || beforeCommand != lastCommand_) {
        Logger::logWithContext(
            LogLevel::INFO, LogLayer::DOM, "GantryState",
            LogContext{logGroup_, "Gantry", "feedback"},
            std::string("state=") + model::gantryCouplingStateName(before)
            + "->" + model::gantryCouplingStateName(state_)
            + " raw=" + model::gantryCouplingStateName(raw)
            + " ackSeq=" + std::to_string(s.ackSeq)
            + " commandResult=" + std::to_string(s.commandResult)
            + " readyCouple=" + std::to_string(s.readyToCouple)
            + " readyDecouple=" + std::to_string(s.readyToDecouple)
            + " logicalAllowed=" + std::to_string(s.logicalControlAllowed)
            + " memberAllowed=" + std::to_string(s.memberControlAllowed)
            + " fault=" + std::to_string(s.fault)
            + " faultCode=" + std::to_string(s.faultCode));
    }
}

inline GantryCouplingStateMachine::RequestResult
GantryCouplingStateMachine::requestCouple() {
    if (state_ == GantryCouplingState::Unconfigured) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "couple"},
                               "requestCouple rejected reason=Unconfigured");
        return RequestResult::RejectedUnconfigured;
    }
    if (state_ == GantryCouplingState::Fault) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "couple"},
                               "requestCouple rejected reason=Fault");
        return RequestResult::RejectedFault;
    }
    if (state_ == GantryCouplingState::Coupled ||
        state_ == GantryCouplingState::CouplingRequested ||
        state_ == GantryCouplingState::DecouplingRequested) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "couple"},
                               std::string("requestCouple rejected reason=StateConflict state=")
                               + model::gantryCouplingStateName(state_));
        return RequestResult::RejectedStateConflict;
    }
    // 准入：ConfigValid && State==Decoupled && ReadyToCouple && !Fault
    if (!configValid_ || !readyToCouple_ || fault_) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "couple"},
                               "requestCouple rejected reason=NotReady configValid="
                               + std::to_string(configValid_)
                               + " readyToCouple=" + std::to_string(readyToCouple_)
                               + " fault=" + std::to_string(fault_));
        return RequestResult::RejectedNotReady;
    }
    nextSeq();
    lastCommand_ = 1;
    pending_ = plc_vnext::contracts::GantryRequest::couple(lastRequestSeq_);
    state_ = GantryCouplingState::CouplingRequested;
    Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "GantryState",
                           LogContext{logGroup_, "Gantry", "couple"},
                           "requestCouple accepted requestSeq="
                           + std::to_string(lastRequestSeq_));
    return RequestResult::Accepted;
}

inline GantryCouplingStateMachine::RequestResult
GantryCouplingStateMachine::requestDecouple() {
    if (state_ == GantryCouplingState::Unconfigured) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "decouple"},
                               "requestDecouple rejected reason=Unconfigured");
        return RequestResult::RejectedUnconfigured;
    }
    if (state_ == GantryCouplingState::Fault) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "decouple"},
                               "requestDecouple rejected reason=Fault");
        return RequestResult::RejectedFault;
    }
    if (state_ == GantryCouplingState::Decoupled) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "decouple"},
                               "requestDecouple rejected reason=NotDecoupled");
        return RequestResult::RejectedNotDecoupled;
    }
    if (state_ == GantryCouplingState::CouplingRequested ||
        state_ == GantryCouplingState::DecouplingRequested) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "decouple"},
                               std::string("requestDecouple rejected reason=StateConflict state=")
                               + model::gantryCouplingStateName(state_));
        return RequestResult::RejectedStateConflict;
    }
    // 准入：基于 ReadyToDecouple / Fault 判定
    if (!readyToDecouple_ || fault_) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "decouple"},
                               "requestDecouple rejected reason=NotReady readyToDecouple="
                               + std::to_string(readyToDecouple_)
                               + " fault=" + std::to_string(fault_));
        return RequestResult::RejectedNotReady;
    }
    nextSeq();
    lastCommand_ = 2;
    pending_ = plc_vnext::contracts::GantryRequest::decouple(lastRequestSeq_);
    state_ = GantryCouplingState::DecouplingRequested;
    Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "GantryState",
                           LogContext{logGroup_, "Gantry", "decouple"},
                           "requestDecouple accepted requestSeq="
                           + std::to_string(lastRequestSeq_));
    return RequestResult::Accepted;
}

inline GantryCouplingStateMachine::RequestResult
GantryCouplingStateMachine::requestReset() {
    if (state_ == GantryCouplingState::Unconfigured) {
        Logger::logWithContext(LogLevel::WARN, LogLayer::DOM, "GantryState",
                               LogContext{logGroup_, "Gantry", "reset"},
                               "requestReset rejected reason=Unconfigured");
        return RequestResult::RejectedUnconfigured;
    }
    nextSeq();
    // 复位为瞬时动作，不建立事务闭环（PLC 先停 SYN0、再依次 GearOut）
    lastCommand_ = 0;
    pending_ = plc_vnext::contracts::GantryRequest::reset(lastRequestSeq_);
    Logger::logWithContext(LogLevel::INFO, LogLayer::DOM, "GantryState",
                           LogContext{logGroup_, "Gantry", "reset"},
                           "requestReset accepted requestSeq="
                           + std::to_string(lastRequestSeq_));
    return RequestResult::Accepted;
}

inline plc_vnext::contracts::GantryRequest
GantryCouplingStateMachine::popPendingRequest() {
    auto r = *pending_;
    pending_.reset();
    return r;
}

}  // namespace domain_vnext::state
