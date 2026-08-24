// ============================================================================
// test_motion_controller.cpp -- Phase 4: 摇杆改造（MotionController 提交 ControlCommand）
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 4 验收：
//   - 摇杆按住 → Joystick 源 StartJog（StartJogForward/Backward）被提交并由统一
//     协调层仲裁/执行（Accepted → 会话）；
//   - 松开 → 提交 StopJog，只停本会话（owner 过滤），不误停他轴；
//   - 跨轴切换 → 旧轴 StopJog + 新轴 StartJog（跨轴跳跃保护）。
// 同时覆盖 JoystickCommandBuilder 纯命令构造（无 Qt，字段正确）。
//
// 依赖：presentation（MotionController/AxisSelectionModel/GamepadInputInterpreter）
// + application_vnext（MotionControlService/PlcRuntimeDriverAdapter）+ FakeControlRuntime
// + FakePlcRuntimeGateway（六轴拓扑，Y/Z/R/X 均可仲裁）。
// ============================================================================
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <QCoreApplication>

#include <gtest/gtest.h>

#include "presentation/input/MotionController.h"
#include "presentation/input/AxisSelectionModel.h"
#include "presentation/input/GamepadInputInterpreter.h"
#include "presentation/input/InputEvent.h"
#include "presentation/input/JoystickCommandBuilder.h"

#include "application_vnext/PlcRuntimeDriverAdapter.h"
#include "application_vnext/control/MotionControlService.h"
#include "fake/FakeControlRuntime.h"

#include "domain_vnext/model/GantryParam.h"
#include "infrastructure/plc_vnext/fake/FakePlcRuntimeGateway.h"
#include "infrastructure/plc_vnext/fake/PlcFixtureBuilder.h"

namespace {

using application_vnext::PlcRuntimeDriverAdapter;
using application_vnext::control::ControlAction;
using application_vnext::control::ControlSource;
using application_vnext::control::FakeControlRuntime;
using application_vnext::control::MotionControlService;
using application_vnext::control::OperationState;
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::SafetySnapshot;
using plc_vnext::fake::FakePlcRuntimeGateway;
using plc_vnext::fake::makeGantryStatusSnapshot;
using plc_vnext::fake::makeRole;
using plc_vnext::fake::makeTrustedRuntimeSnapshot;
using plc_vnext::fake::makeValidTopologySnapshot;
using presentation::input::joystick::joystickGroup;
using presentation::input::joystick::makeJogBackwardCommand;
using presentation::input::joystick::makeJogForwardCommand;
using presentation::input::joystick::makePositionCommand;
using presentation::input::joystick::makeStopJogCommand;

/// gtest 不提供 main；MotionController 等 QObject 需要 QCoreApplication。local-static 保证一次。
QCoreApplication* ensureApp() {
    static int argc = 1;
    static char a0[] = "presentation_tests";
    static char* argv[] = { a0, nullptr };
    static QCoreApplication app(argc, argv);
    return &app;
}

/// A 组 6 功能全绑定拓扑：X1(0)/X2(1)/Y(2)/Z(3)/R(4)/X 逻辑轴(13)。Y/Z/R 供摇杆可仲裁。
plc_vnext::contracts::TopologySnapshot makeSixAxisTopology() {
    auto snap = makeValidTopologySnapshot();
    plc_vnext::contracts::TopologyGroup ga;
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

/// A/B 两组均绑定六功能的拓扑。B 组使用独立槽位，避免跨组重复 PlcAxisIndex。
plc_vnext::contracts::TopologySnapshot makeDualGroupSixAxisTopology() {
    auto snap = makeSixAxisTopology();
    plc_vnext::contracts::TopologyGroup gb;
    gb.valid = true;
    gb.hmiVisible = true;
    gb.groupCode = 1;
    gb.roles = {
        makeRole(true, 5, 6, 0, 0, 1),      // X1
        makeRole(true, 6, 7, 0, 0, 2),      // X2
        makeRole(true, 7, 8, 0, 0, 3),      // Y
        makeRole(true, 8, 9, 0, 0, 3),      // Z
        makeRole(true, 9, 10, 0, 0, 4),     // R
        makeRole(true, 14, 0, 2, 0, 5),     // X 逻辑轴
        makeRole(false, -1, 0, 0, 0, 0),
        makeRole(false, -1, 0, 0, 0, 0),
    };
    snap.groups[1] = gb;
    return snap;
}

/// 真实 service + 六轴拓扑，摇杆提交的 Joystick 源命令可被仲裁执行。
class MotionControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        ensureApp();
        gw_.setTopologySnapshot(makeSixAxisTopology());
        runtime_.setRuntimeSnapshot(makeTrustedRuntimeSnapshot());
        SafetySnapshot s;
        s.trusted = true;
        s.emergencyStop = false;
        runtime_.setSafetySnapshot(s);
        runtime_.setConnected(true);
        driver_ = std::make_unique<PlcRuntimeDriverAdapter>(gw_);
        svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
        svc_->tick();  // boot + 首读可信 → 解除全局锁定
    }

