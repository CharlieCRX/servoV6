#pragma once

#include "entity/Axis.h"
#include "gantry/GantryCouplingState.h"
#include "gantry/GantryRejection.h"
#include <optional>

// ==========================================
// 1. 独立的数据传输对象 (DTOs)
// ==========================================

// 表达龙门控制意图的独立 Command
struct GantryCommand { 
    bool enableCoupling; 
};

// 表达龙门底层物理状态的反馈快照
struct GantryFeedback {
    bool isCoupled;      // 对应 PLC 寄存器: 轴X联动状态 (ON/OFF)
    int errorCode;       // 对应 PLC 寄存器: Gantry_Error_Code
};

// ==========================================
// 2. 充血的聚合根 GantryGroup
// ==========================================

class GantryGroup {
public:
    GantryGroup(Axis& x1, Axis& x2)
        : m_x1(x1), m_x2(x2) {}

    // --- 状态查询 ---
    bool isNotSynchronized() const { return m_state.isNotSynchronized(); }
    bool isCoupled() const { return m_state.isCoupled(); }
    bool isCouplingRequested() const { return m_state.isCouplingRequested(); }
    bool isDecouplingRequested() const { return m_state.isDecouplingRequested(); } 

    // --- 错误查询 ---
    bool hasError() const { return m_last_error != GantryRejection::None; }
    GantryRejection getLastError() const { return m_last_error; }


    // ==========================================
    // 核心 1：意图生成 (Produce Intent)
    // ==========================================
    GantryRejection requestCouple(bool active) {
        // 前置拦截：状态机尚未与 PLC 物理状态同步，拒绝一切意图操作
        if (m_state.isNotSynchronized()) {
            return GantryRejection::NotSynchronized;
        }

        if (active) {
            // --- 联动请求 ---
            // 安全屏障：任一轴处于 Error 状态，拒绝联动
            if (m_x1.state() == AxisState::Error || m_x2.state() == AxisState::Error) {
                return GantryRejection::AxisStateError;
            }
            // 幂等：已联动或联动请求进行中，视为成功但不产生新命令
            if (m_state.isCoupled() || m_state.isCouplingRequested()) {
                return GantryRejection::None;
            }
            // 冲突：解耦请求进行中，不允许反向操作
            if (m_state.isDecouplingRequested()) {
                return GantryRejection::StateConflict;
            }
            // 通过：Decoupled → 生成联动意图
            m_pending_intent = GantryCommand{ true };
            m_state.requestCouple();
        } else {
            // --- 解耦请求 ---
            // 幂等：已解耦或解耦请求进行中，视为成功但不产生新命令
            if (!m_state.isCoupled() && !m_state.isCouplingRequested()) {
                return GantryRejection::None;
            }
            // 冲突：联动请求进行中，不允许反向操作
            if (m_state.isCouplingRequested()) {
                return GantryRejection::StateConflict;
            }
            // 通过：Coupled → 生成解耦意图
            m_pending_intent = GantryCommand{ false };
            m_state.requestDecouple();
        }
        return GantryRejection::None;
    }

    // ==========================================
    // 核心 2：意图暴露与弹出 (Pop Intent)
    // ==========================================
    bool hasPendingCommand() const { 
        return m_pending_intent.has_value(); 
    }

    GantryCommand popPendingCommand() {
        auto cmd = *m_pending_intent;
        m_pending_intent.reset(); 
        return cmd;
    }

    // ==========================================
    // 核心 3：统一的反馈接收 (Apply Feedback)
    // ==========================================
    void applyFeedback(const GantryFeedback& feedback) {
        // 1. 翻译并记录 PLC 错误码
        m_last_error = translatePlcError(feedback.errorCode);

        // 2. 根据 PLC 实际联动状态驱动本地状态机
        if (feedback.isCoupled) {
            m_state.applyCoupledFeedback();
        } else {
            m_state.applyDecoupledFeedback();
        }
    }

private:
    GantryRejection translatePlcError(int plcErrorCode) const {
        switch (plcErrorCode) {
            case 0: return GantryRejection::None;
            case 1: return GantryRejection::PositionToleranceExceeded;
            case 2: return GantryRejection::X1NotEnabled;
            case 3: return GantryRejection::X2NotEnabled;
            case 4: return GantryRejection::X1NotStationary;
            case 5: return GantryRejection::X2NotStationary;
            default: return GantryRejection::UnknownError;
        }
    }

private:
    Axis& m_x1;
    Axis& m_x2;
    GantryCouplingState m_state;
    std::optional<GantryCommand> m_pending_intent;
    GantryRejection m_last_error = GantryRejection::None;
};
