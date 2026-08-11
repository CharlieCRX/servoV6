// ============================================================================
// GantryStatusReader.cpp —— Step 7 telemetry: 龙门状态解码实现
// ============================================================================
#include "infrastructure/plc_vnext/telemetry/GantryStatusReader.h"

#include "infrastructure/plc_vnext/codec/EndianPolicy.h"
#include "infrastructure/plc_vnext/codec/RegisterCodec.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"

namespace plc_vnext::telemetry {
namespace {

constexpr codec::EndianPolicy kCDAB{codec::ByteOrder::BigEndian,
                                    codec::WordOrder::LowWordFirst};

inline bool bitAt(uint16_t word, int bit) { return ((word >> bit) & 1u) != 0u; }

bool readFloat(const codec::RawRegisterBlock& blk, int addr, float& out) {
    auto w = blk.getWords(addr, 2);
    if (!w) return false;
    return !codec::RegisterCodec::decodeFloat(*w, kCDAB, out).has_value();
}

bool readInt16(const codec::RawRegisterBlock& blk, int addr, int16_t& out) {
    auto w = blk.getWords(addr, 1);
    if (!w) return false;
    return !codec::RegisterCodec::decodeInt16(*w, out).has_value();
}

bool readInt32(const codec::RawRegisterBlock& blk, int addr, int32_t& out) {
    auto w = blk.getWords(addr, 2);
    if (!w) return false;
    return !codec::RegisterCodec::decodeInt32(*w, kCDAB, out).has_value();
}

}  // namespace

contracts::GantryStatusSnapshot GantryStatusReader::decode(
    const codec::RawRegisterBlock& block, int group) {
    contracts::GantryStatusSnapshot s;
    const int base = layout::gantryStatusBase(group).value();

    bool ok = true;
    ok &= readInt16(block, base + 0, s.state);
    ok &= readInt16(block, base + 1, s.internalStep);
    ok &= readInt32(block, base + 2, s.ackSeq);
    ok &= readInt16(block, base + 4, s.commandResult);
    ok &= readInt16(block, base + 5, s.commandErrorCode);

    auto flags = block.getWords(base + 6, 1);
    if (flags) {
        const uint16_t w = (*flags)[0];
        s.readyToCouple = bitAt(w, 0);
        s.readyToDecouple = bitAt(w, 1);
        s.memberControlAllowed = bitAt(w, 2);
        s.logicalControlAllowed = bitAt(w, 3);
        s.x1InGear = bitAt(w, 4);
        s.x2InGear = bitAt(w, 5);
    } else {
        ok = false;
    }

    ok &= readFloat(block, base + 7, s.x1Position);
    ok &= readFloat(block, base + 9, s.x2Position);
    ok &= readFloat(block, base + 11, s.logicalPosition);
    ok &= readFloat(block, base + 13, s.skew);

    auto faultW = block.getWords(base + 15, 1);
    if (faultW) {
        s.fault = bitAt((*faultW)[0], 0);
    } else {
        ok = false;
    }
    ok &= readInt16(block, base + 16, s.faultCode);
    ok &= readInt16(block, base + 17, s.reserved);

    s.trusted = ok;
    return s;
}

}  // namespace plc_vnext::telemetry
