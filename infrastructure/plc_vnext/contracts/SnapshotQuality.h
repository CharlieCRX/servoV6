// ============================================================================
// SnapshotQuality.h —— Step 7 contracts: 运行快照的质量标志
// ============================================================================
// 一次轮询（read()）整体数据质量的枚举，作为 contracts::RuntimeSnapshot 的组成
// 字段随 DTO 一并交付上层。
//
// 一次连续读失败**不得**把字段置 0 或“正常”：必须标 trusted=false（槽位/组级）
// 并以本枚举表达整体质量，由上层决定是否锁定普通控制。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

namespace plc_vnext::contracts {

enum class SnapshotQuality {
    /// 所有读请求成功，16 槽位 + 龙门状态均可信
    Trusted,
    /// 数据过期（本帧未获取到新鲜数据）。
    /// 注意：无状态的 PlcSnapshotReader 不会产生本值；本值保留给后续缓存/轮询
    /// 监督器实现陈旧数据策略时使用，避免上层误以为 reader 已实现该策略。
    Stale,
    /// 部分读请求失败：仅可信部分可用，其余槽位/组 trusted=false
    Partial,
    /// 全部读请求失败：不带任何可信数据
    TransportFailed,
};

}  // namespace plc_vnext::contracts
