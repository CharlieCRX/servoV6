// ============================================================================
// test_shadow_run_assessor.cpp —— 阶段 2：只读影子运行锁定判定（TDD）
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§10.2“真实只读影子运行”通过标准：
//   - 快照不完整、超时或部分失败时不会标记为可信；
//   - 配置无效、Revision 不一致或反馈不可信时，普通控制保持锁定；
//   - 急停状态未知（读取失败）不得当作“正常”，普通控制保持锁定；
//   - 设备急停 M224=ON 期间拒绝普通操作（普通控制锁定）。
// 本判定是纯函数（无副作用、不写 PLC），供阶段 2 影子运行工具与后续启动链路
// 使用：只有全部只读同步成功、配置有效、反馈可信、非急停时，才允许解除锁定。
// ============================================================================
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "application_vnext/ShadowRunAssessor.h"
#include "infrastructure/plc_vnext/contracts/ConnectionState.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"

namespace application_vnext {
namespace {

using plc_vnext::contracts::ConnectionState;
using plc_vnext::contracts::ReadResult;
using plc_vnext::contracts::RuntimeSnapshot;
using plc_vnext::contracts::SafetySnapshot;
using plc_vnext::contracts::SnapshotQuality;
using plc_vnext::contracts::TopologySnapshot;

using TopoResult = ReadResult<TopologySnapshot>;
using RuntimeResult = ReadResult<RuntimeSnapshot>;
using SafetyResult = ReadResult<SafetySnapshot>;

// “全绿”影子运行输入：连接、拓扑（ConfigValid=true）、运行（Trusted）、
// 急停（trusted 且未触发）——此时允许解除普通控制锁定。
struct GreenInput {
    ConnectionState conn = ConnectionState::connectedState("up");
    TopoResult topo = TopoResult::success([] {
        TopologySnapshot s;
        s.header.configValid = true;
        return s;
    }());
    RuntimeResult runtime = RuntimeResult::success([] {
        RuntimeSnapshot s;
        s.quality = SnapshotQuality::Trusted;
        return s;
    }());
    SafetyResult safety = SafetyResult::success([] {
        SafetySnapshot s;
        s.trusted = true;
        return s;
    }());
};

TEST(ShadowRunAssessorTest, AllGreen_DoesNotLockOrdinaryControl) {
    auto in = GreenInput{};
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_FALSE(d.lockOrdinaryControl);
    EXPECT_TRUE(d.reasons.empty());
}

TEST(ShadowRunAssessorTest, Disconnected_LocksOrdinaryControl) {
    auto in = GreenInput{};
    in.conn = ConnectionState::disconnectedState("link down");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
}

TEST(ShadowRunAssessorTest, TopologyReadFailure_Locks) {
    auto in = GreenInput{};
    in.topo = TopoResult::failure(
        TopoResult::FailureKind::Transport, "topology read failed");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
    ASSERT_FALSE(d.reasons.empty());
}

TEST(ShadowRunAssessorTest, RevisionChanged_Locks) {
    auto in = GreenInput{};
    in.topo = TopoResult::failure(
        TopoResult::FailureKind::RevisionChanged, "revision changed");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
    // Revision 变化是独立原因，与普通传输失败可区分。
    EXPECT_NE(std::find(d.reasons.begin(), d.reasons.end(),
                        std::string("revision_changed")),
              d.reasons.end());
}

TEST(ShadowRunAssessorTest, ConfigInvalid_Locks) {
    auto in = GreenInput{};
    in.topo = TopoResult::success([] {
        TopologySnapshot s;
        s.header.configValid = false;  // PLC 配置无效
        return s;
    }());
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
    EXPECT_NE(std::find(d.reasons.begin(), d.reasons.end(),
                        std::string("config_invalid")),
              d.reasons.end());
}

TEST(ShadowRunAssessorTest, RuntimeReadFailure_Locks) {
    auto in = GreenInput{};
    in.runtime = RuntimeResult::failure(
        RuntimeResult::FailureKind::Transport, "runtime not trusted");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
}

TEST(ShadowRunAssessorTest, SafetyReadFailure_Locks) {
    auto in = GreenInput{};
    in.safety = SafetyResult::failure(
        SafetyResult::FailureKind::Transport, "safety unknown");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
}

TEST(ShadowRunAssessorTest, EmergencyStopTriggered_Locks) {
    auto in = GreenInput{};
    in.safety = SafetyResult::success([] {
        SafetySnapshot s;
        s.trusted = true;
        s.emergencyStop = true;  // M224 = ON
        return s;
    }());
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
}

TEST(ShadowRunAssessorTest, MultipleReasons_Aggregated) {
    auto in = GreenInput{};
    in.conn = ConnectionState::disconnectedState("down");
    in.runtime = RuntimeResult::failure(
        RuntimeResult::FailureKind::Transport, "runtime not trusted");
    auto d = ShadowRunAssessor::evaluate(in.conn, in.topo, in.runtime, in.safety);
    EXPECT_TRUE(d.lockOrdinaryControl);
    // 断连 + 反馈不可信两个原因都被记录。
    EXPECT_GE(d.reasons.size(), 2u);
}

}  // namespace
}  // namespace application_vnext
