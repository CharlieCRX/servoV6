// ============================================================================
// GantryParam.h —— P1 model: 龙门参数领域映射
// ============================================================================
// GantryParam（D1600 + 22g）是**配置区**（方向 / 齿轮比 / 阈值 / 超时），纯配置、
// 只读注入，领域不改写。真正的建立 / 解除动作走 GantryCommand（D180）事务，
// 由 GantryCouplingStateMachine（P2）+ CommandMapper（P4）处理。
// 纯 DTO：只依赖自身 + plc_vnext::contracts。
// ============================================================================
#pragma once

#include <cstdint>

namespace domain_vnext::model {

/// 龙门参数领域模型。
struct GantryParamModel {
    bool valid = false;                     // +0 bit0
    bool readyToCouple = false;             // GantryReadyToCouple
    int16_t dirX1 = 0, dirX2 = 0;           // +1/+2
    int16_t ratioNumX1 = 0, ratioDenX1 = 0; // +3/+4
    int16_t ratioNumX2 = 0, ratioDenX2 = 0; // +5/+6
    float positionOffsetX1 = 0.f, positionOffsetX2 = 0.f;  // +7..+10
    float coupleSkewLimit = 0.f, runningSkewLimit = 0.f;   // +11..+14
    int32_t skewDelayMs = 0;                // +15..+16
    int32_t coupleTimeoutMs = 0;            // +17..+18
    int32_t decoupleTimeoutMs = 0;          // +19..+20
    /// 本次读取是否可信。
    bool trusted = false;
};

}  // namespace domain_vnext::model
