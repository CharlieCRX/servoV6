// ============================================================================
// TopologyValidator.h —— Step 6 topology: 客户端解码安全校验
// ============================================================================
// 只验证客户端的解码安全不变量（防止内存越界、编造语义），**不**复刻 PLC 的
// 完整业务校验：
//   - Magic 不符（已冻结值）→ MagicMismatch
//   - SchemaVersion 不支持 → UnsupportedSchema
//   - 有效角色的 PlcAxisIndex 越界（0..15）→ SlotOutOfRange
//   - 有效角色间 PlcAxisIndex 重复 → DuplicateSlot
//   - Group.GroupCode 与组下标不符 → GroupCodeMismatch
//   - Reserved 字段非 0（仅当前 schema 要求为 0 时）→ ReservedFieldUnexpected
//
// 输出 std::vector<TopologyIssue{kind, message, context}>；空表示通过。
// 第二组有效本身不构成错误（SecondGroupValid_IsNotRejected）；无效角色的
// 字段（含禁用组的未初始化残留）一律跳过，不产生客户端错误。
// PLC 的 ConfigValid / ConfigErrorCode 由上层按 PLC 有效性决策，本校验器不判定。
// ============================================================================
#pragma once

#include <string>
#include <vector>

#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace plc_vnext::topology {

enum class TopologyIssueKind {
    /// 已启用：Magic 已从真实 PLC 对拍确认并冻结（layout::kTopologyMagic）
    MagicMismatch,
    UnsupportedSchema,
    SlotOutOfRange,
    DuplicateSlot,
    GroupCodeMismatch,
    ReservedFieldUnexpected,
};

struct TopologyIssue {
    TopologyIssueKind kind = TopologyIssueKind::MagicMismatch;
    std::string message;
    std::string context;  ///< 便于定位：如 "D1402" / "group 0" / "group 0 role 5"
};

class TopologyValidator {
public:
    [[nodiscard]] static std::vector<TopologyIssue> validate(
        const contracts::TopologySnapshot& snap);
};

}  // namespace plc_vnext::topology
