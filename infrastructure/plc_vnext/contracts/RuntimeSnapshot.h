// ============================================================================
// RuntimeSnapshot.h —— Step 7 contracts: 一次轮询的运行快照集合
// ============================================================================
// 只表达一次 read() 得到的 16 槽位反馈 + 龙门状态集合及其整体质量，**不注入
// 领域对象**。一个 RuntimeSnapshot 必须有：
//   - 采样时刻 sampledAtMs 与读取耗时 durationMs；
//   - 每槽位/每组的 trusted；
//   - 整体 SnapshotQuality。
// 一次连续读失败时不得把字段置 0 或“正常”，必须标 trusted=false 并以 quality
// 表达，由上层决定是否锁定普通控制。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

#include <array>
#include <cstdint>

#include "infrastructure/plc_vnext/contracts/AxisRuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/GantryStatusSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"

namespace plc_vnext::contracts {

/// 标准 16 槽位数量（与 PlcAxisSlot 范围 0..15 一致）。
constexpr std::size_t kRuntimeAxisCount = 16;
/// 龙门组数量（g=0..1，与 PlcGroupIndex 范围一致）。
constexpr std::size_t kRuntimeGroupCount = 2;

/// 一次轮询得到的完整运行快照。
struct RuntimeSnapshot {
    std::array<AxisRuntimeSnapshot, kRuntimeAxisCount> axes;
    std::array<GantryStatusSnapshot, kRuntimeGroupCount> gantry;

    /// 整体质量（见 contracts::SnapshotQuality）。
    SnapshotQuality quality = SnapshotQuality::TransportFailed;

    /// 采样时刻（steady clock，毫秒）。
    int64_t sampledAtMs = 0;
    /// 本次读取耗时（毫秒）。
    int64_t durationMs = 0;
};

}  // namespace plc_vnext::contracts