    FakePlcRuntimeGateway            gw_;
    FakeControlRuntime               runtime_;
    std::unique_ptr<PlcRuntimeDriverAdapter> driver_;
    std::unique_ptr<MotionControlService>   svc_;
};

/// 构造一个 Motion 事件。
InputEvent motionEvent(MotionDirection dir, MotionEventType type) {
    InputEvent e;
    e.type = InputEvent::Type::Motion;
    e.motionDir = dir;
    e.motionType = type;
    return e;
}


// ---------- 测试 A：JoystickCommandBuilder 纯命令构造（无 Qt） ----------

TEST(JoystickCommandBuilderTest, BuildsJoystickJogAndPositionCommands) {
    application_vnext::control::AxisTarget t;
    t.group = joystickGroup();
    t.function = domain_vnext::model::AxisFunction::Y;

    auto fwd = makeJogForwardCommand(t);
    EXPECT_EQ(fwd.source, ControlSource::Joystick);
    EXPECT_EQ(fwd.action, ControlAction::StartJogForward);
    EXPECT_EQ(fwd.target.function, domain_vnext::model::AxisFunction::Y);

    auto bwd = makeJogBackwardCommand(t);
    EXPECT_EQ(bwd.source, ControlSource::Joystick);
    EXPECT_EQ(bwd.action, ControlAction::StartJogBackward);

    auto stop = makeStopJogCommand(t);
    EXPECT_EQ(stop.source, ControlSource::Joystick);
    EXPECT_EQ(stop.action, ControlAction::StopJog);

    auto pos = makePositionCommand(t, /*abs=*/true, 25.0f, 5.0f);
    EXPECT_EQ(pos.source, ControlSource::Joystick);
    EXPECT_EQ(pos.action, ControlAction::StartAbsMove);
    ASSERT_TRUE(pos.motion.has_value());
    EXPECT_FLOAT_EQ(pos.motion->target, 25.0f);
    EXPECT_FLOAT_EQ(pos.motion->speed, 5.0f);

    auto rel = makePositionCommand(t, /*abs=*/false, 1.5f, /*speed=*/60.0f);
    EXPECT_EQ(rel.action, ControlAction::StartRelMove);
    ASSERT_TRUE(rel.motion.has_value());
    EXPECT_FLOAT_EQ(rel.motion->target, 1.5f);
    EXPECT_FLOAT_EQ(rel.motion->speed, 60.0f);

    // P0 回归：定位命令必须原子携带速度，绝不允许 speed=0 覆盖 PLC 定位速度。
    auto relFast = makePositionCommand(t, /*abs=*/false, 1.5f, /*speed=*/80.0f);
    ASSERT_TRUE(relFast.motion.has_value());
    EXPECT_FLOAT_EQ(relFast.motion->speed, 80.0f);
}

// ---------- 测试 1：摇杆按住 → Joystick 源 StartJog 被提交并被接受 ----------

