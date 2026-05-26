// infrastructure/plc/protocol/PlcSnapshot.h
// P3: PlcSnapshot — 一次完整的 PLC 现场照片
//
// 核心设计目标：
// - 将 RawBitSnapshot 与 RawWordSnapshot 绑定为同一时刻的采集产物
// - 通过 complete 标志表达"本轮所有 FC 调用是否全部成功"
// - 通过 timestamp 支持 TTL 过期检测和延迟告警

#pragma once
#include <cstdint>
#include "MemorySnapshot.h"

namespace plc::protocol {

/**
 * @brief 一次完整采集周期的 PLC 状态照片
 *
 * @details
 * 在 Modbus 轮询中，FC01（读 Coil）和 FC03（读 HoldingReg）是两次独立的网络调用。
 * 如果 FC01 成功但 FC03 失败，系统里的 bit 是最新的、word 是旧的——数据不一致。
 *
 * PlcSnapshot 将 bit 和 word 快照绑定为一个原子不可分的数据包，
 * 并通过 complete 标志完整表达本次采集的可信度。
 *
 * 业务层使用模式：
 * @code
 *   void updateAxis(const PlcSnapshot& snap) {
 *   if (!snap.isTrusted()) return; // 跳过不可信数据
 *   float pos = device->readFloat("ABS_POSITION");
 *   }
 * @endcode
 */
struct PlcSnapshot {
  RawBitSnapshot  bits;     ///< Coil 物理快照（bit 世界）
  RawWordSnapshot words;    ///< HoldingReg 物理快照（word 世界）
  bool        complete;   ///< 本轮所有 FC 调用全部成功 → true
  uint64_t    timestamp;  ///< 采集时间戳 (ms)

  /// @brief 构造一份完整的 PLC 现场照片
  PlcSnapshot(RawBitSnapshot b, RawWordSnapshot w, bool cmp, uint64_t ts)
    : bits(std::move(b)), words(std::move(w)), complete(cmp), timestamp(ts) {}

  /// @brief 默认构造（不完整快照，用于占位）
  PlcSnapshot()
    : bits(), words(), complete(false), timestamp(0) {}

  /// @brief 是否可信（所有子快照都来自成功的网络读取）
  bool isTrusted() const { return complete; }
};

} // namespace plc::protocol
