// ============================================================================
// test_fake_plc_runtime_gateway.cpp —— Step 11 fake: 高层假实现测试（红/绿）
// ============================================================================
// 依据《TDD实施文档》Step 11（11.1 绿 FakePlcRuntimeGateway.h/.cpp）验证应用层
// TDD 假实现的契约：
//   - readTopology/readRuntime 返回脚本化快照；未脚本化 → Transport 失败（强制显式）
//   - 脚本化读故障（sticky）覆盖快照；clear 恢复
//   - readRuntime 仅 Trusted 以 success 返回，非 Trusted → Transport（与真实 Gateway 一致）
//   - writeAxis 记录并返回 sent / 脚本化失败（只提交、不读回）
//   - submitGantryRequestDetailed 记录并返回 Submitted / 脚本化阶段，requestSeq 原样
//   - connectionState 反映 setConnected；requestReconnect 计数
// ============================================================================
#include <gtest/gtest.h>

#include "infrastructure/plc_vnext/IPlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/GantrySubmitResult.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcGroupIndex.h"
#include "infrastructure/plc_vnext/contracts/ReadResult.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/contracts/SafetySnapshot.h"
#include "infrastructure/plc_vnext/contracts/SnapshotQuality.h"
#include "infrastructure/plc_vnext/contracts/TopologySnapshot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace plc_vnext::fake {
namespace {

using contracts::CommunicationResult;
using contracts::GantryCommandKind;
using contracts::GantryRequest;
using contracts::GantrySubmitState;
using contracts::PlcAxisCommand;
using contracts::PlcAxisCommandKind;
using contracts::PlcAxisSlot;
using contracts::PlcGroupIndex;
using contracts::SnapshotQuality;

// ─────────────────────────────────────────────
// readTopology
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, ReadTopology_ReturnsScriptedSnapshot) {
    FakePlcRuntimeGateway gw;
    gw.setTopologySnapshot(makeValidTopologySnapshot(/*revision=*/3));

    auto res = gw.readTopology();
    ASSERT_TRUE(res.hasValue());
    EXPECT_EQ(res.value().header.revision, 3);
    EXPECT_TRUE(res.value().header.configValid);
}

TEST(FakePlcRuntimeGatewayTest, ReadTopology_NoSnapshot_ReturnsTransportFailure) {
    FakePlcRuntimeGateway gw;
    auto res = gw.readTopology();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::TopologySnapshot>::FailureKind::Transport);
    EXPECT_FALSE(res.diagnostic().empty());
}

TEST(FakePlcRuntimeGatewayTest, ReadTopology_ScriptedFailure_OverridesSnapshot) {
    FakePlcRuntimeGateway gw;
    gw.setTopologySnapshot(makeValidTopologySnapshot());
    gw.scriptTopologyReadFailure(
        contracts::ReadResult<contracts::TopologySnapshot>::FailureKind::Decode,
        "decode broken");

    auto res = gw.readTopology();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::TopologySnapshot>::FailureKind::Decode);
    EXPECT_EQ(res.diagnostic(), "decode broken");

    gw.clearTopologyReadFailure();
    EXPECT_TRUE(gw.readTopology().hasValue());
}

// ─────────────────────────────────────────────
// readRuntime
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, ReadRuntime_Trusted_ReturnsSuccess) {
    FakePlcRuntimeGateway gw;
    gw.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());

    auto res = gw.readRuntime();
    ASSERT_TRUE(res.hasValue());
    EXPECT_EQ(res.value().quality, SnapshotQuality::Trusted);
    EXPECT_TRUE(res.value().axes[0].trusted);
}

TEST(FakePlcRuntimeGatewayTest, ReadRuntime_NotTrusted_ReturnsTransportFailure) {
    FakePlcRuntimeGateway gw;
    auto snap = makeTrustedRuntimeSnapshot();
    snap.quality = SnapshotQuality::TransportFailed;
    snap.axes[3].trusted = false;
    gw.setRuntimeSnapshot(snap);

    auto res = gw.readRuntime();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::Transport);
}