TEST_F(MotionControllerTest, HoldSubmitsJoystickStartJogAndAccepted) {
    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());

    // 摇杆按住正方向（Forward Pressed）
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));

    // 命令应立即入队（Queued），尚未被 tick 取走前 queuedCount>0
    EXPECT_GT(svc_->queuedCount(), 0u);

    // 唯一 tick 仲裁并执行
    svc_->tick();

    bool found = false;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Y") {
            found = true;
            EXPECT_NE(op.state, OperationState::Queued);  // 已被仲裁（Accepted/Running）
        }
    }
    EXPECT_TRUE(found) << "Joystick 源 A.Y 点动操作应出现在快照中";
}

// ---------- 测试 1b：切到 B 组后，点动逻辑 X 走 B.X / group=1 ----------

TEST_F(MotionControllerTest, GroupBLogicalXJogTargetsGroupOneGantry) {
    gw_.setTopologySnapshot(makeDualGroupSixAxisTopology());
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[14].motionState = 2;  // B.X 逻辑轴已电机使能空闲，生命周期可进入 Couple 提交。
    r.gantry[1] = makeGantryStatusSnapshot(1, /*state=*/1, /*ackSeq=*/0,
                                           /*commandResult=*/0,
                                           /*x1InGear=*/false,
                                           /*x2InGear=*/false);
    runtime_.setRuntimeSnapshot(r);
    svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
    svc_->tick();
    domain_vnext::model::GantryParamModel cfg;
    cfg.valid = true;
    svc_->applyGantryConfig(plc_vnext::contracts::PlcGroupIndex(1), cfg);

    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());

    axisModel.setCurrentGroupByName(QStringLiteral("Machine_B"));
    axisModel.setCurrentAxisByName(QStringLiteral("X"));
    mc.setJogActiveDirection(1);

    for (int i = 0; i < 80 && gw_.gantrySubmissions().empty(); ++i) {
        svc_->tick();
    }

    bool foundBOperation = false;
    std::string bOpDiag;
    int bOpState = -1;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "B.X") {
            foundBOperation = true;
            bOpDiag = op.diag;
            bOpState = static_cast<int>(op.state);
            break;
        }
    }
    EXPECT_TRUE(foundBOperation) << "B 组 UI/摇杆点动逻辑 X 必须提交为 B.X，而不是 A.X";

    const auto submissions = gw_.gantrySubmissions();
    ASSERT_FALSE(submissions.empty())
        << "B.X 逻辑轴点动应提交 B 组龙门生命周期请求"
        << " opState=" << bOpState << " diag=" << bOpDiag;
    EXPECT_EQ(submissions.front().group.value(), 1);
}

// ---------- 测试 1c：B 组逻辑 X 绝对定位触发必须写 B.X(slot14)，不能落回 A.X(slot13) ----------

TEST_F(MotionControllerTest, GroupBLogicalXAbsMoveTriggersSlot14) {
    gw_.setTopologySnapshot(makeDualGroupSixAxisTopology());
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[14].motionState = 2;
    r.gantry[1] = makeGantryStatusSnapshot(1, /*state=*/1, /*ackSeq=*/0,
                                           /*commandResult=*/0,
                                           /*x1InGear=*/false,
                                           /*x2InGear=*/false);
    runtime_.setRuntimeSnapshot(r);
    svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
    svc_->tick();
    domain_vnext::model::GantryParamModel cfg;
    cfg.valid = true;
    svc_->applyGantryConfig(plc_vnext::contracts::PlcGroupIndex(1), cfg);

    application_vnext::control::ControlCommand move;
    move.source = ControlSource::Ui;
    move.target.group = plc_vnext::contracts::PlcGroupIndex(1);
    move.target.function = domain_vnext::model::AxisFunction::X;
    move.action = ControlAction::StartAbsMove;
    move.motion = application_vnext::control::MotionRequest{100.0f, 5.0f};
    svc_->submit(move);

    for (int i = 0; i < 80 && gw_.gantrySubmissions().empty(); ++i) {
        svc_->tick();
    }
    const auto submissions = gw_.gantrySubmissions();
    ASSERT_FALSE(submissions.empty());
    ASSERT_EQ(submissions.front().group.value(), 1);

    r.gantry[1].state = 3;
    r.gantry[1].internalStep = 80;
    r.gantry[1].ackSeq = submissions.front().req.requestSeq;
    r.gantry[1].commandResult = 2;
    r.gantry[1].commandErrorCode = 0;
    r.gantry[1].x1InGear = true;
    r.gantry[1].x2InGear = true;
    r.gantry[1].logicalControlAllowed = true;
    r.gantry[1].memberControlAllowed = false;
    r.gantry[1].readyToCouple = false;
    r.gantry[1].readyToDecouple = true;
    r.axes[14].motionState = 2;
    runtime_.setRuntimeSnapshot(r);

    svc_->tick();  // Coupling -> Moving, move PostEnableDelay starts.
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    svc_->tick();  // PostEnableDelay -> TriggeringMove
    svc_->tick();  // TriggeringMove -> write TriggerAbsMove

    bool triggerSlot14 = false;
    bool triggerSlot13 = false;
    for (const auto& w : gw_.writtenAxis()) {
        if (w.cmd.kind == PlcAxisCommandKind::TriggerAbsMove) {
            if (w.slot.value() == 14) triggerSlot14 = true;
            if (w.slot.value() == 13) triggerSlot13 = true;
        }
    }
    EXPECT_TRUE(triggerSlot14) << "B.X 绝对定位应触发 B 组逻辑轴 slot14";
    EXPECT_FALSE(triggerSlot13) << "B.X 绝对定位不得落回 A 组逻辑轴 slot13";
}

