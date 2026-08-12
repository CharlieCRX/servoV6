// ============================================================================
// test_system_boot.cpp —— P3 system: AxisRegistry / GroupModel / AxisSystem / SystemBoot
// ============================================================================
// P3 验证点（设计稿 §8）：从 TopologySnapshot 动态建轴、A/B 分组、HmiVisible
// 展示判定、重复/非法配置降级锁定。
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/AxisKey.h"
#include "domain_vnext/model/AxisCommand.h"
#include "domain_vnext/state/CommandOutbox.h"
#include "domain_vnext/system/AxisSystem.h"
#include "domain_vnext/system/SystemBoot.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace domain_vnext::system {
namespace {

using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::contracts::PlcGroupIndex;
using plc_vnext::contracts::TopologyGroup;
using plc_vnext::contracts::TopologyRole;
using plc_vnext::contracts::TopologySnapshot;

// ---------- 测试夹具构造 ----------

TopologyRole role(bool valid, bool hmi, int idx, int motionMode = 3) {
    TopologyRole r;
    r.valid = valid;
    r.hmiVisible = hmi;
    r.plcAxisIndex = idx;
    r.motorNo = 0;
    r.axisClass = 0;
    r.motionMode = motionMode;
    return r;
}

TopologyGroup group(bool valid, bool hmi,
                    std::vector<TopologyRole> roles) {
    TopologyGroup g;
    g.valid = valid;
    g.hmiVisible = hmi;
    g.groupCode = 0;
    g.roles = std::move(roles);
    return g;
}

/// 8 槽位 ABI：Role[0..5] 绑定功能，Role[6..7] 未映射（合法跳过）。
std::vector<TopologyRole> fullRoleBlock(int base, bool hmi = true) {
    std::vector<TopologyRole> roles;
    for (int i = 0; i < 8; ++i) {
        roles.push_back(role(true, hmi, base + i));
    }
    return roles;
}

// ---------- ① 动态建轴 + A/B 分组 + 功能绑定 ----------

TEST(SystemBootTest, BootValidTopology_RegistersAxesByRoleConvention) {
    TopologySnapshot topo;
    topo.header.configValid = true;
    topo.groups.push_back(group(true, true, fullRoleBlock(0)));
    topo.groups.push_back(group(true, true, fullRoleBlock(8)));

    AxisSystem sys;
    const auto res = SystemBoot::initialize(sys, topo);

    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.degraded);
    EXPECT_TRUE(res.configValid);
    EXPECT_EQ(res.registeredAxes, 12u);  // 2 组 x 6 功能，Role 6/7 未映射跳过
    EXPECT_EQ(sys.totalAxes(), 12u);

    const auto g0 = PlcGroupIndex(0);
    const auto g1 = PlcGroupIndex(1);
    EXPECT_EQ(sys.group(g0).axisCount(), 6u);
    EXPECT_EQ(sys.group(g1).axisCount(), 6u);
    EXPECT_TRUE(sys.group(g0).isReady());
    EXPECT_TRUE(sys.group(g1).isReady());

    // 功能 -> 槽位映射遵循 Role 约定（Role[0]=X1 ... Role[5]=X）
    EXPECT_EQ(sys.find({g0, model::AxisFunction::X1})->slot(),
              *PlcAxisSlot::tryCreate(0));
    EXPECT_EQ(sys.find({g0, model::AxisFunction::X2})->slot(),
              *PlcAxisSlot::tryCreate(1));
    EXPECT_EQ(sys.find({g0, model::AxisFunction::Y})->slot(),
              *PlcAxisSlot::tryCreate(2));
    EXPECT_EQ(sys.find({g0, model::AxisFunction::Z})->slot(),
              *PlcAxisSlot::tryCreate(3));
    EXPECT_EQ(sys.find({g0, model::AxisFunction::R})->slot(),
              *PlcAxisSlot::tryCreate(4));
    EXPECT_EQ(sys.find({g0, model::AxisFunction::X})->slot(),
              *PlcAxisSlot::tryCreate(5));
    // B 组从 8 起
    EXPECT_EQ(sys.find({g1, model::AxisFunction::X1})->slot(),
              *PlcAxisSlot::tryCreate(8));

