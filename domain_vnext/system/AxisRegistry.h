// ============================================================================
// AxisRegistry.h —— P3 system: 16 槽位注册表 + 统一 Axis 单轴实体
// ============================================================================
// 设计稿 §3 system/AxisRegistry.h「16 槽位 -> Axis 实体」、§9「Axis 基类 +
// SingleAxis/CoupledAxis + CommandOutbox」。轴实体本体放在全局 16 槽位注册表，
// 分组只是「功能视图」。Axis 组合 P2 的 AxisStateMachine + CommandOutbox：
//   - requiresGantrySync 由 SystemBoot 按角色注入（X1/X2/X 逻辑轴为 true，
//     Y/Z/R 独立轴为 false）。
// 纯领域：只依赖自身 + model + state + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "domain_vnext/logging/DomainLogger.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/model/AxisParameterSet.h"
#include "domain_vnext/state/AxisStateMachine.h"
#include "domain_vnext/state/CommandOutbox.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace domain_vnext::system {

/// 统一单轴实体（普通轴与联动轴的公共能力，见 §2 / §9）。
/// CoupledAxis 追加联动数据属 P4/P5 展开，本类为 P3 组合根的基础实体。
class Axis {
public:
    Axis(model::AxisKey key, plc_vnext::contracts::PlcAxisSlot slot,
         std::int16_t motorNo, std::int32_t axisClass, std::int32_t motionMode,
         bool hmiVisible, bool requiresGantrySync)
        : key_(key), slot_(slot), motorNo_(motorNo), axisClass_(axisClass),
          motionMode_(motionMode), hmiVisible_(hmiVisible),
          sm_(requiresGantrySync) {
        sm_.setLogContext(logging::groupName(key_.group),
                          logging::axisName(key_.function));
    }

    const model::AxisKey& key() const { return key_; }
    plc_vnext::contracts::PlcAxisSlot slot() const { return slot_; }
    std::int16_t motorNo() const { return motorNo_; }
    std::int32_t axisClass() const { return axisClass_; }
    std::int32_t motionMode() const { return motionMode_; }
    bool hmiVisible() const { return hmiVisible_; }

    state::AxisStateMachine& stateMachine() { return sm_; }
    const state::AxisStateMachine& stateMachine() const { return sm_; }
    state::CommandOutbox& outbox() { return outbox_; }
    const state::CommandOutbox& outbox() const { return outbox_; }

    // --- 反馈注入（P4，由 FeedbackDispatcher 接入）---
    /// 注入运行反馈前 7 项（来自 AxisRuntimeSnapshot，只读）。trusted 由该快照决定。
    void applyFeedback(const plc_vnext::contracts::AxisRuntimeSnapshot& snap) {
        const auto oldMotionState = feedback_.motionState;
        const auto oldTrusted = feedback_.trusted;
        const bool hadFeedback = feedbackSeen_;
        feedback_.manualSpeed = snap.manualSpeed;
        feedback_.positioningSpeed = snap.positioningSpeed;
        feedback_.absPosition = snap.absPosition;
        feedback_.relPosition = snap.relPosition;
        feedback_.motionState = snap.motionState;
        feedback_.motionLimit = snap.motionLimit;
        feedback_.alarmWord = snap.alarmWord;
        feedback_.trusted = snap.trusted;
        feedbackSeen_ = true;

        if (!hadFeedback || oldMotionState != feedback_.motionState ||
            oldTrusted != feedback_.trusted) {
            Logger::logWithContext(
                LogLevel::INFO, LogLayer::DOM, "AxisFeedback",
                logging::context(key_.group, key_.function, "feedback"),
                "slot=" + std::to_string(slot_.value())
                + " motionState=" + std::to_string(feedback_.motionState)
                + " trusted=" + std::to_string(feedback_.trusted)
                + " pos=" + std::to_string(feedback_.absPosition)
                + " limit=" + std::to_string(feedback_.motionLimit)
                + " alarm=0x" + std::to_string(feedback_.alarmWord));
        }
    }
    /// 注入参数区 8~13（来自 AxisParameterSnapshot，只读）。trusted 与运行反馈做与。
    void applyParameters(const plc_vnext::contracts::AxisParameterSnapshot& snap) {
        feedback_.relZeroRecord = snap.relZeroRecord;
        feedback_.absMoveDistance = snap.absMoveDistance;
        feedback_.relMoveDistance = snap.relMoveDistance;
        feedback_.softNegLimit = snap.softNegLimit;
        feedback_.softPosLimit = snap.softPosLimit;
        feedback_.softLimitControl = snap.softLimitControl;
        feedback_.trusted = feedback_.trusted && snap.trusted;
    }
    model::AxisParameterSet& feedback() { return feedback_; }
    const model::AxisParameterSet& feedback() const { return feedback_; }

private:
    model::AxisKey key_;
    plc_vnext::contracts::PlcAxisSlot slot_;
    std::int16_t motorNo_;
    std::int32_t axisClass_;
    std::int32_t motionMode_;
    bool hmiVisible_;
    state::AxisStateMachine sm_;
    state::CommandOutbox outbox_;
    model::AxisParameterSet feedback_;
    bool feedbackSeen_ = false;
};

/// 全局 16 槽位轴实体注册表。
class AxisRegistry {
public:
    static constexpr std::size_t kSlotCount = 16;

    /// 注册轴实体；槽位被占用或越界时返回 false（不覆盖）。
    bool registerAxis(plc_vnext::contracts::PlcAxisSlot slot, Axis axis) {
        const auto v = static_cast<std::size_t>(slot.value());
        if (v >= kSlotCount) return false;
        if (slots_[v].has_value()) return false;
        slots_[v] = std::move(axis);
        return true;
    }

    bool isOccupied(plc_vnext::contracts::PlcAxisSlot slot) const {
        const auto v = static_cast<std::size_t>(slot.value());
        if (v >= kSlotCount) return false;
        return slots_[v].has_value();
    }

    Axis* find(plc_vnext::contracts::PlcAxisSlot slot) {
        return const_cast<Axis*>(std::as_const(*this).find(slot));
    }
    const Axis* find(plc_vnext::contracts::PlcAxisSlot slot) const {
        const auto v = static_cast<std::size_t>(slot.value());
        if (v >= kSlotCount) return nullptr;
        const auto& o = slots_[v];
        return o ? &*o : nullptr;
    }

    std::size_t count() const {
        std::size_t n = 0;
        for (const auto& o : slots_) {
            if (o) ++n;
        }
        return n;
    }

    /// 已占用槽位（升序），便于遍历。
    std::vector<plc_vnext::contracts::PlcAxisSlot> occupiedSlots() const {
        std::vector<plc_vnext::contracts::PlcAxisSlot> out;
        for (std::size_t i = 0; i < slots_.size(); ++i) {
            if (slots_[i]) {
                out.push_back(*plc_vnext::contracts::PlcAxisSlot::tryCreate(
                    static_cast<int>(i)));
            }
        }
        return out;
    }

    void clear() {
        for (auto& o : slots_) o.reset();
    }

private:
    std::array<std::optional<Axis>, kSlotCount> slots_;
};

}  // namespace domain_vnext::system
