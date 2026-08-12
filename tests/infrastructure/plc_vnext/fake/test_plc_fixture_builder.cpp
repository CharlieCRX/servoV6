// ============================================================================
// test_plc_fixture_builder.cpp —— Step 11 fake: PlcFixtureBuilder 测试（红）
// ============================================================================
// 依据《TDD实施文档》Step 11（11.1 绿 PlcFixtureBuilder.h）：
//   - makeValidTopologySnapshot：冻结 ABI（Magic/Schema/Revision/ConfigValid、A 组
//     X1/X2/SYN0、B 组禁用）与 makeVersionedPlcTopologyDump 解码结果一致。
//   - makeInvalidTopologySnapshot：可解码但 ConfigValid=false（属快照内容，非失败）。
//   - makeTrustedRuntimeSnapshot：16 槽位 + 2 龙门组全部 trusted，quality=Trusted。
//   - makeAxisSnapshot / makeGantryStatusSnapshot：字段与可信位逐项可辨。
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace plc_vnext::fake {
namespace {

using contracts::SnapshotQuality;

TEST(PlcFixtureBuilderTest, ValidTopology_MatchesFrozenAbi) {
    const auto snap = makeValidTopologySnapshot(/*revision=*/7);

    EXPECT_EQ(snap.header.magic, kFrozenTopologyMagic);
    EXPECT_EQ(snap.header.schemaVersion, kFrozenSchemaVersion);
    EXPECT_EQ(snap.header.revision, 7);
    EXPECT_TRUE(snap.header.configValid);
    EXPECT_EQ(snap.header.configErrorCode, 0);

    ASSERT_EQ(snap.groups.size(), static_cast<std::size_t>(kFixtureGroupCount));
    const auto& ga = snap.groups[0];
    EXPECT_TRUE(ga.valid);
    EXPECT_EQ(ga.groupCode, 0);
    ASSERT_EQ(ga.roles.size(), static_cast<std::size_t>(kFixtureRoleCount));

    // A 组：Role[0] X1 / Role[1] X2 / Role[5] SYN0 有效，其余预留 PlcAxisIndex=-1。
    EXPECT_TRUE(ga.roles[0].valid);
    EXPECT_EQ(ga.roles[0].plcAxisIndex, 0);
    EXPECT_EQ(ga.roles[0].motionMode, 1);
    EXPECT_TRUE(ga.roles[1].valid);
    EXPECT_EQ(ga.roles[1].motionMode, 2);
    EXPECT_FALSE(ga.roles[2].valid);
    EXPECT_EQ(ga.roles[2].plcAxisIndex, -1);
    EXPECT_TRUE(ga.roles[5].valid);
    EXPECT_EQ(ga.roles[5].plcAxisIndex, 13);
    EXPECT_EQ(ga.roles[5].axisClass, 2);   // 虚轴
    EXPECT_EQ(ga.roles[5].motionMode, 5);  // 龙门逻辑轴

    // B 组禁用。
    const auto& gb = snap.groups[1];
    EXPECT_FALSE(gb.valid);
    EXPECT_EQ(gb.groupCode, 1);
    for (const auto& role : gb.roles) {
        EXPECT_FALSE(role.valid);
        EXPECT_EQ(role.plcAxisIndex, -1);
    }
}

TEST(PlcFixtureBuilderTest, InvalidTopology_ConfigValidFalse) {
    const auto snap = makeInvalidTopologySnapshot();
    EXPECT_FALSE(snap.header.configValid);
    EXPECT_EQ(snap.header.configErrorCode, 0x08);
    // 其余头部字段仍为冻结 ABI（可解码，仅配置未就绪）。
    EXPECT_EQ(snap.header.magic, kFrozenTopologyMagic);
    EXPECT_EQ(snap.header.schemaVersion, kFrozenSchemaVersion);
    ASSERT_EQ(snap.groups.size(), static_cast<std::size_t>(kFixtureGroupCount));
}

TEST(PlcFixtureBuilderTest, TrustedRuntime_AllTrusted) {
    const auto snap = makeTrustedRuntimeSnapshot();
    EXPECT_EQ(snap.quality, SnapshotQuality::Trusted);

    ASSERT_EQ(snap.axes.size(), contracts::kRuntimeAxisCount);
    for (std::size_t i = 0; i < snap.axes.size(); ++i) {
        EXPECT_TRUE(snap.axes[i].trusted) << "axis slot " << i;
        EXPECT_EQ(snap.axes[i].slot, static_cast<int16_t>(i));
    }

    ASSERT_EQ(snap.gantry.size(), contracts::kRuntimeGroupCount);
    EXPECT_TRUE(snap.gantry[0].trusted);
    EXPECT_TRUE(snap.gantry[1].trusted);
}

TEST(PlcFixtureBuilderTest, AxisSnapshot_SetsFieldsAndTrust) {
    const auto a = makeAxisSnapshot(/*slot=*/3, /*manualSpeed=*/12.5f,
                                    /*positioningSpeed=*/80.0f,
                                    /*absPosition=*/1000.0f, /*relPosition=*/5.0f,
                                    /*motionState=*/2, /*motionLimit=*/1,
                                    /*alarmWord=*/0x0004, /*trusted=*/true);
    EXPECT_EQ(a.slot, 3);
    EXPECT_FLOAT_EQ(a.manualSpeed, 12.5f);
    EXPECT_FLOAT_EQ(a.positioningSpeed, 80.0f);
    EXPECT_FLOAT_EQ(a.absPosition, 1000.0f);
    EXPECT_FLOAT_EQ(a.relPosition, 5.0f);
    EXPECT_EQ(a.motionState, 2);
    EXPECT_EQ(a.motionLimit, 1);
    EXPECT_EQ(a.alarmWord, 0x0004u);
    EXPECT_TRUE(a.trusted);
}

TEST(PlcFixtureBuilderTest, AxisSnapshot_DefaultTrustedFalse_IsExplicit) {
    // 缺省 trusted=true；调用方需要"失败槽位"时显式传 false。
    EXPECT_TRUE(makeAxisSnapshot(0).trusted);
    EXPECT_FALSE(makeAxisSnapshot(0, 0, 0, 0, 0, 0, 0, 0, /*trusted=*/false).trusted);
}

TEST(PlcFixtureBuilderTest, GantrySnapshot_SetsFlagsAndTrust) {
    const auto s = makeGantryStatusSnapshot(/*g=*/0, /*state=*/3, /*ackSeq=*/9,
                                            /*commandResult=*/2,
                                            /*x1InGear=*/true, /*x2InGear=*/true,
                                            /*trusted=*/true);
    EXPECT_EQ(s.state, 3);
    EXPECT_EQ(s.ackSeq, 9);
    EXPECT_EQ(s.commandResult, 2);
    EXPECT_TRUE(s.x1InGear);
    EXPECT_TRUE(s.x2InGear);
    EXPECT_FALSE(s.readyToCouple);  // 已啮合 → 不再可建立
    EXPECT_TRUE(s.trusted);
}

}  // namespace
}  // namespace plc_vnext::fake
