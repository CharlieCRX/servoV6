// ============================================================================
// test_topology_decoder.cpp —— Step 6 topology: TopologyDecoder
// ============================================================================
// 红：引用 infrastructure/plc_vnext/topology/TopologyDecoder.h 与
//     contracts/TopologySnapshot.h；对应 6.1 红用例：
//       DecodesHeader_AndGroups / DecodesRole_Bindings /
//       DecodesGroup_ValidFlag / RawBlockTooShort_ReturnsError /
//       InvalidRole_PlcAxisIndex_PreservedUnused
// ============================================================================
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/layout/AxisTopologyLayout.h"
#include "infrastructure/plc_vnext/topology/TopologyDecoder.h"
#include "tests/infrastructure/plc_vnext/support/TopologyFixture.h"

namespace plc_vnext::topology {
namespace {

TEST(TopologyDecoderTest, DecodesHeader_AndGroups) {
    auto regs = test::makeValidTopologyRegisters(7);
    std::string diag;
    auto snap = TopologyDecoder::decode(regs, diag);
    ASSERT_TRUE(snap.has_value()) << diag;

    EXPECT_EQ(snap->header.magic, test::kFixtureMagic);
    EXPECT_EQ(snap->header.schemaVersion, 1);
    EXPECT_EQ(snap->header.revision, 7);
    EXPECT_TRUE(snap->header.configValid);
    EXPECT_EQ(snap->header.configErrorCode, 0);

    ASSERT_EQ(snap->groups.size(), 2u);          // 组数量来自 layout schema 常量
    EXPECT_TRUE(snap->groups[0].valid);
    EXPECT_EQ(snap->groups[0].groupCode, 0);
    EXPECT_EQ(snap->groups[0].roles.size(), 8u); // 角色数量来自 layout schema 常量
}

TEST(TopologyDecoderTest, DecodesRole_Bindings) {
    auto regs = test::makeValidTopologyRegisters();
    std::string diag;
    auto snap = TopologyDecoder::decode(regs, diag);
    ASSERT_TRUE(snap.has_value());

    // A 组 Role[0] X1
    const auto& x1 = snap->groups[0].roles[0];
    EXPECT_TRUE(x1.valid);
    EXPECT_EQ(x1.plcAxisIndex, 0);
    EXPECT_EQ(x1.motorNo, 1);
    EXPECT_EQ(x1.axisClass, 0);
    EXPECT_EQ(x1.motionMode, 1);   // 龙门X1

    // A 组 Role[5] SYN0（X 逻辑轴）
    const auto& syn0 = snap->groups[0].roles[5];
    EXPECT_TRUE(syn0.valid);
    EXPECT_EQ(syn0.plcAxisIndex, 13);
    EXPECT_EQ(syn0.motorNo, 0);
    EXPECT_EQ(syn0.axisClass, 2);  // 虚轴
    EXPECT_EQ(syn0.motionMode, 5); // 龙门逻辑轴
}

TEST(TopologyDecoderTest, DecodesGroup_ValidFlag) {
    auto regs = test::makeValidTopologyRegisters();
    // 默认 Group[1].Valid=false
    {
        std::string diag;
        auto snap = TopologyDecoder::decode(regs, diag);
        ASSERT_TRUE(snap.has_value());
        EXPECT_FALSE(snap->groups[1].valid);
    }
    // 置为 true 后应解析正确
    test::setGroupValid(regs, 1, true, true, 1);
    std::string diag;
    auto snap = TopologyDecoder::decode(regs, diag);
    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap->groups[1].valid);
    EXPECT_EQ(snap->groups[1].groupCode, 1);
}

TEST(TopologyDecoderTest, InvalidRole_PlcAxisIndex_PreservedUnused) {
    // 现场不保证无效角色 PlcAxisIndex=-1（可能残留任意值如 16800）。解码须机械
    // 保留原值，且不依赖 -1 作任何判断（有效性以 Valid 位为准，见 Validator）。
    auto regs = test::makeValidTopologyRegisters();
    // 把 A 组一个无效角色的 PlcAxisIndex 改成残留脏值
    test::setRole(regs, 0, 2, false, 16800, 0, 0, 0, 0);
    std::string diag;
    auto snap = TopologyDecoder::decode(regs, diag);
    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(snap->groups[0].roles[2].valid);
    EXPECT_EQ(snap->groups[0].roles[2].plcAxisIndex, 16800);  // 原样保留
}

TEST(TopologyDecoderTest, RawBlockTooShort_ReturnsError) {
    // 少于 178 字必须判定为解码失败（TopologyReadError::DecodeFailed 语义），不抛异常
    std::vector<uint16_t> shortBlock(static_cast<std::size_t>(layout::topologyTotalWords()) - 1, 0);
    std::string diag;
    auto snap = TopologyDecoder::decode(shortBlock, diag);
    EXPECT_FALSE(snap.has_value());
    EXPECT_FALSE(diag.empty());
}

}  // namespace
}  // namespace plc_vnext::topology
