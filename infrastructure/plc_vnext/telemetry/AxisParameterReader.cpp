// ============================================================================
// AxisParameterReader.cpp —— 阶段3 telemetry: 参数区读取实现
// ============================================================================
#include "infrastructure/plc_vnext/telemetry/AxisParameterReader.h"

#include <utility>
#include <vector>

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"

namespace plc_vnext::telemetry {
namespace {

// 与 PlcAxisCommandWriter 相同的写端字序：低字在低地址（汇川 CDAB）。
constexpr codec::EndianPolicy kPlcEndian{codec::ByteOrder::BigEndian,
                                         codec::WordOrder::LowWordFirst};

/// 读取一段连续保持寄存器；任一失败或长度不足 → 返回 false。
bool readWords(transport::IModbusClientPtr& client, layout::HoldingAddress addr,
               int count, std::vector<uint16_t>& out) {
    auto res = client->readHoldingRegisters(static_cast<uint16_t>(addr.value()),
                                            static_cast<uint16_t>(count), out);
    return res.ok() && out.size() == static_cast<std::size_t>(count);
}

bool decodeFloat(const std::vector<uint16_t>& w, float& out) {
    return !codec::RegisterCodec::decodeFloat(w, kPlcEndian, out).has_value();
}

}  // namespace

AxisParameterReader::AxisParameterReader(transport::IModbusClientPtr client)
    : m_client(std::move(client)) {}

contracts::AxisParameterSnapshot AxisParameterReader::read(int slot) {
    contracts::AxisParameterSnapshot s;
    s.slot = static_cast<int16_t>(slot);

    if (slot < contracts::PlcAxisSlot::kMin || slot > contracts::PlcAxisSlot::kMax) {
        s.trusted = false;
        return s;
    }

    std::vector<uint16_t> w;
    bool ok = true;

    ok = ok && readWords(m_client, layout::relZeroRecord(slot), 2, w) &&
               decodeFloat(w, s.relZeroRecord);
    ok = ok && readWords(m_client, layout::absMoveDistance(slot), 2, w) &&
               decodeFloat(w, s.absMoveDistance);
    ok = ok && readWords(m_client, layout::relMoveDistance(slot), 2, w) &&
               decodeFloat(w, s.relMoveDistance);
    ok = ok && readWords(m_client, layout::softNegLimit(slot), 2, w) &&
               decodeFloat(w, s.softNegLimit);
    ok = ok && readWords(m_client, layout::softPosLimit(slot), 2, w) &&
               decodeFloat(w, s.softPosLimit);
    ok = ok && readWords(m_client, layout::softLimitControl(slot), 1, w) &&
               (s.softLimitControl = w[0], true);

    s.trusted = ok;
    return s;
}

}  // namespace plc_vnext::telemetry
