// ============================================================================
// GantryStatus.h —— P1 model: 龙门联动状态领域映射
// ============================================================================
// 从 plc_vnext::contracts::GantryStatusSnapshot 映射为领域状态（§4.5）。
//   - GantryCouplingState 由 raw state 映射（状态映射表见下方注释）。
//   - GantryStatusModel 为领域运行状态 DTO，含建/解联动事务闭环所需字段。
// 纯 DTO：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <cstdint>

namespace domain_vnext::model {

/// 龙门领域联动状态。
enum class GantryCouplingState {
    Unconfigured,        // 0 未配置
    Decoupled,           // 1 已解除
    CouplingRequested,   // 2 建立中
    Coupled,             // 3 已联动
    DecouplingRequested, // 4 解除中
    Fault,               // 5 故障
};

/// raw state -> 领域状态（§4.5 状态映射表）。
///   0 -> Unconfigured | 1 -> Decoupled | 2 -> CouplingRequested
///   3 -> Coupled      | 4 -> DecouplingRequested | 5 -> Fault
inline GantryCouplingState gantryCouplingStateFromRaw(std::int16_t raw) {
    switch (raw) {
        case 1: return GantryCouplingState::Decoupled;
        case 2: return GantryCouplingState::CouplingRequested;
        case 3: return GantryCouplingState::Coupled;
        case 4: return GantryCouplingState::DecouplingRequested;
        case 5: return GantryCouplingState::Fault;
        case 0:
        default: return GantryCouplingState::Unconfigured;
    }
}

/// 可读名称（用于日志/诊断）。
inline const char* gantryCouplingStateName(GantryCouplingState s) {
    switch (s) {
        case GantryCouplingState::Unconfigured:        return "Unconfigured";
        case GantryCouplingState::Decoupled:           return "Decoupled";
        case GantryCouplingState::CouplingRequested:   return "CouplingRequested";
        case GantryCouplingState::Coupled:             return "Coupled";
        case GantryCouplingState::DecouplingRequested: return "DecouplingRequested";
        case GantryCouplingState::Fault:               return "Fault";
    }
    return "?";
}

/// 龙门运行状态领域模型（D190 + 18g -> 领域联动状态机映射，§4.5）。
struct GantryStatusModel {
    GantryCouplingState coupling = GantryCouplingState::Unconfigured;
    int16_t rawState = 0;            // 0未配置 1已解除 2建立中 3已联动 4解除中 5故障
    int32_t ackSeq = 0;              // 与 RequestSeq 对齐判定
    int16_t commandResult = 0;       // 0/1/2/3/4
    int16_t commandErrorCode = 0;
    bool readyToCouple = false;      // +6.bit0
    bool readyToDecouple = false;    // +6.bit1（2026-08-12 新增，解除准入用）
    bool memberControlAllowed = false;   // +6.bit2
    bool logicalControlAllowed = false;  // +6.bit3
    bool x1InGear = false;           // +6.bit4
    bool x2InGear = false;           // +6.bit5
    float x1Position = 0.f;
    float x2Position = 0.f;
    float logicalPosition = 0.f;
    float skew = 0.f;                // X1-X2，mm
    bool fault = false;              // +15.bit0
    int16_t faultCode = 0;           // +16
    /// 本次读取是否可信。
    bool trusted = false;
};

}  // namespace domain_vnext::model
