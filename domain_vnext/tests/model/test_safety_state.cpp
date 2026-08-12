// ============================================================================
// test_safety_state.cpp —— P1 model: SafetyState（急停五态）
// ============================================================================
#include <cstddef>
#include <gtest/gtest.h>
#include <iterator>
#include <string>

#include "domain_vnext/model/SafetyState.h"

namespace domain_vnext::model {
namespace {

TEST(SafetyStateTest, FiveStates_Present) {
    const SafetyState states[] = {
        SafetyState::NotSynchronized,
        SafetyState::Running,
        SafetyState::EmergencyStopping,
        SafetyState::EmergencyStopped,
        SafetyState::ReleasingEmergencyStop,
    };
    EXPECT_EQ(std::size(states), 5u);
}

TEST(SafetyStateTest, Name_IsStable) {
    EXPECT_EQ(std::string(safetyStateName(SafetyState::NotSynchronized)), "NotSynchronized");
    EXPECT_EQ(std::string(safetyStateName(SafetyState::Running)), "Running");
    EXPECT_EQ(std::string(safetyStateName(SafetyState::EmergencyStopping)), "EmergencyStopping");
    EXPECT_EQ(std::string(safetyStateName(SafetyState::EmergencyStopped)), "EmergencyStopped");
    EXPECT_EQ(std::string(safetyStateName(SafetyState::ReleasingEmergencyStop)), "ReleasingEmergencyStop");
}

}  // namespace
}  // namespace domain_vnext::model
