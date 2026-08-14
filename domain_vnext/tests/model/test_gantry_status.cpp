// ============================================================================
// test_gantry_status.cpp —— P1 model: GantryStatus（状态映射 + 领域模型）
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/GantryStatus.h"

namespace domain_vnext::model {
namespace {

TEST(GantryCouplingStateTest, RawStateMapping_MatchesTable) {
    EXPECT_EQ(gantryCouplingStateFromRaw(0), GantryCouplingState::Unconfigured);
    EXPECT_EQ(gantryCouplingStateFromRaw(1), GantryCouplingState::Decoupled);
    EXPECT_EQ(gantryCouplingStateFromRaw(2), GantryCouplingState::CouplingRequested);
    EXPECT_EQ(gantryCouplingStateFromRaw(3), GantryCouplingState::Coupled);
    EXPECT_EQ(gantryCouplingStateFromRaw(4), GantryCouplingState::DecouplingRequested);
    EXPECT_EQ(gantryCouplingStateFromRaw(5), GantryCouplingState::Fault);
    // 未知值保守归为 Unconfigured
    EXPECT_EQ(gantryCouplingStateFromRaw(99), GantryCouplingState::Unconfigured);
    EXPECT_EQ(gantryCouplingStateFromRaw(-1), GantryCouplingState::Unconfigured);
}

TEST(GantryStatusModelTest, Defaults) {
    GantryStatusModel s;
    EXPECT_EQ(s.coupling, GantryCouplingState::Unconfigured);
    EXPECT_EQ(s.rawState, 0);
    EXPECT_EQ(s.ackSeq, 0);
    EXPECT_EQ(s.commandResult, 0);
    EXPECT_EQ(s.commandErrorCode, 0);
    EXPECT_FALSE(s.readyToCouple);
    EXPECT_FALSE(s.readyToDecouple);
    EXPECT_FALSE(s.memberControlAllowed);
    EXPECT_FALSE(s.logicalControlAllowed);
    EXPECT_FALSE(s.x1InGear);
    EXPECT_FALSE(s.x2InGear);
    EXPECT_FLOAT_EQ(s.x1Position, 0.f);
    EXPECT_FLOAT_EQ(s.x2Position, 0.f);
    EXPECT_FLOAT_EQ(s.logicalPosition, 0.f);
    EXPECT_FLOAT_EQ(s.skew, 0.f);
    EXPECT_FALSE(s.fault);
    EXPECT_EQ(s.faultCode, 0);
    EXPECT_FALSE(s.trusted);
}

TEST(GantryStatusModelTest, CouplingReflectsRawState) {
    GantryStatusModel s;
    s.rawState = 3;
    s.coupling = gantryCouplingStateFromRaw(s.rawState);
    EXPECT_EQ(s.coupling, GantryCouplingState::Coupled);
}

TEST(GantryStatusModelTest, InternalStepMappedFromSnapshot) {
    plc_vnext::contracts::GantryStatusSnapshot snap;
    snap.state = 3;
    snap.internalStep = 80;   // 建立完成诊断步骤
    snap.ackSeq = 7;
    snap.logicalControlAllowed = true;

    const auto m = gantryStatusModelFromSnapshot(snap);
    EXPECT_EQ(m.internalStep, 80);
    EXPECT_EQ(m.rawState, 3);
    EXPECT_EQ(m.coupling, GantryCouplingState::Coupled);
    EXPECT_EQ(m.ackSeq, 7);
    EXPECT_TRUE(m.logicalControlAllowed);
}

}  // namespace
}  // namespace domain_vnext::model