TEST(FakePlcRuntimeGatewayTest, ReadRuntime_NoSnapshot_ReturnsTransportFailure) {
    FakePlcRuntimeGateway gw;
    EXPECT_FALSE(gw.readRuntime().hasValue());
}

TEST(FakePlcRuntimeGatewayTest, ReadRuntime_ScriptedFailure_OverridesSnapshot) {
    FakePlcRuntimeGateway gw;
    gw.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
    gw.scriptRuntimeReadFailure(
        contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::RevisionChanged,
        "revision moved");

    auto res = gw.readRuntime();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::RuntimeSnapshot>::FailureKind::RevisionChanged);

    gw.clearRuntimeReadFailure();
    EXPECT_TRUE(gw.readRuntime().hasValue());
}

// ─────────────────────────────────────────────
// writeAxis
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, WriteAxis_RecordsAndReportsSent) {
    FakePlcRuntimeGateway gw;
    auto slot = PlcAxisSlot::tryCreate(2);
    ASSERT_TRUE(slot.has_value());

    auto res = gw.writeAxis(*slot, PlcAxisCommand::makeSetManualSpeed(1.5f));
    EXPECT_TRUE(res.ok());

    const auto written = gw.writtenAxis();
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0].slot, *slot);
    EXPECT_EQ(written[0].cmd.kind, PlcAxisCommandKind::SetManualSpeed);
    EXPECT_FLOAT_EQ(written[0].cmd.realValue, 1.5f);
}

TEST(FakePlcRuntimeGatewayTest, WriteAxis_ScriptedFailure) {
    FakePlcRuntimeGateway gw;
    auto slot = PlcAxisSlot::tryCreate(0);
    ASSERT_TRUE(slot.has_value());

    gw.scriptWriteAxisFailure(CommunicationResult::Status::Timeout, "no ack");
    auto res = gw.writeAxis(*slot, PlcAxisCommand::makeEnableAxis(true));
    EXPECT_FALSE(res.ok());
    EXPECT_TRUE(res.retryable());
    EXPECT_EQ(res.status, CommunicationResult::Status::Timeout);

    gw.clearWriteAxisFailure();
    EXPECT_TRUE(gw.writeAxis(*slot, PlcAxisCommand::makeEnableAxis(true)).ok());
}

// ─────────────────────────────────────────────
// submitGantryRequest / Detailed
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, SubmitGantryDetailed_Submitted_RecordsRequest) {
    FakePlcRuntimeGateway gw;
    auto g0 = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g0.has_value());

    auto out = gw.submitGantryRequestDetailed(*g0, GantryRequest::couple(/*seq=*/41));
    EXPECT_EQ(out.state, GantrySubmitState::Submitted);
    EXPECT_TRUE(out.result.ok());
    EXPECT_EQ(out.requestSeq, 41);

    const auto subs = gw.gantrySubmissions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].group, *g0);
    EXPECT_EQ(subs[0].req.command, GantryCommandKind::Couple);
    EXPECT_EQ(subs[0].req.requestSeq, 41);
}

TEST(FakePlcRuntimeGatewayTest, SubmitGantry_Compatibility_ReturnsUnderlyingResult) {
    FakePlcRuntimeGateway gw;
    auto g0 = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g0.has_value());

    // 兼容入口只返回底层通讯结果（Submitted → sent()）。
    EXPECT_TRUE(gw.submitGantryRequest(*g0, GantryRequest::reset(5)).ok());
}

