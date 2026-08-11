// ============================================================================
// test_gantry_status_reader.cpp —— Step 7 telemetry: GantryStatusReader
// ============================================================================
// 红：引用 infrastructure/plc_vnext/telemetry/GantryStatusReader.h；对应 7.1
// 红用例：DecodesState_AndAckSeq / DecodesControlAllowBits /
//          DecodesSkew_AndPositions / DecodesFault
// 字段偏移来自 layout::GantryLayout 与地址表 §8，端序 CDAB。
// ============================================================================
#include <cstdint>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/codec/RawRegisterBlock.h"
#include "infrastructure/plc_vnext/layout/GantryLayout.h"
#include "infrastructure/plc_vnext/telemetry/GantryStatusReader.h"
#include "tests/infrastructure/plc_vnext/support/TelemetryFixture.h"

namespace plc_vnext::telemetry {
namespace {

// 龙门状态块向量下标 0 == D190（gantryStatusBase(0)）。
inline int gidx(int group, int offset) {
    return layout::gantryStatusBase(group).value() -
           layout::gantryStatusBase(0).value() + offset;
}

codec::RawRegisterBlock toGantryBlock(std::vector<uint16_t> words) {
    return codec::RawRegisterBlock(layout::gantryStatusBase(0).value(),
                                   std::move(words), 0, {}, 0);
}

TEST(GantryStatusReaderTest, DecodesState_AndAckSeq) {
    auto regs = test::makeGantryBlock();
    test::writeInt16(regs, gidx(0, 0), 3);           // State=已联动
    test::writeInt16(regs, gidx(0, 1), 80);          // InternalStep=已联动
    test::writeDintLowWordFirst(regs, gidx(0, 2), 9);// AckSeq=9
    test::writeInt16(regs, gidx(0, 4), 2);           // CommandResult=成功

    auto s = GantryStatusReader::decode(toGantryBlock(std::move(regs)), 0);
    ASSERT_TRUE(s.trusted);
    EXPECT_EQ(s.state, 3);
    EXPECT_EQ(s.internalStep, 80);
    EXPECT_EQ(s.ackSeq, 9);
    EXPECT_EQ(s.commandResult, 2);
}

TEST(GantryStatusReaderTest, DecodesControlAllowBits) {
    auto regs = test::makeGantryBlock();
    uint16_t w = 0;
    test::setBit(w, 0, true);   // ReadyToCouple
    test::setBit(w, 2, true);   // MemberControlAllowed
    test::setBit(w, 3, true);   // LogicalControlAllowed
    test::writeWord(regs, gidx(0, 6), w);

    auto s = GantryStatusReader::decode(toGantryBlock(std::move(regs)), 0);
    ASSERT_TRUE(s.trusted);
    EXPECT_TRUE(s.readyToCouple);
    EXPECT_FALSE(s.readyToDecouple);
    EXPECT_TRUE(s.memberControlAllowed);
    EXPECT_TRUE(s.logicalControlAllowed);
    EXPECT_FALSE(s.x1InGear);
    EXPECT_FALSE(s.x2InGear);
}

TEST(GantryStatusReaderTest, DecodesSkew_AndPositions) {
    auto regs = test::makeGantryBlock();
    test::writeFloat(regs, gidx(0, 7), 150.0f);
    test::writeFloat(regs, gidx(0, 9), 150.5f);
    test::writeFloat(regs, gidx(0, 11), 150.25f);
    test::writeFloat(regs, gidx(0, 13), -0.5f);  // Skew = X1 - X2

    auto s = GantryStatusReader::decode(toGantryBlock(std::move(regs)), 0);
    ASSERT_TRUE(s.trusted);
    EXPECT_FLOAT_EQ(s.x1Position, 150.0f);
    EXPECT_FLOAT_EQ(s.x2Position, 150.5f);
    EXPECT_FLOAT_EQ(s.logicalPosition, 150.25f);
    EXPECT_FLOAT_EQ(s.skew, -0.5f);
}

TEST(GantryStatusReaderTest, DecodesFault) {
    auto regs = test::makeGantryBlock();
    uint16_t fw = 0;
    test::setBit(fw, 0, true);                     // Fault
    test::writeWord(regs, gidx(0, 15), fw);
    test::writeInt16(regs, gidx(0, 16), 151);      // FaultCode=运行偏差持续超限

    auto s = GantryStatusReader::decode(toGantryBlock(std::move(regs)), 0);
    ASSERT_TRUE(s.trusted);
    EXPECT_TRUE(s.fault);
    EXPECT_EQ(s.faultCode, 151);
}

}  // namespace
}  // namespace plc_vnext::telemetry