// ---------- 测试 2：松开 → StopJog 只停本会话（owner 过滤） ----------

TEST_F(MotionControllerTest, ReleaseStopsOwnJogSessionOnly) {
    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());

    // 使 Y(slot2) 使能空闲，驱动点动会话真正进入 Jogging（与 Phase3 测试一致）
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].motionState = 2;
    runtime_.setRuntimeSnapshot(r);

    // 按住 → StartJog(Y)
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));
    svc_->tick();  // 建会话 -> PostEnableDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    svc_->tick();  // PostEnableDelay -> IssuingJog
    svc_->tick();  // IssuingJog -> Jogging

    std::string jogId;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Y") {
            jogId = op.operationId;
        }
    }
    ASSERT_FALSE(jogId.empty()) << "应已建立 Y 轴点动会话";
    EXPECT_EQ(svc_->queryOperation(jogId)->state, OperationState::Running);

    // 松开 → StopJog（只停本会话，owner 过滤）
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Released));
    svc_->tick();  // Jogging -> IssuingStop
    svc_->tick();  // -> WaitingForIdle
    svc_->tick();  // -> PostStopDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // 过 PostStopDelay(0.5s)
    svc_->tick();  // -> EnsuringDisabled
    svc_->tick();  // -> Done -> Cancelled

    EXPECT_EQ(svc_->queryOperation(jogId)->state, OperationState::Cancelled);
}

// ---------- 测试 3：跨轴切换 → 旧轴 StopJog + 新轴 StartJog ----------

TEST_F(MotionControllerTest, CrossAxisSwitchStopsOldAndStartsNew) {
    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());

    // 使 Y(slot2)、Z(slot3) 均使能空闲，便于两轴点动会话真正进入 Jogging
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].motionState = 2;  // Y
    r.axes[3].motionState = 2;  // Z
    runtime_.setRuntimeSnapshot(r);

    // 在 Y 上按住正方向，并推进到 Jogging（Running）
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));  // StartJog(Y)
    svc_->tick();  // 建会话 -> PostEnableDelay
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    svc_->tick();  // -> IssuingJog
    svc_->tick();  // -> Jogging

    std::string yJogId;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Y") {
            yJogId = op.operationId;
        }
    }
    ASSERT_FALSE(yJogId.empty()) << "应先建立 Y 轴点动会话";
    EXPECT_EQ(svc_->queryOperation(yJogId)->state, OperationState::Running);

    // 摇杆仍按着时切换到 Z → 跨轴跳跃保护：先 StopJog(Y)，再 StartJog(Z)
    mc.onCurrentAxisChanged(AxisId::Z);
    svc_->tick();  // Y:IssuingStop；Z:建会话 -> PostEnableDelay
    svc_->tick();  // Y:WaitingForIdle；Z:(PostEnableDelay 需 sleep)
    std::this_thread::sleep_for(std::chrono::milliseconds(450));  // 过 Z PostEnableDelay(0.4s)
    svc_->tick();  // Y:PostStopDelay；Z:IssuingJog
    svc_->tick();  // Z:Jogging；Y:(PostStopDelay 需 sleep)
    std::this_thread::sleep_for(std::chrono::milliseconds(600));  // 过 Y PostStopDelay(0.5s)
    svc_->tick();  // Y:EnsuringDisabled
    svc_->tick();  // Y:Done -> Cancelled

    // 旧 Y 会话最终被停止（StopJog 真正执行，而非仅“存在 A.Y 操作”）
    auto ySt = svc_->queryOperation(yJogId);
    ASSERT_TRUE(ySt.has_value());
    EXPECT_EQ(ySt->state, OperationState::Cancelled) << "跨轴时旧轴 Y 的 StopJog 应使其会话结束";

    // 新 Z 会话进入 Running（StartJog 真正被执行）
    bool zRunning = false;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Z") {
            if (op.state == OperationState::Running) zRunning = true;
        }
    }
    EXPECT_TRUE(zRunning) << "跨轴后新轴 Z 的 StartJog 应进入 Running";
}

