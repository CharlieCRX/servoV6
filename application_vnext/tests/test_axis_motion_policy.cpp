// ============================================================================
// test_axis_motion_policy.cpp —— vnext 运动策略层（AbsMovePolicy/RelMovePolicy/
// JogPolicy/AxisMotionApi）TDD
// ============================================================================
// 依据《servoV6剩余迁移工作实施方案》§4.5 与真实 PLC_re motionState 语义：
//   0=轴控未使能 / 1=轴控ON电机OFF / 2=电机使能空闲(★)/ 3/4=点动正/反 /
//   5=绝对定位执行中 / 6=相对定位执行中
// 用 FakePlcRuntimeGateway 注入已解码快照，把 slot2(Y) 的 motionState 逐步推进，
// 手工驱动 tick() 验证"使能→触发→等待自然回 idle→掉电"全包络与点动"显式停止"。
// ============================================================================
#include <chrono>
#include <memory>
#include <thread>

#include <gtest/gtest.h>

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/SystemManagerVnext.h"
#include "application_vnext/policy/AbsMovePolicy.h"
#include "application_vnext/policy/AxisMotionApi.h"
#include "application_vnext/policy/JogPolicy.h"
#include "application_vnext/policy/RelMovePolicy.h"
#include "domain_vnext/model/AxisFunction.h"
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
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::PlcAxisSlot;
using plc_vnext::contracts::RuntimeSnapshot;
using plc_vnext::contracts::TopologyGroup;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeRole;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;

PlcAxisSlot slotOf(int v) { return *PlcAxisSlot::tryCreate(v); }

// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。
plc_vnext::contracts::TopologySnapshot makeSixAxisTopology() {
    auto snap = makeValidTopologySnapshot();
    TopologyGroup ga;
    ga.valid = true;
    ga.hmiVisible = true;
    ga.groupCode = 0;
    ga.roles = {
        makeRole(true, 0, 1, 0, 0, 1),      // X1
        makeRole(true, 1, 2, 0, 0, 2),      // X2
        makeRole(true, 2, 3, 0, 0, 3),      // Y
        makeRole(true, 3, 4, 0, 0, 3),      // Z
        makeRole(true, 4, 5, 0, 0, 4),      // R
        makeRole(true, 13, 0, 2, 0, 5),     // X 逻辑轴
        makeRole(false, -1, 0, 0, 0, 0),
        makeRole(false, -1, 0, 0, 0, 0),
    };
    snap.groups[0] = ga;
    return snap;
}

class AxisMotionPolicyTest : public ::testing::Test {
protected:
    void SetUp() override {
        adapter_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        mgr_ = std::make_unique<SystemManagerVnext>(*adapter_);
        gw_.setTopologySnapshot(makeSixAxisTopology());
        gw_.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
        ASSERT_TRUE(mgr_->boot());
        // 同步急停为"未急停"，解除安全锁（与 test_system_manager 一致）。
        mgr_->applyEmergencyStopFeedback(false);
        EXPECT_FALSE(mgr_->isSystemLocked());
    }

    /// 设置 slot2 的 motionState 并注入反馈（刷新 Axis::feedback）。
    void setMotion(int16_t ms) {
        auto rt = makeTrustedRuntimeSnapshot();
        rt.axes[2].motionState = ms;
        gw_.setRuntimeSnapshot(rt);
        ASSERT_TRUE(mgr_->poll());
    }

    /// 设置 slot2 的 motionState + motionLimit（D144）并注入反馈。
    void setMotionAndLimit(int16_t ms, int16_t limit) {
        auto rt = makeTrustedRuntimeSnapshot();
        rt.axes[2].motionState = ms;
        rt.axes[2].motionLimit = limit;
        gw_.setRuntimeSnapshot(rt);
        ASSERT_TRUE(mgr_->poll());
    }

    /// 带限位注入的周期推进（验证限位方向感知行为）。
    template <typename Policy>
    void cycleLimit(Policy& p, int16_t ms, int16_t limit, int delayMs = 0) {
        setMotionAndLimit(ms, limit);
        if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        p.tick();
    }

