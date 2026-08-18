// ============================================================================
// AxisSnapshotDecoder.h —— Step 7 telemetry: 单槽位寄存器 -> AxisRuntimeSnapshot
// ============================================================================
// 纯解码：从覆盖 D0..D175 的原始寄存器块解码单个槽位反馈。字段偏移/数量一律
// 来自 layout::AxisSlotRegisterLayout（与 tools/plc_read_validate.py 一致）：
//   REAL 每项占 2 D（低字在前 CDAB）；INT/WORD 每项占 1 D；0 基址。
// 任一字段缺失 → trusted=false（不填 0 冒充正常）。不校验枚举合法性
// （未知运动状态等一律原样保留）。
// 本类不识别轴/业务、不做任何语义校验。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/contracts/AxisParameterSnapshot.h"
#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"

namespace plc_vnext::telemetry {

class AxisSnapshotDecoder {
public:
    /// 解码 block 中下标为 slot（0..15）的槽位反馈。
    /// 任一字段读取失败 → 返回 trusted=false 的快照（字段默认值不冒充正常）。
    [[nodiscard]] static contracts::AxisRuntimeSnapshot decode(
        const codec::RawRegisterBlock& block, int slot);

    /// 解码 block 中下标为 slot 的参数区（RW）快照（含软限位 D1160/D1192/D1228）。
    /// block 需覆盖参数区（D1064..D1243）；任一字段读取失败 → trusted=false。
    [[nodiscard]] static contracts::AxisParameterSnapshot decodeParams(
        const codec::RawRegisterBlock& block, int slot);
};

}  // namespace plc_vnext::telemetry
