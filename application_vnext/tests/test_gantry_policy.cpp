// ============================================================================
// test_gantry_policy.cpp —— 龙门生命周期策略 + 运动许可守卫 + PowerOwnership TDD
// ============================================================================
// 覆盖：
//   - GantryMotionGuard：耦合完成态放行，各失效条件拒绝；
//   - GantryLifecyclePolicy：建立（使能虚轴→等 ms=2→Couple→Ready）、
//     CommandErrorCode!=0 时先 Reset 再 Couple、解除（Decouple→掉电→Done）；
//   - PowerOwnership::LifecycleManaged：Abs 策略不使能、不掉电、失联即停。
// 用 FakePlcRuntimeGateway 注入已解码快照，手工驱动 tick() 验证状态推进。
// ============================================================================
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/GantryLifecyclePolicy.h"
#include "application_vnext/policy/GantryMotionApi.h"
#include "application_vnext/policy/GantryMotionGuard.h"
#include "domain_vnext/model/AxisFunction.h"
#include "infrastructure/plc_vnext/contracts/GantryRequest.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"
#include "infrastructure/plc_vnext/contracts/PlcCommand.h"
#include "infrastructure/plc_vnext/contracts/RuntimeSnapshot.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace application_vnext::policy {
namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::SystemManagerVnext;
using domain_vnext::model::AxisFunction;
using plc_vnext::contracts::GantryCommandKind;
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::contracts::PlcGroupIndex;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

PlcAxisSlot slotOf(int v) { return *PlcAxisSlot::tryCreate(v); }
const PlcGroupIndex g0{0};

class GantryPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        adapter_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        mgr_ = std::make_unique<SystemManagerVnext>(*adapter_);
        gw_.setTopologySnapshot(makeValidTopologySnapshot());
        gw_.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
        ASSERT_TRUE(mgr_->boot());
        mgr_->applyEmergencyStopFeedback(false);
        domain_vnext::model::GantryParamModel cfg;
        cfg.valid = true;
        mgr_->applyGantryConfig(g0, cfg);
        EXPECT_FALSE(mgr_->isSystemLocked());
    }

    /// 一次性设置 slot13 的 motionState 与 A 组龙门状态并注入反馈。
    void push(int16_t logicalMs, int32_t ackSeq, int16_t state, int16_t step,
              int16_t result, int16_t errCode, bool x1, bool x2, bool logical, bool member) {
        auto rt = makeTrustedRuntimeSnapshot();
        rt.axes[13].motionState = logicalMs;
        auto& s = rt.gantry[0];
        s.state = state; s.internalStep = step; s.commandResult = result;
        s.commandErrorCode = errCode; s.ackSeq = ackSeq;
        s.x1InGear = x1; s.x2InGear = x2;
        s.logicalControlAllowed = logical; s.memberControlAllowed = member;
        s.readyToCouple = (state == 1);
        s.readyToDecouple = (state == 3);
        s.fault = false; s.faultCode = 0; s.trusted = true;
        gw_.setRuntimeSnapshot(rt);
        ASSERT_TRUE(mgr_->poll());
    }
    /// 已耦合完成态（State=3 / Step=80 / LogicalAllowed）。ackSeq 对齐本次 Couple 序号。
    void setCoupledReady(int16_t ms = 2, int32_t ackSeq = 0) {
        push(ms, ackSeq, 3, 80, 2, 0, true, true, true, false);
    }
    /// 已解除空闲态（State=1 / Step=10 / MemberAllowed）。
    void setDecoupledIdle(int16_t ms = 2, int32_t ackSeq = 0) {
        push(ms, ackSeq, 1, 10, 0, 0, false, false, false, true);
    }

    bool wrote(PlcAxisCommandKind kind, bool level) const {
        for (const auto& w : gw_.writtenAxis()) {
            if (w.cmd.kind == kind && w.cmd.boolValue == level) return true;
        }
        return false;
    }

    FakePlcRuntimeGateway gw_;
    std::unique_ptr<PlcRuntimeDriverAdapter> adapter_;
    std::unique_ptr<SystemManagerVnext> mgr_;
};

// ---------- GantryMotionGuard：运动许可判定 ----------

TEST_F(GantryPolicyTest, Guard_AllowsWhenCoupledReady) {
    setCoupledReady();
    GantryMotionGuard guard(*mgr_, g0);
    const auto r = guard.evaluate();
    EXPECT_TRUE(r.allowed) << r.reason;
}

