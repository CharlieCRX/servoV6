// ============================================================================
// test_axis_parameter_set.cpp —— P1 model: AxisParameterSet（13 项字段 + 映射）
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/AxisParameterSet.h"

namespace domain_vnext::model {
namespace {

TEST(AxisParameterSetTest, Defaults_AreZeroAndUntrusted) {
    AxisParameterSet p;
    EXPECT_FLOAT_EQ(p.manualSpeed, 0.f);
    EXPECT_FLOAT_EQ(p.positioningSpeed, 0.f);
    EXPECT_FLOAT_EQ(p.absPosition, 0.f);
    EXPECT_FLOAT_EQ(p.relPosition, 0.f);
    EXPECT_EQ(p.motionState, 0);
    EXPECT_EQ(p.motionLimit, 0);
    EXPECT_EQ(p.alarmWord, 0u);
    EXPECT_FLOAT_EQ(p.relZeroRecord, 0.f);
    EXPECT_FLOAT_EQ(p.absMoveDistance, 0.f);
    EXPECT_FLOAT_EQ(p.relMoveDistance, 0.f);
    EXPECT_FLOAT_EQ(p.softNegLimit, 0.f);
    EXPECT_FLOAT_EQ(p.softPosLimit, 0.f);
    EXPECT_EQ(p.softLimitControl, 0u);
    EXPECT_FALSE(p.trusted);
}

TEST(AxisParameterSetTest, ThirteenFields_CanBePopulated) {
    AxisParameterSet p;
    p.manualSpeed = 1.5f;
    p.positioningSpeed = 2.5f;
    p.absPosition = 100.f;
    p.relPosition = 10.f;
    p.motionState = 4;              // MovingAbsolute
    p.motionLimit = 1;              // PositiveSoftware
    p.alarmWord = 0xABCD;
    p.relZeroRecord = 5.f;
    p.absMoveDistance = 20.f;
    p.relMoveDistance = 8.f;
    p.softNegLimit = -50.f;
    p.softPosLimit = 50.f;
    p.softLimitControl = 0x03u;
    p.trusted = true;

    EXPECT_EQ(p.motionStateEnum(), MotionState::MovingAbsolute);
    EXPECT_EQ(p.limitStateEnum(), LimitState::PositiveSoftware);
    auto sc = p.softLimitControlValue();
    EXPECT_TRUE(sc.positiveEnabled);
    EXPECT_TRUE(sc.negativeEnabled);
    EXPECT_TRUE(p.trusted);
}

TEST(AxisParameterSetTest, MotionStateMapping_AllValues) {
    AxisParameterSet p;
    struct Case { int16_t raw; MotionState expected; };
    const Case cases[] = {
        {0, MotionState::ControlNotEnabled},
        {1, MotionState::Idle},
        {2, MotionState::JogForward},
        {3, MotionState::JogBackward},
        {4, MotionState::MovingAbsolute},
        {5, MotionState::MovingRelative},
    };
    for (const auto& c : cases) {
        p.motionState = c.raw;
        EXPECT_EQ(p.motionStateEnum(), c.expected);
    }
}

TEST(AxisParameterSetTest, LimitStateMapping_AllValues) {
    AxisParameterSet p;
    struct Case { int16_t raw; LimitState expected; };
    const Case cases[] = {
        {0, LimitState::None},
        {1, LimitState::PositiveSoftware},
        {2, LimitState::NegativeSoftware},
        {3, LimitState::PositiveHardware},
        {4, LimitState::NegativeHardware},
    };
    for (const auto& c : cases) {
        p.motionLimit = c.raw;
        EXPECT_EQ(p.limitStateEnum(), c.expected);
    }
}

}  // namespace
}  // namespace domain_vnext::model
