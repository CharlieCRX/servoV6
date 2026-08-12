// ============================================================================
// AxisSystem.h —— P3 system: 组合根（聚合）
// ============================================================================
// 设计稿 §3 system/AxisSystem.h「组合根：注册表 + GroupModel[2] + Safety」。
//   - registry：全局 16 槽位轴实体；group(g)：A/B 功能视图；safety：全局急停。
//   - find(AxisKey)：按 (组, 功能) 查找；findBySlot(slot)：按槽位查找。
// 纯领域：只依赖自身 + model + state + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <array>
#include <cstddef>
#include <utility>

#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/state/SafetyStateMachine.h"
#include "domain_vnext/system/AxisRegistry.h"
#include "domain_vnext/system/GroupModel.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"

namespace domain_vnext::system {

/// 领域组合根。
class AxisSystem {
public:
    AxisRegistry& registry() { return registry_; }
    const AxisRegistry& registry() const { return registry_; }

    GroupModel& group(plc_vnext::contracts::PlcGroupIndex g) {
        return groups_[static_cast<std::size_t>(g.value())];
    }
    const GroupModel& group(plc_vnext::contracts::PlcGroupIndex g) const {
        return groups_[static_cast<std::size_t>(g.value())];
    }

    state::SafetyStateMachine& safety() { return safety_; }
    const state::SafetyStateMachine& safety() const { return safety_; }

    /// 按 (组, 功能) 查找轴实体。
    Axis* find(model::AxisKey key) {
        return const_cast<Axis*>(std::as_const(*this).find(key));
    }
    const Axis* find(model::AxisKey key) const {
        if (key.group.value() < plc_vnext::contracts::PlcGroupIndex::kMin ||
            key.group.value() > plc_vnext::contracts::PlcGroupIndex::kMax) {
            return nullptr;
        }
        return group(key.group).find(key.function);
    }

    /// 按槽位查找轴实体。
    Axis* findBySlot(plc_vnext::contracts::PlcAxisSlot slot) {
        return registry_.find(slot);
    }
    const Axis* findBySlot(plc_vnext::contracts::PlcAxisSlot slot) const {
        return registry_.find(slot);
    }

    std::size_t totalAxes() const { return registry_.count(); }

    /// 全部组都 ready 才视为系统可开放控制。
    bool isReady() const {
        return group(plc_vnext::contracts::PlcGroupIndex(0)).isReady() &&
               group(plc_vnext::contracts::PlcGroupIndex(1)).isReady();
    }

    /// 清空注册表与分组视图（供 SystemBoot 幂等重建，保留 safety 同步状态）。
    void reset() {
        registry_.clear();
        groups_ = {GroupModel{}, GroupModel{}};
    }

private:
    AxisRegistry registry_;
    std::array<GroupModel, 2> groups_;
    state::SafetyStateMachine safety_;
};

}  // namespace domain_vnext::system
