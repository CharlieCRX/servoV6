// ============================================================================
// GantrySubmitResult.h —— Step 9 command: 龙门请求提交状态
// ============================================================================
// 一次龙门提交（Command→RequestSeq 两笔有序事务）的结果，除底层通讯结果外，
// 显式表达"提交处于哪个阶段"，让上层（Gateway/ack reader）能区分：
//   - 确定未提交（RejectedLocally / CommandNotWritten）
//   - 提交结果未知（CommitUncertain：Command 已写，RequestSeq 结果未知——
//     PLC 可能已收也可能没收，绝不能据此立即生成新序号重发）
//   - 已提交（Submitted）
//
// 保留 contracts::CommunicationResult 作为底层通讯结果，语义不变。
// 纯 DTO：不依赖 Modbus / Qt / Domain。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"

namespace plc_vnext::contracts {

/// 龙门提交所处的阶段。
enum class GantrySubmitState {
    /// 本地拒绝（准入策略 / 非法命令码），未发起任何写入。
    RejectedLocally,
    /// Command 未成功写入 PLC，请求确定未提交。
    CommandNotWritten,
    /// Command 已写入，RequestSeq 结果未知（超时/响应丢失）。
    /// 可能已提交也可能未提交，不能据此重发，需由 ack reader 按 AckSeq 判定。
    CommitUncertain,
    /// Command 与 RequestSeq 均已写入 PLC（写请求已到达，不表示 PLC 已执行）。
    Submitted,
};

/// 龙门提交结果：阶段 + 底层通讯结果 + 本次请求序号。
struct GantrySubmitResult {
    GantrySubmitState state = GantrySubmitState::RejectedLocally;
    CommunicationResult result;  ///< 底层最新一次通讯结果（诊断/重试依据）
    /// 本次请求的 requestSeq。ack reader 据此判定"PLC 是否已接收"：
    /// `GantryStatusSnapshot.ackSeq == requestSeq` → 已提交；否则等待/由用户决定。
    int32_t requestSeq = 0;

    [[nodiscard]] bool ok() const {
        return state == GantrySubmitState::Submitted;
    }
    [[nodiscard]] bool committedUnknown() const {
        return state == GantrySubmitState::CommitUncertain;
    }
};

}  // namespace plc_vnext::contracts
