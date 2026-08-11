// ============================================================================
// TopologyValidator.cpp —— Step 6 topology: 校验实现
// ============================================================================
#include "infrastructure/plc_vnext/topology/TopologyValidator.h"

#include <cstdint>
#include <set>
#include <string>
#include <utility>

#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"

namespace plc_vnext::topology {

std::vector<TopologyIssue> TopologyValidator::validate(
    const contracts::TopologySnapshot& snap) {
    std::vector<TopologyIssue> issues;

    // Magic 已从真实 PLC 对拍确认并冻结（见 layout::kTopologyMagic）。
    if (snap.header.magic != layout::kTopologyMagic) {
        issues.push_back({TopologyIssueKind::MagicMismatch,
                          "AxisTopology Magic mismatch",
                          "D1400..D1401"});
    }
    if (snap.header.schemaVersion != layout::kTopologySchemaVersion) {
        issues.push_back({TopologyIssueKind::UnsupportedSchema,
                          "SchemaVersion not supported by this client",
                          "D1402"});
    }

    const bool schemaRequiresZeroReserved =
        snap.header.schemaVersion == layout::kTopologySchemaVersion;
    if (schemaRequiresZeroReserved && snap.header.reserved != 0) {
        issues.push_back({TopologyIssueKind::ReservedFieldUnexpected,
                          "header Reserved must be 0 in current schema", "D1403"});
    }

    for (std::size_t g = 0; g < snap.groups.size(); ++g) {
        const contracts::TopologyGroup& grp = snap.groups[g];
        const std::string gctx = "group " + std::to_string(g);

        if (grp.groupCode != static_cast<int16_t>(g)) {
            issues.push_back({TopologyIssueKind::GroupCodeMismatch,
                              "Group.GroupCode does not match its group index",
                              gctx});
        }
        if (schemaRequiresZeroReserved && grp.reserved != 0) {
            issues.push_back({TopologyIssueKind::ReservedFieldUnexpected,
                              "Group Reserved must be 0 in current schema", gctx});
        }

        for (std::size_t r = 0; r < grp.roles.size(); ++r) {
            const contracts::TopologyRole& role = grp.roles[r];
            if (!role.valid) continue;
            const std::string rctx =
                gctx + " role " + std::to_string(r);

            if (schemaRequiresZeroReserved && role.reserved != 0) {
                issues.push_back({TopologyIssueKind::ReservedFieldUnexpected,
                                  "Role Reserved must be 0 in current schema", rctx});
            }
            if (!contracts::PlcAxisSlot::tryCreate(role.plcAxisIndex)) {
                issues.push_back({TopologyIssueKind::SlotOutOfRange,
                                  "valid Role.PlcAxisIndex out of range 0..15", rctx});
            }
        }
    }

    // 跨全部有效角色检查 PlcAxisIndex 重复（槽位全局唯一 0..15）。
    std::set<int16_t> seenSlots;
    for (const contracts::TopologyGroup& grp : snap.groups) {
        for (std::size_t r = 0; r < grp.roles.size(); ++r) {
            const contracts::TopologyRole& role = grp.roles[r];
            if (!role.valid) continue;
            if (!seenSlots.insert(role.plcAxisIndex).second) {
                issues.push_back({TopologyIssueKind::DuplicateSlot,
                                  "duplicate PlcAxisIndex across valid roles",
                                  "group " + std::to_string(grp.groupCode) +
                                      " role " + std::to_string(r)});
            }
        }
    }

    return issues;
}

}  // namespace plc_vnext::topology
