// ============================================================================
// IPlcRuntimeGateway.h —— Step 10 Gateway: 高层组合门面接口
// ============================================================================
// 对上层（application/domain）暴露唯一的 PLC 运行时通道，聚合只读（拓扑/运行
// 快照）与受控写（单轴/龙门请求）以及连接生命周期。接口只引用 contracts::*
// 纯 DTO，**不含任何业务决策，不依赖 SystemContext / Axis / ViewModel / 旧
// ISystemDriver**（Step 10.3 禁止项）。
//
// 语义边界（与各子模块一致）：
//   - readTopology()/readRuntime()：只读；失败返回 contracts::ReadResult 的
//     FailureKind（Transport/Decode/RevisionChanged）。PLC 已读出的
//     ConfigValid=false 属于快照内容，不是 ReadResult 失败。
//   - writeAxis()/submitGantryRequest()：只提交，`CommunicationResult::ok()`
//     仅证明写请求到达 PLC，不证明 PLC 已执行（读回确认属 Step 10/11 的
//     telemetry/ack reader，不在 Gateway 内实现超时/重试/联动判定）。
// ============================================================================
#pragma once

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace plc_vnext {

class IPlcRuntimeGateway {
public:
    virtual ~IPlcRuntimeGateway() = default;

    /// 双读校验 + 解码 + 校验后返回拓扑快照（只读）。
    virtual contracts::ReadResult<contracts::TopologySnapshot> readTopology() = 0;

    /// 执行读计划，产出 16 槽位 + 龙门状态的运行快照（只读）。
    /// 仅当快照可信（quality == Trusted）时返回 success；否则返回 Transport 失败。
    virtual contracts::ReadResult<contracts::RuntimeSnapshot> readRuntime() = 0;

    /// 提交单轴写入意图（参数写/使能/点动/触发/清除）。只提交，不做读回。
    virtual contracts::CommunicationResult writeAxis(
        contracts::PlcAxisSlot slot, const contracts::PlcAxisCommand& cmd) = 0;

    /// 提交一条龙门请求（组事务：Command→RequestSeq 两笔有序写入）。只提交，不做确认。
    /// 兼容入口：只返回底层通讯结果，**无法表达提交不确定性**；需要处理
    /// CommitUncertain（Command 已写、RequestSeq 结果未知、必须等 AckSeq、禁止重发）
    /// 的调用方必须使用 submitGantryRequestDetailed()。
    virtual contracts::CommunicationResult submitGantryRequest(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) = 0;

    /// 提交一条龙门请求，并返回完整提交阶段（RejectedLocally / CommandNotWritten /
    /// CommitUncertain / Submitted）与本次 requestSeq。上层/ack reader 应优先使用本
    /// 入口，以区分"确定未提交"与"提交结果未知"，并据此决定是否等待 AckSeq。
    virtual contracts::GantrySubmitResult submitGantryRequestDetailed(
        contracts::PlcGroupIndex g, const contracts::GantryRequest& req) = 0;

    /// 当前连接状态快照（TCP 已连接与否 + 诊断文本）。
    virtual contracts::ConnectionState connectionState() const = 0;

    /// 请求立即重连（委托 transport 连接层；本层绝不重放任何命令）。
    virtual void requestReconnect() = 0;
};

}  // namespace plc_vnext
