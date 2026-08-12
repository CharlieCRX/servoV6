// ============================================================================
// test_command_mapper.cpp —— P4 command: domain command -> PlcAxisCommand 映射
// ============================================================================
// P4 验证点（设计稿 §8）：domain command -> PlcAxisCommand 映射。覆盖：
//   - 14 触发 + 参数写的 kind 逐项映射（§4.2 / §4.6b）；
//   - value/level 透传（参数写 -> realValue；保持电平线圈 -> boolValue）；
//   - ResetAlarm 按 PLC 能力返回 UnsupportedByPlcVersion（§7 注）；
//   - 信封映射携带槽位；
//   - A 组使能入口路由（§4.6a）：X1/X2 的 Enable* 改写为逻辑轴槽位。
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/command/CommandMapper.h"
#include "domain_vnext/model/AxisCommand.h"
#include "infrastructure/plc_vnext/contracts/PlcAxisSlot.h"

namespace domain_vnext::command {
namespace {

using model::AxisCommand;
using model::AxisCommandEnvelope;
using model::AxisCommandKind;
using model::AxisFunction;
using plc_vnext::contracts::PlcAxisCommandKind;
using plc_vnext::contracts::PlcAxisSlot;

PlcAxisSlot slotOf(int v) {
    return *PlcAxisSlot::tryCreate(v);
}

// ---------- ① kind 逐项映射 ----------

TEST(CommandMapperTest, MapKind_TriggerAndCoilKinds) {
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::EnableAxis),
              PlcAxisCommandKind::EnableAxis);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::EnableMotor),
              PlcAxisCommandKind::EnableMotor);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::JogForward),
              PlcAxisCommandKind::JogForward);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::JogBackward),
              PlcAxisCommandKind::JogBackward);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::JogHeartbeat),
              PlcAxisCommandKind::JogHeartbeat);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::TriggerAbsMove),
              PlcAxisCommandKind::TriggerAbsMove);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::TriggerRelMove),
              PlcAxisCommandKind::TriggerRelMove);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::StopAbsMove),
              PlcAxisCommandKind::StopAbsMove);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::StopRelMove),
              PlcAxisCommandKind::StopRelMove);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::ClearAbsPosition),
              PlcAxisCommandKind::ClearAbsPosition);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::ClearRelZero),
              PlcAxisCommandKind::ClearRelZero);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetRelZero),
              PlcAxisCommandKind::SetRelZero);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::ResetAlarm),
              PlcAxisCommandKind::ResetAlarm);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::ClearAlarmWord),
              PlcAxisCommandKind::ClearAlarmWord);
}

TEST(CommandMapperTest, MapKind_ParameterWriteKinds) {
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetManualSpeed),
              PlcAxisCommandKind::SetManualSpeed);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetPositioningSpeed),
              PlcAxisCommandKind::SetPositioningSpeed);
    // 领域「绝对/相对定位距离」映射为 plc「绝对/相对目标」。
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetAbsDistance),
              PlcAxisCommandKind::SetAbsTarget);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetRelDistance),
              PlcAxisCommandKind::SetRelTarget);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetSoftNegLimit),
              PlcAxisCommandKind::SetSoftNegLimit);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetSoftPosLimit),
              PlcAxisCommandKind::SetSoftPosLimit);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetSoftLimitControl),
              PlcAxisCommandKind::SetSoftLimitControl);
    EXPECT_EQ(CommandMapper::mapKind(AxisCommandKind::SetRelZeroRecord),
              PlcAxisCommandKind::SetRelZeroRecord);
}

// ---------- ② value/level 透传 ----------

TEST(CommandMapperTest, MapAxis_ParameterWriteCarriesRealValue) {
    const auto r = CommandMapper::mapAxis(
        AxisCommand{AxisCommandKind::SetManualSpeed, 12.5f, false});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.cmd.kind, PlcAxisCommandKind::SetManualSpeed);
    EXPECT_FLOAT_EQ(r.cmd.realValue, 12.5f);
    EXPECT_FALSE(r.cmd.boolValue);
}

