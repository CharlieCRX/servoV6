// ============================================================================
// test_gantry_coupling_state_machine.cpp —— P2 state: 龙门联动状态机
// ============================================================================
// 覆盖：同步映射、建 / 解事务多条件闭环、RequestSeq 自增、准入与冲突拒绝。
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/state/GantryCouplingStateMachine.h"

namespace domain_vnext::state {
namespace {

using model::GantryCouplingState;
using model::GantryParamModel;
using model::GantryStatusModel;
using model::gantryCouplingStateFromRaw;
using plc_vnext::contracts::GantryCommandKind;

// 构造龙门状态反馈（默认 success 语义 result=2）
static GantryStatusModel makeStatus(int rawState, int32_t ackSeq = 0,
                                    int16_t commandResult = 2, bool x1InGear = false,
                                    bool x2InGear = false, bool logical = false,
                                    bool member = false, bool readyToCouple = false,
                                    bool readyToDecouple = false, bool fault = false) {
    GantryStatusModel s;
    s.rawState = rawState;
    s.coupling = gantryCouplingStateFromRaw(rawState);
    s.ackSeq = ackSeq;
    s.commandResult = commandResult;
    s.x1InGear = x1InGear;
    s.x2InGear = x2InGear;
    s.logicalControlAllowed = logical;
    s.memberControlAllowed = member;
    s.readyToCouple = readyToCouple;
    s.readyToDecouple = readyToDecouple;
    s.fault = fault;
    return s;
}

// 建立联动完整闭环：同步 -> requestCouple -> 中间帧 -> 多条件闭环
TEST(GantryCouplingStateMachineTest, Couple_ClosedByMultiCondition) {
    GantryCouplingStateMachine sm;

    // 初始未同步：拒绝
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedUnconfigured);
    EXPECT_FALSE(sm.isConfigured());

    // 注入配置（ConfigValid + ReadyToCouple）
    GantryParamModel cfg;
    cfg.valid = true;
    cfg.readyToCouple = true;
    sm.applyConfig(cfg);

    // 首次反馈：已解除（readyToCouple=true）
    sm.applyFeedback(makeStatus(1, 0, 2, false, false, false, false, true, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::Decoupled);
    EXPECT_TRUE(sm.isConfigured());

    // requestCouple -> 产出 Couple + seq=1
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::Accepted);
    EXPECT_EQ(sm.state(), GantryCouplingState::CouplingRequested);
    EXPECT_TRUE(sm.hasOpenTransaction());
    ASSERT_TRUE(sm.hasPendingRequest());
    auto req = sm.popPendingRequest();
    EXPECT_EQ(req.command, GantryCommandKind::Couple);
    EXPECT_EQ(req.requestSeq, 1);

    // 中间帧（未满足全部条件）：保持建立中
    sm.applyFeedback(makeStatus(2, 1, 2, false, false, false, false, true, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::CouplingRequested);

    // 多条件闭环：Ack==1 && Result==2 && State==3 && InGear && LogicalControl
    sm.applyFeedback(makeStatus(3, 1, 2, true, true, true, false, true, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::Coupled);
    EXPECT_FALSE(sm.hasOpenTransaction());
}

TEST(GantryCouplingStateMachineTest, Reset_AcceptedAndSeqIncrements) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(5, 0, 2, false, false, false, false, false, false, true));
    EXPECT_EQ(sm.state(), GantryCouplingState::Fault);

    EXPECT_EQ(sm.requestReset(), GantryCouplingStateMachine::RequestResult::Accepted);
    ASSERT_TRUE(sm.hasPendingRequest());
    auto req = sm.popPendingRequest();
    EXPECT_EQ(req.command, GantryCommandKind::Reset);
    EXPECT_EQ(req.requestSeq, 1);
}

// 解除闭环：同步到已联动（readyToDecouple=true）-> requestDecouple -> 多条件闭环
TEST(GantryCouplingStateMachineTest, Decouple_Closed_FreshPath) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(3, 0, 2, true, true, true, false, true, true, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::Coupled);

