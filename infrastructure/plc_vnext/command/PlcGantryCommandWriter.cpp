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
    return code >= 0 && code <= 3;  // 0无 1建立 2解除 3安全复位
}

contracts::CommunicationResult PlcGantryCommandWriter::submit(
    contracts::PlcGroupIndex g, const contracts::GantryRequest& req) {
    // 组提交策略：由调用方按拓扑注入（如 B 组 Group[1].Valid=FALSE 时禁止提交）。
    // 本 writer 不内置拓扑校验，避免与 PLC 的 ConfigValid 校验逻辑重复（§9.3）。
    if (m_groupGate && !m_groupGate(g)) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::ProtocolError, 0,
            "PlcGantryCommandWriter: group submission rejected by policy"};
    }

    const int16_t code = static_cast<int16_t>(req.command);
    if (!isCommandCodeValid(code)) {
        return contracts::CommunicationResult{
            contracts::CommunicationResult::Status::ProtocolError, 0,
            "PlcGantryCommandWriter: invalid GantryCommand code " +
                std::to_string(code)};
    }

    const auto cmdLayout = layout::gantryCommand(g.value());

    // 两笔有序事务必须成组原子：并发下保证 Command→RequestSeq 不被其它提交插入。
    // （单通道串行化的 ModbusIoExecutor 只保证单笔事务原子，不保证两笔成组。）
    std::lock_guard<std::mutex> lock(m_submitMutex);

    // 1) 先写 Command（FC06，单寄存器，INT 0/1/2/3）。
    const auto cmdRes = m_client->writeSingleRegister(
        static_cast<uint16_t>(cmdLayout.command.value()),
        static_cast<uint16_t>(code));
    if (!cmdRes.ok()) {
        return cmdRes;  // Command 失败：不写 RequestSeq，且不自动重发 Command。
    }

    // 2) Command 正常响应后再写 RequestSeq（FC10，D181..182，DINT CDAB 低字在前）。
    //    失败时原样返回真实通讯结果；writer 不自动重试、不重放。
    const std::vector<uint16_t> seqWords =
        codec::RegisterCodec::encodeInt32(req.requestSeq, kCDAB);
    return m_client->writeMultipleRegisters(
        static_cast<uint16_t>(cmdLayout.requestSeq.value()), seqWords);
}

}  // namespace plc_vnext::command
