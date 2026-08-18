// ============================================================================
// AxisSnapshotDecoder.cpp —— Step 7 telemetry: 单槽位解码实现
// ============================================================================
#include "infrastructure/plc_vnext/telemetry/AxisSnapshotDecoder.h"

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/AxisSlotRegisterLayout.h"

namespace plc_vnext::telemetry {
namespace {

// 汇川 H5U 默认端序：CDAB（BigEndian + LowWordFirst）。
constexpr codec::EndianPolicy kCDAB{codec::ByteOrder::BigEndian,
                                    codec::WordOrder::LowWordFirst};

// 读取低字在前的 REAL；失败返回 false。
bool readFloat(const codec::RawRegisterBlock& blk, int addr, float& out) {
    auto w = blk.getWords(addr, 2);
    if (!w) return false;
    return !codec::RegisterCodec::decodeFloat(*w, kCDAB, out).has_value();
}

// 读取单个 INT 字；失败返回 false。
bool readInt16(const codec::RawRegisterBlock& blk, int addr, int16_t& out) {
    auto w = blk.getWords(addr, 1);
    if (!w) return false;
    return !codec::RegisterCodec::decodeInt16(*w, out).has_value();
}

}  // namespace

contracts::AxisRuntimeSnapshot AxisSnapshotDecoder::decode(
    const codec::RawRegisterBlock& block, int slot) {
    contracts::AxisRuntimeSnapshot snap;
    snap.slot = static_cast<int16_t>(slot);

    bool ok = true;
    ok &= readFloat(block, layout::manualSpeed(slot).value(), snap.manualSpeed);
    ok &= readFloat(block, layout::positioningSpeed(slot).value(), snap.positioningSpeed);
    ok &= readFloat(block, layout::absPosition(slot).value(), snap.absPosition);
    ok &= readFloat(block, layout::relPosition(slot).value(), snap.relPosition);
    ok &= readInt16(block, layout::motionState(slot).value(), snap.motionState);
    ok &= readInt16(block, layout::motionLimit(slot).value(), snap.motionLimit);
    auto alarm = block.getWords(layout::alarmWord(slot).value(), 1);
    if (alarm) {
        snap.alarmWord = (*alarm)[0];  // WORD 原样保留
    } else {
        ok = false;
    }

    snap.trusted = ok;
    return snap;
}

contracts::AxisParameterSnapshot AxisSnapshotDecoder::decodeParams(
    const codec::RawRegisterBlock& block, int slot) {
    contracts::AxisParameterSnapshot s;
    s.slot = static_cast<int16_t>(slot);

    bool ok = true;
    ok &= readFloat(block, layout::relZeroRecord(slot).value(), s.relZeroRecord);
    ok &= readFloat(block, layout::absMoveDistance(slot).value(), s.absMoveDistance);
    ok &= readFloat(block, layout::relMoveDistance(slot).value(), s.relMoveDistance);
    ok &= readFloat(block, layout::softNegLimit(slot).value(), s.softNegLimit);
    ok &= readFloat(block, layout::softPosLimit(slot).value(), s.softPosLimit);
    auto c = block.getWords(layout::softLimitControl(slot).value(), 1);
    if (c) {
        s.softLimitControl = (*c)[0];  // WORD 原样保留
    } else {
        ok = false;
    }

    s.trusted = ok;
    return s;
}

}  // namespace plc_vnext::telemetry