    // findBySlot / find(AxisKey) 一致
    EXPECT_EQ(sys.findBySlot(*PlcAxisSlot::tryCreate(5)),
              sys.find({g0, model::AxisFunction::X}));
}

TEST(SystemBootTest, GantrySyncInjected_ByFunctionRole) {
    TopologySnapshot topo;
    topo.groups.push_back(group(true, true, fullRoleBlock(0)));

    AxisSystem sys;
    SystemBoot::initialize(sys, topo);
    const auto g0 = PlcGroupIndex(0);

    // X1 依赖龙门同步：龙门未配置时应被拒绝（RejectedGantryLocked）
    auto& x1sm = sys.find({g0, model::AxisFunction::X1})->stateMachine();
    auto& x1box = sys.find({g0, model::AxisFunction::X1})->outbox();
    EXPECT_EQ(x1sm.submit({model::AxisCommandKind::TriggerAbsMove}, x1box),
              state::AxisStateMachine::SubmitResult::RejectedGantryLocked);

    // Y 独立轴不依赖龙门同步：非忙且未锁时 Accepted
    auto& ysm = sys.find({g0, model::AxisFunction::Y})->stateMachine();
    auto& ybox = sys.find({g0, model::AxisFunction::Y})->outbox();
    EXPECT_EQ(ysm.submit({model::AxisCommandKind::TriggerAbsMove}, ybox),
              state::AxisStateMachine::SubmitResult::Accepted);
}

// ---------- ③ HmiVisible 展示判定 ----------

TEST(SystemBootTest, HmiVisible_IsGroupAndRoleAnded) {
    TopologySnapshot topo;
    // 组 0 可见：role0 可见、role1 不可见 -> axis X1 可见 / X2 不可见
    std::vector<TopologyRole> r0;
    r0.push_back(role(true, true, 0));   // X1 visible
    r0.push_back(role(true, false, 1));  // X2 not visible
    r0.push_back(role(true, true, 2));   // Y visible
    for (int i = 3; i < 8; ++i) r0.push_back(role(true, true, i));
    topo.groups.push_back(group(true, true, r0));
    // 组 1 不可见：即使 role 可见，axis 也不可见
    topo.groups.push_back(group(true, false, fullRoleBlock(8)));

    AxisSystem sys;
    SystemBoot::initialize(sys, topo);
    const auto g0 = PlcGroupIndex(0);
    const auto g1 = PlcGroupIndex(1);

    EXPECT_TRUE(sys.find({g0, model::AxisFunction::X1})->hmiVisible());
    EXPECT_FALSE(sys.find({g0, model::AxisFunction::X2})->hmiVisible());
    EXPECT_TRUE(sys.find({g0, model::AxisFunction::Y})->hmiVisible());
    // 组不可见 => 轴不可见（& 门控）
    EXPECT_FALSE(sys.find({g1, model::AxisFunction::X1})->hmiVisible());
    EXPECT_FALSE(sys.group(g1).hmiVisible());
}

// ---------- ④ 重复 / 非法配置降级锁定 ----------

TEST(SystemBootTest, DuplicateSlot_WithinGroup_DegradesAndLocks) {
    TopologySnapshot topo;
    std::vector<TopologyRole> r0;
    r0.push_back(role(true, true, 0));  // X1 -> slot 0
    r0.push_back(role(true, true, 0));  // X2 -> slot 0（重复）
    for (int i = 2; i < 8; ++i) r0.push_back(role(true, true, i));
    topo.groups.push_back(group(true, true, r0));

    AxisSystem sys;
    const auto res = SystemBoot::initialize(sys, topo);
    const auto g0 = PlcGroupIndex(0);

    EXPECT_TRUE(res.degraded);
    EXPECT_FALSE(res.ok);
    EXPECT_EQ(res.issues.size(), 1u);
    EXPECT_EQ(res.issues[0].kind, SystemBootIssue::Kind::DuplicateSlotBinding);
    // 重复槽位跳过（不覆盖已注册轴）
    EXPECT_EQ(sys.registry().count(), 5u);
    EXPECT_FALSE(sys.find({g0, model::AxisFunction::X2}));  // 未被绑定
    // 组被降级锁定：isReady=false（即使 valid=true）
    EXPECT_TRUE(sys.group(g0).valid());
    EXPECT_TRUE(sys.group(g0).degraded());
    EXPECT_FALSE(sys.group(g0).isReady());
    EXPECT_FALSE(sys.isReady());
}