    /// 一次"刷新反馈 + 推进策略"周期（支持 Abs/Rel/Jog 各策略）。
    template <typename Policy>
    void cycle(Policy& p, int16_t ms, int delayMs = 0) {
        setMotion(ms);
        if (delayMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
        p.tick();
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

// ---------- AbsMovePolicy：使能→触发→等待自然回 idle→掉电 ----------

TEST_F(AxisMotionPolicyTest, AbsMove_EnablesTriggersWaitsDisables) {
    AbsMovePolicy p(*mgr_, slotOf(2));
    p.start();
    p.setVerifyTarget(2000.3f);  // 假快照 slot2 位置=2000，偏差0.3 < 容差0.5 → 应判到位完成
    ASSERT_FALSE(p.hasError());

    // 轴控未使能：首次 tick 下发 使能轴控 + 使能电机。
    cycle(p, /*ms=*/0);
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableAxis, true));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, true));

    // 使能完成（电机使能空闲 2）→ PostEnableDelay → 400ms 后 TriggeringMove。
    cycle(p, /*ms=*/2);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::PostEnableDelay);
    cycle(p, /*ms=*/2, /*delayMs=*/450);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::TriggeringMove);

    // 触发（TriggeringMove）→ WaitingMotionStart；下一周期观测到运动(5)→ WaitingMotionFinish。
    cycle(p, /*ms=*/5);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::WaitingMotionStart);
    cycle(p, /*ms=*/5);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::WaitingMotionFinish);

    // 运动自然结束（回 2）→ 补发 Stop 复位 busy → PostStopDelay。
    cycle(p, /*ms=*/2);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::PostStopDelay);

    // 停稳 500ms → Disabling → 掉电（使能电机 OFF）→ Done。
    cycle(p, /*ms=*/2, /*delayMs=*/550);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::Disabling);
    cycle(p, /*ms=*/2);
    EXPECT_TRUE(p.isDone());
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, false));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::TriggerAbsMove, false));
}

TEST_F(AxisMotionPolicyTest, AbsMove_StartsFromMotorOffState1_SendsEnableMotorOnly) {
    // 现场常见初始态：使能轴控已 ON、使能电机未 ON → ms=1。
    // 策略应只补"使能电机"，不再重发"使能轴控"，然后等 ms→2 推进。
    setMotion(/*ms=*/1);
    AbsMovePolicy p(*mgr_, slotOf(2));
    p.start();
    p.tick();
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::EnsuringEnabled);
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, true));
    EXPECT_FALSE(wrote(PlcAxisCommandKind::EnableAxis, true));

    // 使能电机后 ms→2 → 进入 PostEnableDelay。
    cycle(p, /*ms=*/2);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::PostEnableDelay);
}

TEST_F(AxisMotionPolicyTest, AbsMove_StateNot2_ButReachedTarget3s_Completes) {
    // 兜底：state!=2（ms 保持 5）但位置已到目标(2000)并持续 3s → 也应判成功（写 OFF 收尾）。
    AbsMovePolicy p(*mgr_, slotOf(2));
    p.setVerifyTarget(2000.f);   // 假快照 slot2 pos=2000，恰好到位
    p.start();
    ASSERT_FALSE(p.hasError());
    cycle(p, /*ms=*/0);
    cycle(p, /*ms=*/2);
    cycle(p, /*ms=*/2, /*delayMs=*/450);   // → TriggeringMove
    cycle(p, /*ms=*/5);                    // → WaitingMotionStart（过渡）
    cycle(p, /*ms=*/5);                    // → WaitingMotionFinish（观察到运动）
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::WaitingMotionFinish);

    // ms 仍为 5（非空闲）但位置已在目标：首次 tick 记录到位时刻，尚未到 3s。
    cycle(p, /*ms=*/5);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::WaitingMotionFinish);
    // 持续 3.2s 到位 → 兜底完成 → PostStopDelay。
    cycle(p, /*ms=*/5, /*delayMs=*/3200);
    EXPECT_EQ(p.currentStep(), AbsMovePolicy::Step::PostStopDelay);
}

// ---------- JogPolicy：使能→点动→显式停止→等空闲→掉电 ----------

TEST_F(AxisMotionPolicyTest, Jog_ExplicitStopThenDisable) {
    JogPolicy p(*mgr_, slotOf(2), /*forward=*/true, /*durationMs=*/200,
                /*heartbeatPeriodMs=*/1000);
    p.start();
    ASSERT_FALSE(p.hasError());

    cycle(p, /*ms=*/0);
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableAxis, true));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, true));

    cycle(p, /*ms=*/2);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::PostEnableDelay);
    cycle(p, /*ms=*/2, /*delayMs=*/450);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::IssuingJog);

    // 点动运行（motionState 3 正向点动），心跳线程维持。
    cycle(p, /*ms=*/3);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogForward, true));

    // Explicit stop must write the PLC hold bits OFF immediately on release.
    p.requestStop();
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogHeartbeat, false));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogForward, false));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogBackward, false));
    cycle(p, /*ms=*/3);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::WaitingForIdle);

    // 等空闲(2) → PostStopDelay → 掉电 → Done。
    cycle(p, /*ms=*/2);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::PostStopDelay);
    cycle(p, /*ms=*/2, /*delayMs=*/550);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::EnsuringDisabled);
    cycle(p, /*ms=*/2);
    EXPECT_TRUE(p.isDone());
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, false));
}

// ---------- JogPolicy：限位方向感知（负限位下正向撤离不被误停）----------
// 回归：轴到负限位后 motionLimit(D144)=2 仍锁存（回差区 0.1 未退出），
// 若按方向无关的 limit!=0 判定会在撤离时立即停机（卡顿），这里验证撤离方向允许继续。

