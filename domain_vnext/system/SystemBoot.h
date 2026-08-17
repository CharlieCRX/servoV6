// ============================================================================
// SystemBoot.h —— P3 system: TopologySnapshot -> AxisSystem 动态初始化
// ============================================================================
// 设计稿 §3 system/SystemBoot.h「TopologySnapshot -> AxisSystem 动态初始化」。
// 从 plc_vnext::contracts::TopologySnapshot 动态建轴、A/B 分组、HmiVisible 判定、
// 重复/非法配置降级锁定（P3 验证点，见 §8）。
//   - role.valid=false（PLC 空绑定）/ 未映射功能（Role 6/7 超出领域功能集）：合规跳过；
//   - slot 越界（非 0..15）/ 重复槽位绑定：降级锁定（组标记 degraded，控制关闭）并记录 issue；
//   - requiresGantrySync 按角色注入：X1/X2/X(逻辑轴)=true，Y/Z/R=false。
// 纯领域：只依赖自身 + model + plc_vnext::contracts（纯 DTO）。
// ============================================================================
#pragma once

#include <cstddef>
#include <vector>

#include "domain_vnext/model/AxisFunction.h"
#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/system/AxisSystem.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace domain_vnext::system {

/// 启动期降级/阻塞 issue。
struct SystemBootIssue {
    enum class Kind {
        IllegalSlotIndex,   // role.plcAxisIndex 越界（非 0..15）
        DuplicateSlotBinding,  // 两角色绑定同一槽位（全局冲突）
    };
    Kind kind;
    std::size_t group = 0;
    std::size_t role = 0;
    int plcAxisIndex = -1;
};

/// 启动结果（诊断用，可被上层日志化）。
struct SystemBootResult {
    bool ok = false;             ///< 无阻塞问题（未降级）
    bool degraded = false;       ///< 发生重复/非法配置，已降级锁定
    bool configValid = false;    ///< 来自 TopologySnapshot.header.configValid（PLC 校验结果）
    std::size_t registeredAxes = 0;
    std::vector<SystemBootIssue> issues;
};

class SystemBoot {
public:
    /// 用拓扑快照重建 AxisSystem。幂等（先 reset 清空注册表与分组视图）。
    static SystemBootResult initialize(
        AxisSystem& sys, const plc_vnext::contracts::TopologySnapshot& topo) {
        sys.reset();
        SystemBootResult result;
        result.configValid = topo.header.configValid;

        const std::size_t groupCount =
            topo.groups.size() < 2 ? topo.groups.size() : 2;
        for (std::size_t g = 0; g < groupCount; ++g) {
            const auto& tg = topo.groups[g];
            const auto groupIndex =
                plc_vnext::contracts::PlcGroupIndex(static_cast<int>(g));
            auto& gm = sys.group(groupIndex);
            gm.setValid(tg.valid);
            gm.setHmiVisible(tg.hmiVisible);

            for (std::size_t r = 0; r < tg.roles.size(); ++r) {
                const auto& role = tg.roles[r];
                // 无效角色（PLC 空绑定，索引写 -1）：合规跳过，不视为降级。
                if (!role.valid) continue;
                // 未映射功能（Role 6/7 超出领域功能集 X/X1/X2/Y/Z/R）：合规跳过。
                const auto fnOpt =
                    model::axisFunctionFromRoleIndex(static_cast<int>(r));
                if (!fnOpt) continue;
                const auto fn = *fnOpt;

                // 槽位合法性校验。
                const auto slotOpt = plc_vnext::contracts::PlcAxisSlot::tryCreate(
                    role.plcAxisIndex);
                if (!slotOpt) {
                    result.degraded = true;
                    gm.setDegraded(true);
                    result.issues.push_back({SystemBootIssue::Kind::IllegalSlotIndex,
                                             g, r, role.plcAxisIndex});
                    continue;
                }
                const auto slot = *slotOpt;
                // 重复绑定校验（全局 16 槽位唯一）。
                if (sys.registry().isOccupied(slot)) {
                    result.degraded = true;
                    gm.setDegraded(true);
                    result.issues.push_back({SystemBootIssue::Kind::DuplicateSlotBinding,
                                             g, r, role.plcAxisIndex});
                    continue;
                }

                // HmiVisible 展示判定 = 组可见 && 角色可见（§2 / §5.1）。
                const bool axisHmi = tg.hmiVisible && role.hmiVisible;
                // 龙门同步依赖：X1/X2/X 逻辑轴 true，Y/Z/R 独立轴 false。
                const bool gantry =
                    fn == model::AxisFunction::X1 || fn == model::AxisFunction::X2 ||
                    fn == model::AxisFunction::X;
                const model::AxisKey key{groupIndex, fn};

                sys.registry().registerAxis(
                    slot, Axis(key, slot, role.motorNo, role.axisClass,
                               role.motionMode, axisHmi, gantry));
                gm.bind(fn, *sys.registry().find(slot));
                ++result.registeredAxes;
            }

        }

        result.ok = !result.degraded;
        return result;
    }
};

}  // namespace domain_vnext::system