TEST(SystemBootTest, IllegalSlotIndex_DegradesAndSkips) {
    TopologySnapshot topo;
    std::vector<TopologyRole> r0;
    r0.push_back(role(true, true, 16));  // 越界（非 0..15）
    for (int i = 1; i < 8; ++i) r0.push_back(role(true, true, i));
    topo.groups.push_back(group(true, true, r0));

    AxisSystem sys;
    const auto res = SystemBoot::initialize(sys, topo);

    EXPECT_TRUE(res.degraded);
    EXPECT_FALSE(res.ok);
    ASSERT_EQ(res.issues.size(), 1u);
    EXPECT_EQ(res.issues[0].kind, SystemBootIssue::Kind::IllegalSlotIndex);
    EXPECT_EQ(res.issues[0].plcAxisIndex, 16);
    EXPECT_EQ(sys.registry().count(), 5u);  // 越界轴未注册
    EXPECT_FALSE(sys.group(PlcGroupIndex(0)).isReady());
}

TEST(SystemBootTest, InvalidGroup_BuildsButNotReady) {
    TopologySnapshot topo;
    // B 组 valid=false：仍需构建轴，但标记 not-ready（§10.5），不开放控制
    topo.groups.push_back(group(true, true, fullRoleBlock(0)));
    topo.groups.push_back(group(false, true, fullRoleBlock(8)));

    AxisSystem sys;
    const auto res = SystemBoot::initialize(sys, topo);
    const auto g1 = PlcGroupIndex(1);

    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.degraded);
    EXPECT_EQ(res.registeredAxes, 12u);  // B 组轴仍被构建
    EXPECT_EQ(sys.group(g1).axisCount(), 6u);
    EXPECT_FALSE(sys.group(g1).valid());
    EXPECT_FALSE(sys.group(g1).isReady());
    // A 组 ready，但整体 isReady 需要全部组 ready
    EXPECT_FALSE(sys.isReady());
}

TEST(SystemBootTest, InvalidRole_SkippedWithoutDegradation) {
    TopologySnapshot topo;
    std::vector<TopologyRole> r0;
    r0.push_back(role(false, true, -1));  // 无效角色（PLC 空绑定）
    r0.push_back(role(true, true, 1));
    for (int i = 2; i < 8; ++i) r0.push_back(role(true, true, i));
    topo.groups.push_back(group(true, true, r0));

    AxisSystem sys;
    const auto res = SystemBoot::initialize(sys, topo);
    const auto g0 = PlcGroupIndex(0);

    // 无效角色合规跳过，不触发降级
    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.degraded);
    EXPECT_EQ(sys.registry().count(), 5u);
    EXPECT_FALSE(sys.find({g0, model::AxisFunction::X1}));
}

TEST(SystemBootTest, Reboot_ReplacesPreviousBinding) {
    TopologySnapshot a;
    a.groups.push_back(group(true, true, fullRoleBlock(0)));
    TopologySnapshot b;
    b.groups.push_back(group(true, true, fullRoleBlock(10)));

    AxisSystem sys;
    SystemBoot::initialize(sys, a);
    EXPECT_EQ(sys.totalAxes(), 6u);
    EXPECT_TRUE(sys.find({PlcGroupIndex(0), model::AxisFunction::X1}));

    // 二次启动（不同拓扑）应替换旧绑定，而非累积 / 误判重复
    const auto res = SystemBoot::initialize(sys, b);
    EXPECT_TRUE(res.ok);
    EXPECT_FALSE(res.degraded);
    EXPECT_EQ(sys.totalAxes(), 6u);
    EXPECT_EQ(sys.find({PlcGroupIndex(0), model::AxisFunction::X1})->slot(),
              *PlcAxisSlot::tryCreate(10));
}

}  // namespace
}  // namespace domain_vnext::system

