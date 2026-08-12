// ============================================================================
// PlcGantryCommandWriter.h —— Step 9 command: 龙门请求写入器
// ============================================================================
// 职责：按 PLC 契约把一条龙门请求提交为**两次有序事务**：
//   1) 先写 Command（D180+4g，FC06 单寄存器，INT 0/1/2/3）；
//   2) Command 正常响应后再写 RequestSeq（D181+4g..，FC10 多寄存器，DINT CDAB 低字在前）。
// 不能用一次 FC10 合并二者——它无法表达"序号最后提交"的提交屏障（TDD 文档 §9.3）。
//
// ★ 边界（writer 只负责提交，不复制 PLC 语义）：
//   - 序号属于 application/session 的请求身份；writer 不维护跨请求序号状态，
//     原样写入传入的 N+1，不自行去重、不递增（递增/去重/准入是上层职责）。
//   - 不写 GearIn/GearOut、不写控制许可、不做联动成功判定（Step 10 reader 负责）。
//   - 不做读回确认：`CommunicationResult::ok()` 仅证明写请求到达 PLC。
//   - 断线失败不落盘、重连后不自动重放（无状态）。
//   - 组提交策略（如 B 组 Group[1].Valid=FALSE 时禁止提交）由调用方注入，本类不
//     内置拓扑判断——避免与 PLC 的 ConfigValid 校验逻辑重复（见 §9.3）。
//
// 并发安全：本 writer 自带一把互斥锁，把"Command→RequestSeq"两笔事务包成一个
// 临界区，保证并发提交时 PLC 看到的仍是完整序列 Command→RequestSeq，而不出现
// CommandA→CommandB→RequestSeqA 的交叉。跨类别（单轴写 vs 龙门写）的原子性属于
// Step 10 Gateway（共享同一串行通道/全局锁），不在本 writer 内承诺。
// ============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "infrastructure/plc_vnext/contracts/CommunicationResult.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/transport/IModbusClient.h"

namespace plc_vnext::command {

/// 龙门请求写入器。构造注入 transport client（测试用 FakeModbusClient）。
class PlcGantryCommandWriter {
public:
    /// 组提交策略：返回 false 时该组禁止提交（例如 Group[1].Valid=FALSE）。
    /// 缺省（空函数）放行所有组（0..1）。调用方负责按拓扑注入。
    using GroupGate = std::function<bool(contracts::PlcGroupIndex)>;

    explicit PlcGantryCommandWriter(transport::IModbusClientPtr client,
                                    GroupGate groupGate = {});

    /// 提交一条龙门请求（组事务）。成功 = 两次有序写入均到达 PLC。
    contracts::CommunicationResult submit(contracts::PlcGroupIndex g,
                                          const contracts::GantryRequest& req);

    /// 协议完整性：Command 仅允许 0/1/2/3（地址表 §7）。
    [[nodiscard]] static bool isCommandCodeValid(int16_t code);

private:
    transport::IModbusClientPtr m_client;
    GroupGate m_groupGate;
    // 保证单次请求的 Command→RequestSeq 两笔事务成组原子提交（并发不交叉）。
    std::mutex m_submitMutex;
};

}  // namespace plc_vnext::command