// ---------- 测试 3b：跨组切换 → 旧组 StopJog + 新组 StartJog，无需右摇杆回中 ----------

TEST_F(MotionControllerTest, CrossGroupSwitchWhileHoldingJogStopsOldAndStartsNew) {
    gw_.setTopologySnapshot(makeDualGroupSixAxisTopology());
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[13].motionState = 2;
    r.axes[14].motionState = 2;
    runtime_.setRuntimeSnapshot(r);
    svc_ = std::make_unique<MotionControlService>(*driver_, runtime_);
    svc_->tick();

    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());
    axisModel.setCurrentAxisByName(QStringLiteral("X"));

    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));
    ASSERT_EQ(svc_->queuedCount(), 1u);

    axisModel.setCurrentGroupByName(QStringLiteral("Machine_B"));
    EXPECT_EQ(svc_->queuedCount(), 3u)
        << "切组时应追加 StopJog(A.X) + StartJog(B.X)，右摇杆无需回中再推";

    svc_->tick();

    int axOps = 0;
    int bxOps = 0;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.X") ++axOps;
        if (op.source == ControlSource::Joystick && op.axis == "B.X") ++bxOps;
    }
    EXPECT_GE(axOps, 2) << "旧 A.X 应包含原 StartJog 与切组 StopJog";
    EXPECT_GE(bxOps, 1) << "新 B.X 应立即收到重放 StartJog";
}

// ---------- 测试 4：SetAbsTarget 预填目标投影到快照（Position 摇杆目标来源 §5.3） ----------

TEST_F(MotionControllerTest, SetAbsTargetProjectsToSnapshot) {
    application_vnext::control::ControlCommand set;
    set.source = ControlSource::Ui;
    set.target.group = joystickGroup();
    set.target.function = domain_vnext::model::AxisFunction::Y;
    set.action = ControlAction::SetAbsTarget;
    set.value = 123.5f;
    svc_->submit(set);
    svc_->tick();

    float got = 0.0f;
    for (const auto& a : svc_->store().snapshot().axes) {
        if (a.group == set.target.group && a.role == set.target.function) {
            got = a.absMoveTarget;
            break;
        }
    }
    EXPECT_FLOAT_EQ(got, 123.5f) << "SetAbsTarget 应记录为快照 absMoveTarget 供摇杆读取";
}

// ---------- 测试 5：定位速度来自 PLC 反馈快照（P0：摇杆定位不得以 speed=0 覆盖 PLC 速度） ----------

TEST_F(MotionControllerTest, PositioningSpeedProjectsToSnapshot) {
    // 定位速度以 PLC 反馈为准：设 Y(slot2) 的反馈定位速度为 80，快照应如实投影，
    // 供摇杆 Position 模式经 handlePositionMotion 读取并原子携带（绝不写 0）。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].positioningSpeed = 80.0f;  // Y(slot2)
    runtime_.setRuntimeSnapshot(r);
    svc_->tick();

    float got = 0.0f;
    for (const auto& a : svc_->store().snapshot().axes) {
        if (a.group == joystickGroup() && a.role == domain_vnext::model::AxisFunction::Y) {
            got = a.positioningSpeed;
            break;
        }
    }
    EXPECT_FLOAT_EQ(got, 80.0f)
        << "快照 positioningSpeed 应反映 PLC 反馈定位速度，供摇杆 Position 模式原子携带";
}