    EXPECT_EQ(sm.requestDecouple(), GantryCouplingStateMachine::RequestResult::Accepted);
    EXPECT_EQ(sm.state(), GantryCouplingState::DecouplingRequested);
    ASSERT_TRUE(sm.hasPendingRequest());
    auto req = sm.popPendingRequest();
    EXPECT_EQ(req.command, GantryCommandKind::Decouple);
    EXPECT_EQ(req.requestSeq, 1);

    // 多条件闭环：Ack==1 && Result==2 && State==1 && !InGear && MemberControl
    sm.applyFeedback(makeStatus(1, 1, 2, false, false, false, true, true, true, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::Decoupled);
    EXPECT_FALSE(sm.hasOpenTransaction());
}

TEST(GantryCouplingStateMachineTest, DecoupleRejected_WhenNotReady) {
    GantryCouplingStateMachine sm;
    // 已联动但 readyToDecouple=false
    sm.applyFeedback(makeStatus(3, 0, 2, true, true, true, false, true, false, false));
    EXPECT_EQ(sm.state(), GantryCouplingState::Coupled);
    EXPECT_EQ(sm.requestDecouple(), GantryCouplingStateMachine::RequestResult::RejectedNotReady);
}

TEST(GantryCouplingStateMachineTest, RequestSeq_IncrementsPerRequest) {
    GantryCouplingStateMachine sm;
    GantryParamModel cfg;
    cfg.valid = true;
    cfg.readyToCouple = true;
    sm.applyConfig(cfg);
    sm.applyFeedback(makeStatus(1, 0, 2, false, false, false, false, true, false));

    sm.requestCouple();
    auto c1 = sm.popPendingRequest();
    EXPECT_EQ(c1.requestSeq, 1);

    // 闭环到 Coupled，再解除
    sm.applyFeedback(makeStatus(3, 1, 2, true, true, true, false, true, false));
    sm.applyFeedback(makeStatus(3, 0, 2, true, true, true, false, true, true, false));
    sm.requestDecouple();
    auto d1 = sm.popPendingRequest();
    EXPECT_EQ(d1.requestSeq, 2);
}

TEST(GantryCouplingStateMachineTest, CoupleRejected_WhenFault) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(5, 0, 2, false, false, false, false, false, false, true));
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedFault);
}

TEST(GantryCouplingStateMachineTest, CoupleRejected_WhenNotReady) {
    GantryCouplingStateMachine sm;
    GantryParamModel cfg;
    cfg.valid = true;
    cfg.readyToCouple = false;  // ReadyToCouple 不满足
    sm.applyConfig(cfg);
    sm.applyFeedback(makeStatus(1, 0, 2, false, false, false, false, false, false));
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedNotReady);
}

TEST(GantryCouplingStateMachineTest, CoupleRejected_WhenAlreadyCoupled) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(3, 0, 2, true, true, true, false, true, false));
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedStateConflict);
}

TEST(GantryCouplingStateMachineTest, DecoupleRejected_WhenDecoupled) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(1, 0, 2, false, false, false, false, true, false));
    EXPECT_EQ(sm.requestDecouple(), GantryCouplingStateMachine::RequestResult::RejectedNotDecoupled);
}

TEST(GantryCouplingStateMachineTest, CoupleRejected_DuringDecoupling) {
    GantryCouplingStateMachine sm;
    sm.applyFeedback(makeStatus(3, 0, 2, true, true, true, false, true, true, false));
    sm.requestDecouple();
    sm.popPendingRequest();
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedStateConflict);
}

TEST(GantryCouplingStateMachineTest, Unconfigured_RejectsAllIntents) {
    GantryCouplingStateMachine sm;
    EXPECT_EQ(sm.requestCouple(), GantryCouplingStateMachine::RequestResult::RejectedUnconfigured);
    EXPECT_EQ(sm.requestDecouple(), GantryCouplingStateMachine::RequestResult::RejectedUnconfigured);
    EXPECT_EQ(sm.requestReset(), GantryCouplingStateMachine::RequestResult::RejectedUnconfigured);
}

}  // namespace
}  // namespace domain_vnext::state