TEST(CommandMapperTest, MapAxis_LevelCoilCarriesBoolValue) {
    const auto on = CommandMapper::mapAxis(
        AxisCommand{AxisCommandKind::JogForward, 0.f, true});
    ASSERT_TRUE(on.ok());
    EXPECT_EQ(on.cmd.kind, PlcAxisCommandKind::JogForward);
    EXPECT_TRUE(on.cmd.boolValue);

    const auto off = CommandMapper::mapAxis(
        AxisCommand{AxisCommandKind::JogForward, 0.f, false});
    ASSERT_TRUE(off.ok());
    EXPECT_FALSE(off.cmd.boolValue);
}

TEST(CommandMapperTest, MapAxis_TriggerPassesThrough) {
    const auto r = CommandMapper::mapAxis(
        AxisCommand{AxisCommandKind::TriggerRelMove, 0.f, false});
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.cmd.kind, PlcAxisCommandKind::TriggerRelMove);
}

// ---------- ③ ResetAlarm 按 PLC 能力拒绝 ----------

TEST(CommandMapperTest, MapAxis_ResetAlarm_UnsupportedByPlcVersion) {
    const auto r = CommandMapper::mapAxis(
        AxisCommand{AxisCommandKind::ResetAlarm, 0.f, false});
    EXPECT_EQ(r.status, AxisMapResult::Status::UnsupportedByPlcVersion);
    EXPECT_FALSE(r.ok());
}

// ---------- ④ 信封映射携带槽位 ----------

TEST(CommandMapperTest, MapEnvelope_CarriesSlot) {
    const auto env = AxisCommandEnvelope{
        slotOf(7), AxisCommand{AxisCommandKind::EnableMotor, 0.f, true}};
    const auto mapped = CommandMapper::mapEnvelope(env);
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped->slot, slotOf(7));
    EXPECT_EQ(mapped->cmd.kind, PlcAxisCommandKind::EnableMotor);
    EXPECT_TRUE(mapped->cmd.boolValue);
}

TEST(CommandMapperTest, MapEnvelope_UnsupportedReturnsNullopt) {
    const auto env = AxisCommandEnvelope{
        slotOf(7), AxisCommand{AxisCommandKind::ResetAlarm, 0.f, false}};
    EXPECT_FALSE(CommandMapper::mapEnvelope(env).has_value());
}

// ---------- ⑤ A 组使能入口路由（§4.6a） ----------

TEST(CommandMapperTest, EnableEntryRoute_GantryMemberToLogicalSlot) {
    // X1 龙门成员的 EnableMotor 改写为逻辑轴槽位 13。
    const auto routed = CommandMapper::effectiveEnableSlot(
        AxisFunction::X1, PlcAxisCommandKind::EnableMotor, slotOf(0),
        slotOf(13));
    EXPECT_EQ(routed, slotOf(13));
    // X2 同理。
    const auto routedX2 = CommandMapper::effectiveEnableSlot(
        AxisFunction::X2, PlcAxisCommandKind::EnableAxis, slotOf(1),
        slotOf(13));
    EXPECT_EQ(routedX2, slotOf(13));
}

TEST(CommandMapperTest, EnableEntryRoute_IndependentAxisStaysOwnSlot) {
    // Y/Z/R 独立轴即使 Enable* 也写自身槽位。
    const auto routed = CommandMapper::effectiveEnableSlot(
        AxisFunction::Y, PlcAxisCommandKind::EnableMotor, slotOf(2),
        slotOf(13));
    EXPECT_EQ(routed, slotOf(2));
    // 非使能命令不受路由影响（例如龙门成员的定位触发仍写自身槽位）。
    const auto trigger = CommandMapper::effectiveEnableSlot(
        AxisFunction::X1, PlcAxisCommandKind::TriggerAbsMove, slotOf(0),
        slotOf(13));
    EXPECT_EQ(trigger, slotOf(0));
    // 龙门成员 Enable* 但组内无逻辑轴槽位 -> 回退自身槽位。
    const auto fallback = CommandMapper::effectiveEnableSlot(
        AxisFunction::X1, PlcAxisCommandKind::EnableMotor, slotOf(0),
        std::nullopt);
    EXPECT_EQ(fallback, slotOf(0));
}

}  // namespace
}  // namespace domain_vnext::command

