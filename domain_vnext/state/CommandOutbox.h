// ============================================================================
// CommandOutbox.h —— P2 state: 批量意图槽位（解决旧 Axis::m_pending_intent 单一覆盖）
// ============================================================================
// 设计稿 §4.3：定位设置 / 触发 / 终止必须严格有序（set target -> wait accepted
// -> trigger），故放入保序的 motion 队列并分配 seq；清零 / 报警解除等触发无需
// 保序，放入 pulses 队列；普通参数写按字段去重（DirtyParameterSet）。
// 纯领域：只依赖自身 + model。最终由 CommandMapper（P4）一次 drain 取走。
// ============================================================================
#pragma once

#include <cstddef>
#include <deque>
#include <map>
#include <vector>

#include "domain_vnext/model/AxisCommand.h"

namespace domain_vnext::state {

/// 参数区脏标记：普通参数按字段去重（同一字段多次写入只保留最后一次值）。
struct DirtyParameterSet {
    std::map<model::AxisCommandKind, model::AxisCommand> items;

    void set(const model::AxisCommand& cmd) { items[cmd.kind] = cmd; }
    bool empty() const { return items.empty(); }
    void clear() { items.clear(); }
    std::size_t size() const { return items.size(); }

    /// 按字段（枚举序）展开为命令列表，供 CommandMapper 取走。
    std::vector<model::AxisCommand> toCommands() const {
        std::vector<model::AxisCommand> out;
        out.reserve(items.size());
        for (const auto& entry : items) {
            out.push_back(entry.second);
        }
        return out;
    }
};

/// 定位设置 / 触发 / 终止：严格有序（分配递增 seq，便于上层对齐确认）。
struct AxisSequencedCommand {
    int seq = 0;
    model::AxisCommand cmd;
};

/// 清零 / 报警解除等触发：无需保序。
struct AxisPulseCommand {
    model::AxisCommand cmd;
};

/// 批量意图槽位。
class CommandOutbox {
public:
    void push(const model::AxisCommand& cmd);

    bool hasAny() const {
        return !params_.empty() || !motion_.empty() || !pulses_.empty();
    }
    bool hasDirtyParameters() const { return !params_.empty(); }
    bool hasMotion() const { return !motion_.empty(); }
    bool hasPulses() const { return !pulses_.empty(); }
    std::size_t motionCount() const { return motion_.size(); }
    std::size_t pulseCount() const { return pulses_.size(); }

    /// 一次取走全部：参数（去重，枚举序） -> 运动（保序） -> 其他触发。
    std::vector<model::AxisCommand> drain() {
        std::vector<model::AxisCommand> out;
        auto params = params_.toCommands();
        out.insert(out.end(), params.begin(), params.end());
        for (const auto& m : motion_) {
            out.push_back(m.cmd);
        }
        for (const auto& p : pulses_) {
            out.push_back(p.cmd);
        }
        params_.clear();
        motion_.clear();
        pulses_.clear();
        return out;
    }

private:
    static bool isParameter(model::AxisCommandKind kind);
    static bool isMotion(model::AxisCommandKind kind);

    DirtyParameterSet params_;
    std::deque<AxisSequencedCommand> motion_;
    std::deque<AxisPulseCommand> pulses_;
    int nextSeq_ = 0;
};

inline bool CommandOutbox::isParameter(model::AxisCommandKind kind) {
    switch (kind) {
        case model::AxisCommandKind::SetManualSpeed:
        case model::AxisCommandKind::SetPositioningSpeed:
        case model::AxisCommandKind::SetSoftNegLimit:
        case model::AxisCommandKind::SetSoftPosLimit:
        case model::AxisCommandKind::SetSoftLimitControl:
        case model::AxisCommandKind::SetRelZeroRecord:
            return true;
        default:
            return false;
    }
}

inline bool CommandOutbox::isMotion(model::AxisCommandKind kind) {
    switch (kind) {
        // 定位目标 / 触发 / 终止：严格有序
        case model::AxisCommandKind::SetAbsDistance:
        case model::AxisCommandKind::SetRelDistance:
        case model::AxisCommandKind::TriggerAbsMove:
        case model::AxisCommandKind::TriggerRelMove:
        case model::AxisCommandKind::StopAbsMove:
        case model::AxisCommandKind::StopRelMove:
            return true;
        default:
            return false;
    }
}

inline void CommandOutbox::push(const model::AxisCommand& cmd) {
    if (isParameter(cmd.kind)) {
        params_.set(cmd);
    } else if (isMotion(cmd.kind)) {
        motion_.push_back(AxisSequencedCommand{nextSeq_++, cmd});
    } else {
        pulses_.push_back(AxisPulseCommand{cmd});
    }
}

}  // namespace domain_vnext::state
