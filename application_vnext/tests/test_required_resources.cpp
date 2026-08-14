// ============================================================================
// test_required_resources.cpp —— Phase 0：requiredResources 资源集合单测
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 0 验收：
//   - 单轴命令只占一个轴资源；
//   - 逻辑轴 X 运动返回 {gantry:A:0, axis:A:X, axis:A:X1, axis:A:X2}；
//   - 龙门生命周期返回 {gantry:A:0}；
//   - 急停/释放急停不占资源（空集合）。
// ============================================================================
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "application_vnext/control/OperationState.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace application_vnext::control {
namespace {

using domain_vnext::model::AxisFunction;

plc_vnext::contracts::TopologySnapshot dummyTopology() {
    plc_vnext::contracts::TopologySnapshot topo;
    // Phase 0 暂不解析 slot；空拓扑即可验证资源命名。
    return topo;
}

std::vector<std::string> keys(const std::vector<ControlResource>& rs) {
    std::vector<std::string> k;
    for (const auto& r : rs) k.push_back(r.key);
    return k;
}

TEST(RequiredResourcesTest, SingleAxisYCommandOccupiesOneAxisResource) {
    ControlCommand cmd;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    cmd.target.function = AxisFunction::Y;
    cmd.action = ControlAction::StartAbsMove;

    auto rs = requiredResources(cmd, dummyTopology());
    ASSERT_EQ(rs.size(), 1u);
    EXPECT_EQ(rs[0].key, "axis:A:Y");
}

TEST(RequiredResourcesTest, LogicalAxisXMoveOccupiesGantryPlusAxisXMembers) {
    ControlCommand cmd;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    cmd.target.function = AxisFunction::X;
    cmd.action = ControlAction::StartRelMove;

    auto rs = requiredResources(cmd, dummyTopology());
    EXPECT_EQ(keys(rs), (std::vector<std::string>{
        "gantry:A:0", "axis:A:X", "axis:A:X1", "axis:A:X2"}));
}

TEST(RequiredResourcesTest, MemberAxisX1MoveIsSingleAxisResource) {
    ControlCommand cmd;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    cmd.target.function = AxisFunction::X1;
    cmd.action = ControlAction::StartJogForward;

    auto rs = requiredResources(cmd, dummyTopology());
    ASSERT_EQ(rs.size(), 1u);
    EXPECT_EQ(rs[0].key, "axis:A:X1");
}

TEST(RequiredResourcesTest, GantryLifecycleOccupiesFullResourceSet) {
    ControlCommand cmd;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    cmd.action = ControlAction::GantryEnableAndCouple;

    auto rs = requiredResources(cmd, dummyTopology());
    // 与龙门期间成员轴结构性互斥：必须占龙门组 + 逻辑轴 + 两个成员轴
    EXPECT_EQ(keys(rs), (std::vector<std::string>{
        "gantry:A:0", "axis:A:X", "axis:A:X1", "axis:A:X2"}));
}

TEST(RequiredResourcesTest, GantryDecoupleOccupiesFullResourceSet) {
    ControlCommand cmd;
    cmd.target.group = plc_vnext::contracts::PlcGroupIndex(1);
    cmd.action = ControlAction::GantryDecoupleAndDisable;

    auto rs = requiredResources(cmd, dummyTopology());
    EXPECT_EQ(keys(rs), (std::vector<std::string>{
        "gantry:B:1", "axis:B:X", "axis:B:X1", "axis:B:X2"}));
}

TEST(RequiredResourcesTest, EmergencyStopOccupiesNoResource) {
    ControlCommand cmd;
    cmd.action = ControlAction::EmergencyStop;
    EXPECT_TRUE(requiredResources(cmd, dummyTopology()).empty());

    ControlCommand release;
    release.action = ControlAction::ReleaseEmergencyStop;
    EXPECT_TRUE(requiredResources(release, dummyTopology()).empty());
}

TEST(RequiredResourcesTest, StopCommandsDoNotAcquireNewLease) {
    // StopMotion/StopJog 不申请新租约：定位已有会话调 requestStop()，
    // 即使目标资源已被该会话占用也必须可执行（不能因资源被占而被拒）。
    ControlCommand stopMotion;
    stopMotion.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    stopMotion.target.function = AxisFunction::Y;
    stopMotion.action = ControlAction::StopMotion;
    EXPECT_TRUE(requiredResources(stopMotion, dummyTopology()).empty());

    ControlCommand stopJog;
    stopJog.target.group = plc_vnext::contracts::PlcGroupIndex(0);
    stopJog.target.function = AxisFunction::X1;
    stopJog.action = ControlAction::StopJog;
    EXPECT_TRUE(requiredResources(stopJog, dummyTopology()).empty());
}

TEST(RequiredResourcesTest, ResourceKeysAreComparableAndOrdered) {
    ControlResource a = ControlResource::ofAxis({plc_vnext::contracts::PlcGroupIndex(0),
                                                 AxisFunction::Y});
    ControlResource b = ControlResource::ofAxis({plc_vnext::contracts::PlcGroupIndex(0),
                                                 AxisFunction::X});
    EXPECT_NE(a, b);
    EXPECT_TRUE(b < a);  // "axis:A:X" < "axis:A:Y"
    EXPECT_TRUE(a < b || b < a);  // 严格全序
}

}  // namespace
}  // namespace application_vnext::control