TEST(FakePlcRuntimeGatewayTest, SubmitGantryDetailed_ScriptedFailure_PreservesSeq) {
    FakePlcRuntimeGateway gw;
    auto g0 = PlcGroupIndex::tryCreate(0);
    ASSERT_TRUE(g0.has_value());

    gw.scriptGantrySubmitFailure(GantrySubmitState::CommitUncertain, "seq unknown");
    auto out = gw.submitGantryRequestDetailed(*g0, GantryRequest::decouple(7));
    EXPECT_EQ(out.state, GantrySubmitState::CommitUncertain);
    EXPECT_FALSE(out.result.ok());
    EXPECT_EQ(out.requestSeq, 7);
    EXPECT_TRUE(out.committedUnknown());

    gw.clearGantrySubmitFailure();
    EXPECT_TRUE(gw.submitGantryRequestDetailed(*g0, GantryRequest::decouple(8)).ok());
}

// ─────────────────────────────────────────────
// 阶段 2：readSafety —— 急停只读快照契约（M224/M225）
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, ReadSafety_ReturnsScriptedSnapshot) {
    FakePlcRuntimeGateway gw;
    contracts::SafetySnapshot s;
    s.trusted = true;
    s.emergencyStop = true;
    s.releaseRequest = false;
    gw.setSafetySnapshot(s);

    auto res = gw.readSafety();
    ASSERT_TRUE(res.hasValue());
    EXPECT_TRUE(res.value().trusted);
    EXPECT_TRUE(res.value().emergencyStop);
    EXPECT_FALSE(res.value().releaseRequest);
}

TEST(FakePlcRuntimeGatewayTest, ReadSafety_NoSnapshot_ReturnsTransportFailure) {
    FakePlcRuntimeGateway gw;
    auto res = gw.readSafety();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::SafetySnapshot>::FailureKind::Transport);
    EXPECT_FALSE(res.diagnostic().empty());
}

TEST(FakePlcRuntimeGatewayTest, ReadSafety_ScriptedFailure_OverridesSnapshot) {
    FakePlcRuntimeGateway gw;
    contracts::SafetySnapshot s;
    s.trusted = true;
    gw.setSafetySnapshot(s);
    gw.scriptSafetyReadFailure(
        contracts::ReadResult<contracts::SafetySnapshot>::FailureKind::Transport,
        "safety read broken");

    auto res = gw.readSafety();
    ASSERT_FALSE(res.hasValue());
    EXPECT_EQ(res.failureKind(),
              contracts::ReadResult<contracts::SafetySnapshot>::FailureKind::Transport);
    EXPECT_EQ(res.diagnostic(), "safety read broken");

    gw.clearSafetyReadFailure();
    EXPECT_TRUE(gw.readSafety().hasValue());
}

// ─────────────────────────────────────────────
// connection / reconnect
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, ConnectionState_ReflectsScriptedFlag) {
    FakePlcRuntimeGateway gw;
    gw.setConnected(true);
    EXPECT_TRUE(gw.connectionState().connected);

    gw.setConnected(false);
    EXPECT_FALSE(gw.connectionState().connected);
}

TEST(FakePlcRuntimeGatewayTest, RequestReconnect_IncrementsCount) {
    FakePlcRuntimeGateway gw;
    EXPECT_EQ(gw.requestReconnectCount(), 0u);
    gw.requestReconnect();
    gw.requestReconnect();
    EXPECT_EQ(gw.requestReconnectCount(), 2u);
}

// ─────────────────────────────────────────────
// 接口多态：Fake 必须可经 IPlcRuntimeGateway 基类消费（application 接线路径）。
// ─────────────────────────────────────────────
TEST(FakePlcRuntimeGatewayTest, UsableThroughGatewayInterface) {
    auto fake = std::make_unique<FakePlcRuntimeGateway>();
    fake->setTopologySnapshot(makeValidTopologySnapshot());
    std::unique_ptr<IPlcRuntimeGateway> gw = std::move(fake);
    EXPECT_TRUE(gw->readTopology().hasValue());
    EXPECT_TRUE(gw->readTopology().value().header.configValid);
}

}  // namespace
}  // namespace plc_vnext::fake

