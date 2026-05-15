#pragma once

#include "gantry/GantryCouplingState.h"
#include "gantry/GantryFeedback.h"
#include "gantry/GantryRejection.h"
#include <optional>

// 表达龙门联动控制意图的独立 Command
struct GantryCouplingCommand { 
    bool enableCoupling; 
};

// ==========================================
// 龙门联动控制器 — 五态耦合状态机
// ==========================================
// 职责：管理 PLC 寄存器「轴X联动使能」的上位机侧状态机
//       - 生成联动/解耦意图（GantryCouplingCommand）
//       - 接收 PLC 反馈（GantryFeedback::isCoupled / errorCode）同步状态
//       - 不持有任何 Axis 引用（PLC 负责所有物理安全校验）
//       - 在任何联动状态下均可独立访问
//
// 状态机：
//   NotSynchronized → Coupled / Decoupled（仅通过 applyFeedback 退出）
//   Decoupled       → CouplingRequested → Coupled
//   Coupled         → DecouplingRequested → Decoupled
// ==========================================

class GantryCouplingController {
public:
    GantryCouplingController() = default;

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
            // 幂等：已联动或联动请求进行中，视为成功但不产生新命令
            if (m_state.isCoupled() || m_state.isCouplingRequested()) {
                return GantryRejection::None;
            }
            // 冲突：解耦请求进行中，不允许反向操作
            if (m_state.isDecouplingRequested()) {
                return GantryRejection::StateConflict;
            }
            // 通过：Decoupled → 生成联动意图
            // 注意：不再检查 Axis Error 状态，X1/X2 是否使能/静止/超差
            //       由 PLC 通过 Gantry_Error_Code 反馈，PLC 是最终安全裁决者
            m_pending_intent = GantryCouplingCommand{ true };
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
            m_pending_intent = GantryCouplingCommand{ false };
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

    GantryCouplingCommand popPendingCommand() {
        auto cmd = *m_pending_intent;
        m_pending_intent.reset(); 
        return cmd;
    }

    // ==========================================
    // 核心 3：统一的反馈接收 (Apply Feedback)
    // ==========================================
    // 注意：只消费 isCoupled 和 errorCode，不消费 enable
    //       enable 由 GantryServoPowerController（原 GantryServoPowerController）消费
    void applyFeedback(const GantryFeedback& feedback) {
        // 1. 翻译并记录 PLC 错误码
        m_last_error = translatePlcError(feedback.errorCode);

        // 2. 状态感知的反馈处理
        if (m_state.isCouplingRequested()) {
            if (feedback.errorCode != 0) {
                // PLC 已明确拒绝联动 → 回退到解耦状态
                m_state.applyDecoupledFeedback();
            } else if (feedback.isCoupled) {
                // PLC 已确认联动成功
                m_state.applyCoupledFeedback();
            }
            // isCoupled=false 且 errorCode=0：中间帧，保持 CouplingRequested 等待

        } else if (m_state.isDecouplingRequested()) {
            // 解耦操作 PLC 不返回错误码（errorCode 始终为 None），
            // 因此不需要根据 errorCode 回退状态。
            // 仅根据 isCoupled 判断解耦是否完成。
            if (!feedback.isCoupled) {
                // PLC 已确认解耦成功
                m_state.applyDecoupledFeedback();
            }
            // isCoupled=true：中间帧，保持 DecouplingRequested 等待

        } else {
            // NotSynchronized / Coupled / Decoupled：直接反映物理真相
            if (feedback.isCoupled) {
                m_state.applyCoupledFeedback();
            } else {
                m_state.applyDecoupledFeedback();
            }
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
    GantryCouplingState m_state;
    std::optional<GantryCouplingCommand> m_pending_intent;
    GantryRejection m_last_error = GantryRejection::None;
};
