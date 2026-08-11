// ============================================================================
// PlcTopologyReader.cpp —— Step 6 topology: 双读实现
// ============================================================================
#include "infrastructure/plc_vnext/topology/PlcTopologyReader.h"

#include <cstdint>
#include <sstream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"
#include "infrastructure/plc_vnext/layout/ReadPlanBuilder.h"
#include "infrastructure/plc_vnext/topology/TopologyDecoder.h"
#include "infrastructure/plc_vnext/topology/TopologyValidator.h"

namespace plc_vnext::topology {
namespace {

constexpr codec::EndianPolicy kCDAB{codec::ByteOrder::BigEndian,
                                    codec::WordOrder::LowWordFirst};

// 头部寄存器数量（D1400..D1405：Magic/Schema/Reserved/Revision）。
constexpr int kHeaderWords = 6;

}  // namespace

using ReadResult = contracts::ReadResult<contracts::TopologySnapshot>;

PlcTopologyReader::PlcTopologyReader(transport::IModbusClientPtr client)
    : m_client(std::move(client)) {}

bool PlcTopologyReader::readRevision(int32_t& out) {
    std::vector<uint16_t> head;
    auto res = m_client->readHoldingRegisters(layout::topologyMagic().value(),
                                              kHeaderWords, head);
    if (!res.ok() || head.size() < static_cast<std::size_t>(kHeaderWords)) {
        return false;
    }
    const int revOff = layout::topologyRevision().value() -
                       layout::topologyMagic().value();  // D1404..D1405 -> 4..5
    const std::span<const uint16_t> revSpan(head.data() + revOff, 2);
    return !codec::RegisterCodec::decodeInt32(revSpan, kCDAB, out).has_value();
}

ReadResult PlcTopologyReader::makeFailure(TopologyReadError error,
                                          const std::string& diagnostic) {
    switch (error) {
        case TopologyReadError::Changed:
            return ReadResult::failure(ReadResult::FailureKind::RevisionChanged,
                                       diagnostic);
        case TopologyReadError::DecodeFailed:
            return ReadResult::failure(ReadResult::FailureKind::Decode, diagnostic);
        case TopologyReadError::TransportFailed:
        default:
            return ReadResult::failure(ReadResult::FailureKind::Transport, diagnostic);
    }
}

ReadResult PlcTopologyReader::read() {
    // 1. 首读 Header：取得 Revision
    int32_t revisionA = 0;
    if (!readRevision(revisionA)) {
        return makeFailure(TopologyReadError::TransportFailed,
                           "PlcTopologyReader: failed to read header");
    }

    // 2. 通过 Step 4 计划能力把整块 Body 拆为合法 FC03 分片（每片 ≤ 125）：
    //      D1400..D1524 (125) + D1525..D1577 (53)
    //    按序读取并拼回 178 字，再整体解码。
    const layout::ReadPlan plan = layout::ReadPlanBuilder::buildHolding(
        {{layout::topologyMagic().value(), layout::topologyTotalWords()}});
    std::vector<uint16_t> words;
    words.reserve(static_cast<std::size_t>(layout::topologyTotalWords()));
    for (const layout::ReadRange& range : plan) {
        if (range.area != layout::ReadArea::Holding) {
            return makeFailure(TopologyReadError::DecodeFailed,
                               "PlcTopologyReader: topology plan contains non-holding read");
        }
        std::vector<uint16_t> chunk;
        auto res = m_client->readHoldingRegisters(
            static_cast<uint16_t>(range.start), static_cast<uint16_t>(range.count), chunk);
        if (!res.ok()) {
            return makeFailure(TopologyReadError::TransportFailed,
                               "PlcTopologyReader: body chunk read failed: " + res.diagnostic);
        }
        words.insert(words.end(), chunk.begin(), chunk.end());
    }
    if (words.size() < static_cast<std::size_t>(layout::topologyTotalWords())) {
        return makeFailure(TopologyReadError::DecodeFailed,
                           "PlcTopologyReader: topology body too short");
    }
    std::string diag;
    auto snap = TopologyDecoder::decode(words, diag);
    if (!snap) {
        return makeFailure(TopologyReadError::DecodeFailed, std::move(diag));
    }

    // 3. 再次读取 Header：校验 Revision 稳定
    int32_t revisionB = 0;
    if (!readRevision(revisionB)) {
        return makeFailure(TopologyReadError::TransportFailed,
                           "PlcTopologyReader: failed to re-read header");
    }
    if (revisionA != revisionB) {
        return makeFailure(TopologyReadError::Changed,
                           "PlcTopologyReader: Revision changed during double read");
    }

    // 4. 接入客户端解码安全校验（阻塞项 2）。ConfigValid=false 不属于本校验
    //    范围（快照原样保留）；客户端安全问题 → Decode 失败并保留诊断。
    const auto issues = TopologyValidator::validate(*snap);
    if (!issues.empty()) {
        std::ostringstream oss;
        oss << "PlcTopologyReader: topology validation failed, " << issues.size()
            << " issue(s):";
        for (const auto& issue : issues) {
            oss << " [" << issue.context << "] " << issue.message << ";";
        }
        return makeFailure(TopologyReadError::DecodeFailed, oss.str());
    }

    // 5. 成功：返回解码快照（含原样保留的 ConfigValid / ConfigErrorCode）
    return ReadResult::success(std::move(*snap));
}

}  // namespace plc_vnext::topology