TEST_F(GantryPolicyTest, Guard_RejectsWhenNotCoupled) {
    setDecoupledIdle();
    GantryMotionGuard guard(*mgr_, g0);
    const auto r = guard.evaluate();
    EXPECT_FALSE(r.allowed);
    EXPECT_NE(std::string(r.reason).find("not coupled"), std::string::npos);
}

TEST_F(GantryPolicyTest, Guard_RejectsWhenInternalStepNot80) {
    push(2, 0, 3, 10, 2, 0, true, true, true, false);  // 已耦合但 Step=10
    GantryMotionGuard guard(*mgr_, g0);
    EXPECT_FALSE(guard.evaluate().allowed);
}

TEST_F(GantryPolicyTest, Guard_RejectsWhenLogicalNotAllowedOrFault) {
    push(2, 0, 3, 80, 2, 0, true, true, false, false);  // logical=false
    GantryMotionGuard guard(*mgr_, g0);
    EXPECT_FALSE(guard.evaluate().allowed);

    auto rt = makeTrustedRuntimeSnapshot();
    rt.gantry[0].state = 3;
    rt.gantry[0].internalStep = 80;
    rt.gantry[0].logicalControlAllowed = true;
    rt.gantry[0].fault = true;   // 运行中故障
    gw_.setRuntimeSnapshot(rt);
    ASSERT_TRUE(mgr_->poll());
    EXPECT_FALSE(guard.evaluate().allowed);
}


// ---------- GantryLifecyclePolicy：建立（使能虚轴 → Couple → Ready）----------

TEST_F(GantryPolicyTest, Lifecycle_Couple_EnablesMotorCouplesToReady) {
    setDecoupledIdle(/*ms=*/1);
    GantryLifecyclePolicy lp(*mgr_, g0);
    lp.beginCouple();

    lp.tick();   // ValidatePreconditions → EnsureAxisControl
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::EnsureAxisControl);
    lp.tick();   // EnsureAxisControl → 使能轴控
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableAxis, true));
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::WaitAxisControlReady);
    lp.tick();   // WaitAxisControlReady → EnsureMotor
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::EnsureMotor);
    lp.tick();   // EnsureMotor → 使能电机
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, true));
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::WaitMotorReady);

    setDecoupledIdle(/*ms=*/2);   // 电机就绪（motionState==2）
    lp.tick();   // WaitMotorReady → CheckGantryError
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::CheckGantryError);
    lp.tick();   // 无错误码 → SubmitCouple
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::SubmitCouple);
    lp.tick();   // SubmitCouple → 提交 Couple → WaitCoupleFinal
    const auto subs = gw_.gantrySubmissions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].req.command, GantryCommandKind::Couple);
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::WaitCoupleFinal);

    setCoupledReady(2, 1);
    lp.tick();   // WaitCoupleFinal → Ready
    EXPECT_TRUE(lp.isReady());
    EXPECT_FALSE(lp.hasError());
}

TEST_F(GantryPolicyTest, Lifecycle_Couple_ResetsErrorBeforeCouple) {
    // 上次命令留有错误码（CommandErrorCode=124）→ 必须先 Reset 清错再 Couple。
    push(1, 0, 1, 10, 3, 124, false, false, false, true);
    GantryLifecyclePolicy lp(*mgr_, g0);
    lp.beginCouple();

    lp.tick(); lp.tick(); lp.tick(); lp.tick();   // 使能轴控+电机 → WaitMotorReady
    push(2, 0, 1, 10, 3, 124, false, false, false, true);
    lp.tick();   // WaitMotorReady → CheckGantryError
    lp.tick();   // errCode!=0 → SubmitReset
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::SubmitReset);
    lp.tick();   // SubmitReset → 提交 Reset → WaitResetFinal
    const auto subs1 = gw_.gantrySubmissions();
    ASSERT_EQ(subs1.size(), 1u);
    EXPECT_EQ(subs1[0].req.command, GantryCommandKind::Reset);
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::WaitResetFinal);

    push(2, 1, 1, 10, 2, 0, false, false, false, true);   // Reset 已确认（ackSeq=1, result=2）
    lp.tick();   // WaitResetFinal → SubmitCouple
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::SubmitCouple);
    lp.tick();   // SubmitCouple → 提交 Couple
    const auto subs2 = gw_.gantrySubmissions();
    ASSERT_EQ(subs2.size(), 2u);
    EXPECT_EQ(subs2[1].req.command, GantryCommandKind::Couple);

    setCoupledReady(2, 2);
    lp.tick();
    EXPECT_TRUE(lp.isReady());
}

// ---------- GantryLifecyclePolicy：解除（Decouple → 掉电 → Done）----------