// ---------- 测试 6：摇杆 Position 真实触发后，最终命令携带快照正速度（不被 speed 拒绝） ----------

TEST_F(MotionControllerTest, PositionSubmitCarriesPositiveSpeed) {
    // 快照 Y 定位速度 = 80（PLC 反馈）；轴绑定且可信（六轴拓扑 + trusted 快照）。
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].positioningSpeed = 80.0f;  // Y(slot2)
    runtime_.setRuntimeSnapshot(r);
    svc_->tick();  // 刷新快照

    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());
    mc.setControlMode(1);  // Position 模式
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Released));  // 触发 StartAbsMove
    svc_->tick();  // 协调层 execute：speed=80>0，应接受；若写成 0 会被权威拒绝

    bool found = false, speedRejected = false;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Y") {
            found = true;
            if (op.diag.find("speed") != std::string::npos) speedRejected = true;
        }
    }
    EXPECT_TRUE(found) << "应提交 Joystick 源 A.Y 定位命令";
    EXPECT_FALSE(speedRejected)
        << "摇杆读取到快照正速度(80)并原子携带，协调层不得以 speed 为由拒绝";
}

// ---------- 测试 7：协调层权威校验 —— MotionRequest.speed==0 被拒绝且不占租约 ----------

TEST_F(MotionControllerTest, ZeroSpeedPositionRejectedAndNoLease) {
    application_vnext::control::ControlCommand move;
    move.source = ControlSource::Udp;
    move.target.group = joystickGroup();
    move.target.function = domain_vnext::model::AxisFunction::Y;
    move.action = ControlAction::StartRelMove;
    move.motion = application_vnext::control::MotionRequest{50.0f, /*speed=*/0.0f};
    const auto id = svc_->submit(move);
    svc_->tick();

    auto st = svc_->queryOperation(id);
    ASSERT_TRUE(st.has_value());
    EXPECT_EQ(st->state, OperationState::Failed);
    EXPECT_NE(st->diag.find("speed"), std::string::npos)
        << "speed=0 应以权威校验理由 Failed";

    // 不占租约：快照中 Y 轴不得显示被租约占用
    bool leased = false;
    for (const auto& a : svc_->store().snapshot().axes) {
        if (a.group == joystickGroup() && a.role == domain_vnext::model::AxisFunction::Y) {
            leased = a.leased;
            break;
        }
    }
    EXPECT_FALSE(leased) << "speed=0 的定位被拒绝后不得占用资源租约";
}

// ---------- 测试 8：快照目标轴不可信 → 摇杆不提交定位命令 ----------

TEST_F(MotionControllerTest, PositionSkippedWhenAxisUntrusted) {
    // 使 Y 反馈不可信（快照仍能找到该轴，但 ready=false）
    auto r = makeTrustedRuntimeSnapshot();
    r.axes[2].positioningSpeed = 80.0f;
    r.axes[2].trusted = false;  // Y(slot2) 不可信
    runtime_.setRuntimeSnapshot(r);
    svc_->tick();

    AxisSelectionModel axisModel;
    GamepadInputInterpreter interpreter;
    MotionController mc(&interpreter, &axisModel, svc_.get());
    mc.setControlMode(1);  // Position
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Pressed));
    mc.onInputEvent(motionEvent(MotionDirection::Forward, MotionEventType::Released));
    svc_->tick();

    // 摇杆侧应跳过提交：快照无 Joystick 源 A.Y 定位操作（而非“提交后 Rejected”）
    bool found = false;
    for (const auto& op : svc_->store().snapshot().operations) {
        if (op.source == ControlSource::Joystick && op.axis == "A.Y") {
            found = true;
        }
    }
    EXPECT_FALSE(found) << "目标轴不可信时摇杆不应提交定位命令";
}

}  // namespace
