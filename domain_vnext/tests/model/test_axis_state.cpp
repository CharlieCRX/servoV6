// ============================================================================
// test_axis_state.cpp —— P1 model: MotionState / LimitState / SoftLimitControl
// ============================================================================
#include <cstdint>
#include <gtest/gtest.h>

#include "domain_vnext/model/AxisState.h"

namespace domain_vnext::model {
namespace {

TEST(MotionStateTest, Encodings_MatchAddressTable) {
    EXPECT_EQ(static_cast<uint16_t>(MotionState::ControlNotEnabled), 0u);
    EXPECT_EQ(static_cast<uint16_t>(MotionState::Idle), 1u);
    EXPECT_EQ(static_cast<uint16_t>(MotionState::JogForward), 2u);
    EXPECT_EQ(static_cast<uint16_t>(MotionState::JogBackward), 3u);
    EXPECT_EQ(static_cast<uint16_t>(MotionState::MovingAbsolute), 4u);
    EXPECT_EQ(static_cast<uint16_t>(MotionState::MovingRelative), 5u);
}

TEST(LimitStateTest, Encodings_MatchAddressTable) {
    EXPECT_EQ(static_cast<uint16_t>(LimitState::None), 0u);
    EXPECT_EQ(static_cast<uint16_t>(LimitState::PositiveSoftware), 1u);
    EXPECT_EQ(static_cast<uint16_t>(LimitState::NegativeSoftware), 2u);
    EXPECT_EQ(static_cast<uint16_t>(LimitState::PositiveHardware), 3u);
    EXPECT_EQ(static_cast<uint16_t>(LimitState::NegativeHardware), 4u);
}

TEST(SoftLimitControlTest, Decode_Bit0PosBit1Neg) {
    auto c = SoftLimitControl::decode(0x01u);
    EXPECT_TRUE(c.positiveEnabled);
    EXPECT_FALSE(c.negativeEnabled);

    c = SoftLimitControl::decode(0x02u);
    EXPECT_FALSE(c.positiveEnabled);
    EXPECT_TRUE(c.negativeEnabled);

    c = SoftLimitControl::decode(0x03u);
    EXPECT_TRUE(c.positiveEnabled);
    EXPECT_TRUE(c.negativeEnabled);

    c = SoftLimitControl::decode(0x00u);
    EXPECT_FALSE(c.positiveEnabled);
    EXPECT_FALSE(c.negativeEnabled);
}

TEST(SoftLimitControlTest, Encode_RoundTrip) {
    EXPECT_EQ((SoftLimitControl{false, false}).encode(), 0u);
    EXPECT_EQ((SoftLimitControl{true, false}).encode(), 0x01u);
    EXPECT_EQ((SoftLimitControl{false, true}).encode(), 0x02u);
    EXPECT_EQ((SoftLimitControl{true, true}).encode(), 0x03u);
}

}  // namespace
}  // namespace domain_vnext::model