TEST_F(AxisMotionPolicyTest, Jog_ForwardWithdrawAtNegativeLimit_KeepsJogging) {
    JogPolicy p(*mgr_, slotOf(2), /*forward=*/true, /*durationMs=*/0,
                /*heartbeatPeriodMs=*/1000);
    p.start();
    ASSERT_FALSE(p.hasError());
    cycle(p, /*ms=*/0);                          // → EnsuringEnabled
    cycle(p, /*ms=*/2);                          // → PostEnableDelay
    cycle(p, /*ms=*/2, /*delayMs=*/450);         // → IssuingJog
    cycle(p, /*ms=*/3);                          // → Jogging
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);

    // 负软限位（motionLimit=2）激活：正向点动为撤离方向，必须允许继续。
    cycleLimit(p, /*ms=*/3, /*limit=*/2);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);

    // 持续退出回差区域（motionLimit 归 0）后仍保持点动。
    cycleLimit(p, /*ms=*/3, /*limit=*/0);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);
}

TEST_F(AxisMotionPolicyTest, Jog_BackwardBlockedAtNegativeLimit_Stops) {
    JogPolicy p(*mgr_, slotOf(2), /*forward=*/false, /*durationMs=*/0,
                /*heartbeatPeriodMs=*/1000);
    p.start();
    ASSERT_FALSE(p.hasError());
    cycle(p, /*ms=*/0);
    cycle(p, /*ms=*/2);
    cycle(p, /*ms=*/2, /*delayMs=*/450);
    cycle(p, /*ms=*/4);                          // 反向点动
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);

    // 负限位阻挡反向点动：应立即停止。
    cycleLimit(p, /*ms=*/4, /*limit=*/2);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::IssuingStop);
}

TEST_F(AxisMotionPolicyTest, Jog_ForwardBlockedAtPositiveLimit_Stops) {
    JogPolicy p(*mgr_, slotOf(2), /*forward=*/true, /*durationMs=*/0,
                /*heartbeatPeriodMs=*/1000);
    p.start();
    ASSERT_FALSE(p.hasError());
    cycle(p, /*ms=*/0);
    cycle(p, /*ms=*/2);
    cycle(p, /*ms=*/2, /*delayMs=*/450);
    cycle(p, /*ms=*/3);                          // 正向点动
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::Jogging);

    // 正软限位（motionLimit=1）阻挡正向点动：应立即停止。
    cycleLimit(p, /*ms=*/3, /*limit=*/1);
    EXPECT_EQ(p.currentStep(), JogPolicy::Step::IssuingStop);
}

// ---------- AxisMotionApi：slot→AxisFunction 解析 + 阻塞便利 ----------

TEST_F(AxisMotionPolicyTest, Api_ResolvesSlotToFunction) {
    AxisMotionApi api(*mgr_);
    AxisMotionApi::Target t;
    ASSERT_TRUE(appResultOk(api.targetFor(slotOf(2), t)));
    EXPECT_EQ(t.function, AxisFunction::Y);
    EXPECT_EQ(t.group.value(), 0);
}

TEST_F(AxisMotionPolicyTest, Api_RunAbs_NoMotionObserved_ReportsError) {
    // 轴保持空闲(2)且位置不变：策略不应冒充"已运动完成"，应报"运动未启动"错误。
    setMotion(2);
    AxisMotionApi api(*mgr_);
    // timeoutMs 需大于策略内部"运动未启动"(5s)超时，才能等到 Error 及诊断。
    const auto o = api.runAbs(slotOf(2), /*target=*/100.f, /*timeoutMs=*/8000);
    EXPECT_FALSE(o.ok);
    EXPECT_FALSE(o.diag.empty());
    EXPECT_NE(o.diag.find("never started"), std::string::npos);
}

TEST_F(AxisMotionPolicyTest, Api_RunJog_CompletesWithExplicitStop) {
    setMotion(2);
    AxisMotionApi api(*mgr_);
    const auto o = api.runJog(slotOf(2), /*forward=*/true, /*durationMs=*/200);
    EXPECT_TRUE(o.ok) << o.diag;
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogForward, true));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::JogForward, false));
    EXPECT_TRUE(wrote(PlcAxisCommandKind::EnableMotor, false));
}

TEST_F(AxisMotionPolicyTest, Api_RunAbs_UnboundSlot_ReportsError) {
    // 拓扑未绑定 slot5（Role 无绑定）：targetFor 返回 AxisNotFound。
    AxisMotionApi api(*mgr_);
    AxisMotionApi::Target t;
    const auto r = api.targetFor(slotOf(5), t);
    EXPECT_FALSE(appResultOk(r));
}
}  // namespace
}  // namespace application_vnext::policy