TEST_F(GantryPolicyTest, Lifecycle_Decouple_DecouplesThenDisablesMotor) {
    setCoupledReady(2, 1);   // 已耦合且逻辑轴空闲
    GantryLifecyclePolicy lp(*mgr_, g0);
    lp.beginDecouple();   // EnsureLogicalAxisStopped

    lp.tick();   // 逻辑轴已空闲 → SubmitDecouple
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::SubmitDecouple);
    lp.tick();   // SubmitDecouple → 提交 Decouple → WaitDecoupleFinal
    const auto subs = gw_.gantrySubmissions();
    ASSERT_EQ(subs.size(), 1u);
    EXPECT_EQ(subs[0].req.command, GantryCommandKind::Decouple);
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::WaitDecoupleFinal);

    push(2, 1, 1, 10, 2, 0, false, false, false, true);   // 解除完成（Decouple 已确认，result=2）
    lp.tick();   // WaitDecoupleFinal → DisableMotor
    EXPECT_EQ(lp.currentStep(), GantryLifecyclePolicy::Step::DisableMotor);
    lp.tick();   // DisableMotor → EnableMotor[X]=OFF → Done
    EXPECT_TRUE(lp.isDone());
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, false));
}

// ---------- PowerOwnership::LifecycleManaged：不使能 / 不掉电 / 失联即停 ----------

TEST_F(GantryPolicyTest, LifecycleManaged_Abs_DoesNotManagePower) {
    setCoupledReady();
    AbsMovePolicy p(*mgr_, slotOf(13));
    p.setPowerOwnership(PowerOwnership::LifecycleManaged);
    GantryMotionGuard guard(*mgr_, g0);
    p.setGantryGuard(&guard);
    p.start();
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::PostEnableDelay);
    EXPECT_FALSE(wrote(PlcAxisCommandKind::EnableAxis, true));   // 不自行使能

    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    p.tick();   // PostEnableDelay → TriggeringMove
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::TriggeringMove);
    p.tick();   // TriggeringMove → 触发 → WaitingMotionStart

    push(5, 0, 3, 80, 2, 0, true, true, true, false);   // 进入绝对定位
    p.tick();   // WaitingMotionStart → WaitingMotionFinish
    push(2, 0, 3, 80, 2, 0, true, true, true, false);   // 自然回 idle
    p.tick();   // WaitingMotionFinish → PostStopDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    p.tick();   // PostStopDelay → Disabling
    p.tick();   // Disabling → 掉电 no-op → Done
    EXPECT_TRUE(p.isDone());
    EXPECT_FALSE(wrote(PlcAxisCommandKind::EnableMotor, false));  // 不掉电
}

TEST_F(GantryPolicyTest, LifecycleManaged_Abs_PermitLost_StopsAndErrors) {
    setCoupledReady();
    AbsMovePolicy p(*mgr_, slotOf(13));
    p.setPowerOwnership(PowerOwnership::LifecycleManaged);
    GantryMotionGuard guard(*mgr_, g0);
    p.setGantryGuard(&guard);
    p.start();

    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    p.tick();   // → TriggeringMove
    p.tick();   // → 触发 → WaitingMotionStart

    // 运行中联动失效（Fault=5）→ guard 失联 → 发 Stop + Error（不掉电）。
    auto rt = makeTrustedRuntimeSnapshot();
    rt.axes[13].motionState = 5;
    rt.gantry[0].state = 5; rt.gantry[0].internalStep = 900;
    rt.gantry[0].commandResult = 4; rt.gantry[0].fault = true;
    rt.gantry[0].logicalControlAllowed = false;
    gw_.setRuntimeSnapshot(rt);
    ASSERT_TRUE(mgr_->poll());
    p.tick();

    EXPECT_TRUE(p.hasError());
    EXPECT_NE(p.diag().find("gantry permit lost"), std::string::npos);
    EXPECT_FALSE(wrote(PlcAxisCommandKind::EnableMotor, false));  // 不直接掉电
}

// ---------- GantryMotionApi：解析到逻辑轴 slot13 ----------

TEST_F(GantryPolicyTest, Api_BeginAbs_ResolvesLogicalAxisNoError) {
    setCoupledReady();
    GantryMotionApi api(*mgr_);
    auto p = api.beginAbs(g0);
    EXPECT_FALSE(p.hasError());
    EXPECT_FALSE(wrote(PlcAxisCommandKind::EnableAxis, true));  // LifecycleManaged 不使能
}

}  // namespace
}  // namespace application_vnext::policy


