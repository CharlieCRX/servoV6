// ============================================================================
// PlcGantryCommandWriter.cpp —— Step 9 command: 龙门请求写入实现
// ============================================================================
#include "infrastructure/plc_vnext/command/PlcGantryCommandWriter.h"

#include <string>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"

namespace plc_vnext::command {
namespace {

// 《PLC变量协议_Modbus最终地址表.md》§7 / §2.3：DINT 采用低字在低地址（CDAB）。
// 与 plc_read_validate.py 自测断言一致：-65536 → {0x0000, 0xFFFF}。
constexpr codec::EndianPolicy kCDAB{codec::ByteOrder::BigEndian,
                                    codec::WordOrder::LowWordFirst};

}  // namespace

PlcGantryCommandWriter::PlcGantryCommandWriter(transport::IModbusClientPtr client,
                                               GroupGate groupGate)
    : m_client(std::move(client)), m_groupGate(std::move(groupGate)) {}

bool PlcGantryCommandWriter::isCommandCodeValid(int16_t code) {
    // 仅 1 建立 / 2 解除 / 3 安全复位。None=0 是 PLC 的"无命令状态"，不是有效事务。
    return code >= 1 && code <= 3;
}

contracts::CommunicationResult PlcGantryCommandWriter::submit(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    // 旧契约等价：只暴露底层通讯结果（无法表达"提交结果未知"）。
    return submitDetailed(g, req).result;
}

contracts::GantrySubmitResult PlcGantryCommandWriter::submitDetailed(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    // 组提交策略：由调用方按拓扑注入（如 B 组 Group[1].Valid=FALSE 时禁止提交）。
    // 本 writer 不内置拓扑校验，避免与 PLC 的 ConfigValid 校验逻辑重复（§9.3）。
    if (m_groupGate && !m_groupGate(g)) {
        return {contracts::GantrySubmitState::RejectedLocally,
                contracts::CommunicationResult{
                    contracts::CommunicationResult::Status::ProtocolError, 0,
                    "PlcGantryCommandWriter: group submission rejected by policy"},
                req.requestSeq};
    }

    const int16_t code = static_cast<int16_t>(req.command);
    if (!isCommandCodeValid(code)) {
        // 本地拒绝（含 None=0），未发起任何写入。
        return {contracts::GantrySubmitState::RejectedLocally,
                contracts::CommunicationResult{
                    contracts::CommunicationResult::Status::ProtocolError, 0,
                    "PlcGantryCommandWriter: invalid GantryCommand code " +
                        std::to_string(code)},
                req.requestSeq};
    }

    const auto cmdLayout = layout::gantryCommand(g.value());

    // 两笔有序事务必须成组原子：并发下保证 Command→RequestSeq 不被其它提交插入。
    // （单通道串行化的 ModbusIoExecutor 只保证单笔事务原子，不保证两笔成组；
    //   跨 writer/reader 的成组原子由 Step 10 Gateway 的共享组锁保证。）
    std::lock_guard<std::mutex> lock(m_submitMutex);

    // 1) 先写 Command（FC06，单寄存器，INT 1/2/3）。
    const auto cmdRes = m_client->writeSingleRegister(
        static_cast<uint16_t>(cmdLayout.command.value()),
        static_cast<uint16_t>(code));
    if (!cmdRes.ok()) {
        // Command 未写入：请求确定未提交，且不自动重发 Command。
        return {contracts::GantrySubmitState::CommandNotWritten, cmdRes,
                req.requestSeq};
    }

    // 2) Command 正常响应后再写 RequestSeq（FC10，D181..182，DINT CDAB 低字在前）。
    //    RequestSeq 失败（超时/响应丢失）：无法确定 PLC 是否已收到 Command，
    //    → CommitUncertain；writer 不自动重试、不重放，交由上层 ack reader 判定。
    const std::vector<uint16_t> seqWords =
        codec::RegisterCodec::encodeInt32(req.requestSeq, kCDAB);
    const auto seqRes = m_client->writeMultipleRegisters(
        static_cast<uint16_t>(cmdLayout.requestSeq.value()), seqWords);
    if (!seqRes.ok()) {
        return {contracts::GantrySubmitState::CommitUncertain, seqRes,
                req.requestSeq};
    }

    // 两笔均已写入（写请求到达 PLC；不表示 PLC 已执行联动）。
    return {contracts::GantrySubmitState::Submitted, seqRes, req.requestSeq};
}

}  // namespace plc_vnext::command
