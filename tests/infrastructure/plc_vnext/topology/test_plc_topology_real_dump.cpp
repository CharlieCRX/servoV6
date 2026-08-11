// ============================================================================
// test_plc_topology_real_dump.cpp —— M2 证据：版本化真实现场转储逐字段对拍
// ============================================================================
// 依据一次 `python plc_read_validate.py --only topology` 的真实输出（Magic=
// 0x013527C6, Schema=1, Revision=0, ConfigValid=true, A 组 Role0/1/5 有效，
// 其余角色与 B 组无效 PlcAxisIndex=-1，含 A 组 Role[3] 与 B 组 Role[0] 的未初始化
// 残留），经 test::makeVersionedPlcTopologyDump() 重建 178 字后：
//   1. 逐字段断言 Header / 两个 Group / 全部 16 个 Role 的关键字段（含脏值保留）；
//   2. 断言该真实转储经 TopologyValidator 校验 0 issue（现场数据被正确接受）。
// 这是 M2"真实 PLC topology 对拍一致"的最后一项证据。
// ============================================================================
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"
#include "infrastructure/plc_vnext/topology/TopologyDecoder.h"
#include "infrastructure/plc_vnext/topology/TopologyValidator.h"
#include "tests/infrastructure/plc_vnext/support/TopologyFixture.h"

namespace plc_vnext::topology {
namespace {

using contracts::TopologyRole;

void expectRole(const TopologyRole& role, bool valid, bool hmi, int16_t axis,
                int16_t motor, int32_t cls, int32_t unit, int32_t mode,
                int16_t reserved) {
    EXPECT_EQ(role.valid, valid);
    EXPECT_EQ(role.hmiVisible, hmi);
    EXPECT_EQ(role.plcAxisIndex, axis);
    EXPECT_EQ(role.motorNo, motor);
    EXPECT_EQ(role.axisClass, cls);
    EXPECT_EQ(role.unitType, unit);
    EXPECT_EQ(role.motionMode, mode);
    EXPECT_EQ(role.reserved, reserved);
}

TEST(PlcTopologyRealDump, DecodesVersionedDump_FieldByField) {
    auto dump = test::makeVersionedPlcTopologyDump();
    std::string diag;
    auto snap = TopologyDecoder::decode(dump, diag);
    ASSERT_TRUE(snap.has_value()) << diag;

    // ---- Header ----
    EXPECT_EQ(snap->header.magic, test::kFixtureMagic);
    EXPECT_EQ(snap->header.schemaVersion, 1);
    EXPECT_EQ(snap->header.revision, 0);
    EXPECT_EQ(snap->header.configCRC, 0u);
    EXPECT_TRUE(snap->header.configValid);
    EXPECT_EQ(snap->header.configErrorCode, 0);

    // ---- Groups ----
    ASSERT_EQ(snap->groups.size(), 2u);
    const auto& g0 = snap->groups[0];
    EXPECT_TRUE(g0.valid);
    EXPECT_TRUE(g0.hmiVisible);
    EXPECT_EQ(g0.groupCode, 0);
    ASSERT_EQ(g0.roles.size(), 8u);
    const auto& g1 = snap->groups[1];
    EXPECT_FALSE(g1.valid);
    EXPECT_FALSE(g1.hmiVisible);
    EXPECT_EQ(g1.groupCode, 1);
    ASSERT_EQ(g1.roles.size(), 8u);

    // ---- A 组 Role 字段 ----
    expectRole(g0.roles[0], true, true, 0, 1, 0, 0, 1, 0);        // X1 龙门X1
    expectRole(g0.roles[1], true, true, 1, 2, 0, 0, 2, 0);        // X2 龙门X2
    expectRole(g0.roles[2], false, false, -1, 0, 0, 0, 0, 0);
    // Role[3]：现场未初始化残留 UnitType=851971, MotionMode=131072 须原样保留
    expectRole(g0.roles[3], false, false, -1, 0, 0, 851971, 131072, 0);
    expectRole(g0.roles[4], false, false, -1, 0, 0, 0, 0, 0);
    expectRole(g0.roles[5], true, true, 13, 0, 2, 0, 5, 0);       // SYN0 龙门逻辑轴
    expectRole(g0.roles[6], false, false, -1, 0, 0, 0, 0, 0);
    expectRole(g0.roles[7], false, false, -1, 0, 0, 0, 0, 0);

    // ---- B 组 Role 字段 ----
    // Role[0]：现场未初始化残留 MotionMode=16800, Reserved=16800 须原样保留
    expectRole(g1.roles[0], false, false, -1, 0, 0, 0, 16800, 16800);
    for (int i = 1; i < 8; ++i) {
        expectRole(g1.roles[i], false, false, -1, 0, 0, 0, 0, 0);
    }
}

TEST(PlcTopologyRealDump, Validator_AcceptsRealDump_ZeroIssues) {
    auto dump = test::makeVersionedPlcTopologyDump();
    std::string diag;
    auto snap = TopologyDecoder::decode(dump, diag);
    ASSERT_TRUE(snap.has_value()) << diag;

    // 真实现场数据（含禁用组/无效角色脏残留）必须被客户端校验接受，0 issue。
    const auto issues = TopologyValidator::validate(*snap);
    EXPECT_TRUE(issues.empty()) << "real dump must produce no client-side issues";
    for (const auto& issue : issues) {
        std::cerr << "[" << issue.context << "] " << issue.message << "\n";
    }
}

}  // namespace
}  // namespace plc_vnext::topology
