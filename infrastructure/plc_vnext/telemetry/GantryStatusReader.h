// ============================================================================
// GantryStatusReader.h —— Step 7 telemetry: 组级寄存器 -> GantryStatusSnapshot
// ============================================================================
// 纯解码：从覆盖 D190..D225 的原始寄存器块解码 GantryStatus[g]（g=0..1，每组
// 18 D）。字段偏移与 tools/plc_read_validate.py GANTRY_STATUS_FIELDS 及地址表
// §8 一致，端序 CDAB（低字在前）。
// 任一字段缺失 → trusted=false（不填 0 冒充正常）。
// 本类不识别轴/业务、不做任何语义校验。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h"

namespace plc_vnext::telemetry {

class GantryStatusReader {
public:
    /// 解码 block 中下标为 group（0..1）的龙门状态快照。
    /// 任一字段读取失败 → 返回 trusted=false 的快照。
    [[nodiscard]] static contracts::GantryStatusSnapshot decode(
        const codec::RawRegisterBlock& block, int group);
};

}  // namespace plc_vnext::telemetry
