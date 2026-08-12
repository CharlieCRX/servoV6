// ============================================================================
// test_axis_command.cpp —— P1 model: AxisCommand（14 触发 + 参数写 + 信封）
// ============================================================================
#include <cstddef>
#include <gtest/gtest.h>
#include <iterator>

#include "domain_vnext/model/AxisCommand.h"

namespace domain_vnext::model {
namespace {

TEST(AxisCommandKindTest, FourteenTriggers_Present) {
    // 14 项触发全部枚举到位（映射目标见 plc_vnext::PlcAxisCommandKind）。
    const AxisCommandKind kinds[] = {
        AxisCommandKind::EnableAxis,
        AxisCommandKind::ClearRelZero,
        AxisCommandKind::ClearAbsPosition,
        AxisCommandKind::TriggerAbsMove,
        AxisCommandKind::TriggerRelMove,
        AxisCommandKind::JogForward,
        AxisCommandKind::JogBackward,
        AxisCommandKind::ResetAlarm,
        AxisCommandKind::EnableMotor,
        AxisCommandKind::StopRelMove,
        AxisCommandKind::StopAbsMove,
        AxisCommandKind::SetRelZero,
        AxisCommandKind::JogHeartbeat,
        AxisCommandKind::ClearAlarmWord,
    };
    EXPECT_EQ(std::size(kinds), 14u);
}

TEST(AxisCommandTest, Defaults) {
    AxisCommand c;
    EXPECT_EQ(c.kind, AxisCommandKind::EnableAxis);
    EXPECT_FLOAT_EQ(c.value, 0.f);
    EXPECT_FALSE(c.level);
}

TEST(AxisCommandTest, CanCarryValueAndLevel) {
    AxisCommand c{AxisCommandKind::SetManualSpeed, 1.5f, false};
    EXPECT_FLOAT_EQ(c.value, 1.5f);

    AxisCommand e{AxisCommandKind::EnableMotor, 0.f, true};
    EXPECT_TRUE(e.level);
}

TEST(AxisCommandEnvelopeTest, CarriesSlotAndCommand) {
    auto slot = plc_vnext::contracts::PlcAxisSlot::tryCreate(3);
    ASSERT_TRUE(slot.has_value());
    AxisCommand cmd{AxisCommandKind::TriggerAbsMove, 0.f, false};
    AxisCommandEnvelope env{*slot, cmd};
    EXPECT_EQ(env.slot, *slot);
    EXPECT_EQ(env.cmd.kind, AxisCommandKind::TriggerAbsMove);
}

}  // namespace
}  // namespace domain_vnext::model
