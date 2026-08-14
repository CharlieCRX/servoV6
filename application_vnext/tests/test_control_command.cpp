// ============================================================================
// test_control_command.cpp —— Phase 0：统一命令模型可读名称单测
// ============================================================================
// 依据《MotionControlService —— 统一控制协调层阶段实施文档》Phase 0 验收：
//   - 枚举与结构可编译；
//   - controlSourceName / controlActionName 可读名称单测通过；
//   - 默认构造语义（UI / 组A / X / 默认 TTL）正确。
// ============================================================================
#include <string>

#include <gtest/gtest.h>

#include "application_vnext/control/ControlCommand.h"
#include "application_vnext/control/OperationState.h"

namespace application_vnext::control {
namespace {

TEST(ControlCommandTest, SourceNameIsReadable) {
    EXPECT_STREQ(controlSourceName(ControlSource::Ui), "UI");
    EXPECT_STREQ(controlSourceName(ControlSource::Joystick), "Joystick");
    EXPECT_STREQ(controlSourceName(ControlSource::Udp), "UDP");
    EXPECT_STREQ(controlSourceName(ControlSource::Maintenance), "Maintenance");
}

TEST(ControlCommandTest, ActionNameIsReadable) {
    EXPECT_STREQ(controlActionName(ControlAction::StartJogForward), "StartJogForward");
    EXPECT_STREQ(controlActionName(ControlAction::StartJogBackward), "StartJogBackward");
    EXPECT_STREQ(controlActionName(ControlAction::StopJog), "StopJog");
    EXPECT_STREQ(controlActionName(ControlAction::StartAbsMove), "StartAbsMove");
    EXPECT_STREQ(controlActionName(ControlAction::StartRelMove), "StartRelMove");
    EXPECT_STREQ(controlActionName(ControlAction::StopMotion), "StopMotion");
    EXPECT_STREQ(controlActionName(ControlAction::EnableAxis), "EnableAxis");
    EXPECT_STREQ(controlActionName(ControlAction::EnableMotor), "EnableMotor");
    EXPECT_STREQ(controlActionName(ControlAction::SetManualSpeed), "SetManualSpeed");
    EXPECT_STREQ(controlActionName(ControlAction::SetPositioningSpeed), "SetPositioningSpeed");
    EXPECT_STREQ(controlActionName(ControlAction::SetAbsTarget), "SetAbsTarget");
    EXPECT_STREQ(controlActionName(ControlAction::SetRelTarget), "SetRelTarget");
    EXPECT_STREQ(controlActionName(ControlAction::GantryEnableAndCouple), "GantryEnableAndCouple");
    EXPECT_STREQ(controlActionName(ControlAction::GantryDecoupleAndDisable), "GantryDecoupleAndDisable");
    EXPECT_STREQ(controlActionName(ControlAction::EmergencyStop), "EmergencyStop");
    EXPECT_STREQ(controlActionName(ControlAction::ReleaseEmergencyStop), "ReleaseEmergencyStop");
}

TEST(ControlCommandTest, DefaultCommandIsUiAxisXWithDefaultTtl) {
    ControlCommand c;
    EXPECT_EQ(c.source, ControlSource::Ui);
    EXPECT_EQ(c.target.group.value(), 0);          // 组 A
    EXPECT_EQ(c.target.function, domain_vnext::model::AxisFunction::X);
    EXPECT_EQ(c.ttl, std::chrono::milliseconds(1000));
    EXPECT_TRUE(c.operationId.empty());
    EXPECT_EQ(c.value, 0.0f);
    EXPECT_FALSE(c.level);
}

TEST(ControlCommandTest, AxisTargetNameFormatsGroupAndFunction) {
    AxisTarget a;
    a.group = plc_vnext::contracts::PlcGroupIndex(0);
    a.function = domain_vnext::model::AxisFunction::Y;
    EXPECT_EQ(axisTargetName(a), "A.Y");

    AxisTarget b;
    b.group = plc_vnext::contracts::PlcGroupIndex(1);
    b.function = domain_vnext::model::AxisFunction::X1;
    EXPECT_EQ(axisTargetName(b), "B.X1");
}

TEST(ControlCommandTest, StartMoveCarriesTargetAndSpeedAtomically) {
    ControlCommand c;
    c.action = ControlAction::StartAbsMove;
    c.motion = MotionRequest{123.5f, 25.0f};
    ASSERT_TRUE(c.motion.has_value());
    EXPECT_FLOAT_EQ(c.motion->target, 123.5f);
    EXPECT_FLOAT_EQ(c.motion->speed, 25.0f);

    // 默认构造：无负载（Set* 命令 / 未填 Start*Move 的调用保持 nullopt）。
    ControlCommand d;
    EXPECT_FALSE(d.motion.has_value());
}

}  // namespace
}  // namespace application_vnext::control
