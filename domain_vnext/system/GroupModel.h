// ============================================================================
// GroupModel.h —— P3 system: 分组功能视图（AxisFunction -> Axis&）
// ============================================================================
// 设计稿 §2「分组只是『功能视图』」、§3 system/GroupModel.h「(功能 -> Axis&) +
// gantry 控制器 + HmiVisible」。轴实体本体在 AxisRegistry，本类只聚合指针。
//   - isReady()：配置有效 && 未降级，才允许开放控制（对齐 §10.5「B 组构建但
//     not-ready，不开放控制」）。
//   - 降级锁定：SystemBoot 在重复/非法配置时 setDegraded(true)，controls 保持锁定。
// 纯领域：只依赖自身 + model + state + AxisRegistry。
// ============================================================================
#pragma once

#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "domain_vnext/logging/DomainLogger.h"
#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/state/GantryCouplingStateMachine.h"
#include "domain_vnext/system/AxisRegistry.h"

namespace domain_vnext::system {

/// 一组（A/B）的领域功能视图。
class GroupModel {
public:
    void setLogGroup(plc_vnext::contracts::PlcGroupIndex g) {
        group_ = g;
        gantry_.setLogGroup(logging::groupName(g));
    }

    void setValid(bool v) { valid_ = v; }
    bool valid() const { return valid_; }

    void setHmiVisible(bool v) { hmiVisible_ = v; }
    bool hmiVisible() const { return hmiVisible_; }

    /// 降级锁定：重复/非法配置后标记，组内控制保持锁定。
    void setDegraded(bool d) { degraded_ = d; }
    bool degraded() const { return degraded_; }

    /// 是否可开放控制 = 配置有效 && 未降级。
    bool isReady() const { return valid_ && !degraded_; }

    /// 绑定功能轴；功能已被占用返回 false（重复绑定降级由 SystemBoot 判定）。
    bool bind(model::AxisFunction fn, Axis& axis) {
        if (members_.count(fn) != 0) return false;
        members_[fn] = &axis;
        return true;
    }
    Axis* find(model::AxisFunction fn) {
        return const_cast<Axis*>(std::as_const(*this).find(fn));
    }
    const Axis* find(model::AxisFunction fn) const {
        const auto it = members_.find(fn);
        return it == members_.end() ? nullptr : it->second;
    }
    std::size_t axisCount() const { return members_.size(); }

    /// 已绑定功能（按枚举序）。
    std::vector<model::AxisFunction> boundFunctions() const {
        std::vector<model::AxisFunction> out;
        out.reserve(members_.size());
        for (const auto& kv : members_) out.push_back(kv.first);
        return out;
    }

    state::GantryCouplingStateMachine& gantryCoupling() { return gantry_; }
    const state::GantryCouplingStateMachine& gantryCoupling() const { return gantry_; }

private:
    std::map<model::AxisFunction, Axis*> members_;
    state::GantryCouplingStateMachine gantry_;
    plc_vnext::contracts::PlcGroupIndex group_{0};
    bool valid_ = false;
    bool hmiVisible_ = false;
    bool degraded_ = false;
};

}  // namespace domain_vnext::system
