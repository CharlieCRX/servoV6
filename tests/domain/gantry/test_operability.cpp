/**
 * @file test_operability.cpp
 * @brief Operability 枚举单元测试 (T1.2)
 *
 * 测试用例源自: domain/docs/下一阶段TDD开发任务.md
 *
 * 测试编号:
 *   TS1.2.1 - Operability 枚举可比较
 *   TS1.2.2 - Allowed 表示通过
 */

#include <gtest/gtest.h>
#include "domain/value/Operability.h"

using namespace GantryValue;

// ─────────────────────────────────────────
// TS1.2.1: Operability 枚举可比较
// ─────────────────────────────────────────
TEST(OperabilityTest, EnumsAreComparable) {
    EXPECT_NE(Operability::Allowed, Operability::AlarmActive);
    EXPECT_NE(Operability::Allowed, Operability::LimitTriggered);
    EXPECT_NE(Operability::Allowed, Operability::LimitBlocksDirection);
    EXPECT_NE(Operability::Allowed, Operability::NotEnabled);
    EXPECT_NE(Operability::Allowed, Operability::TargetNotOperableInMode);
    EXPECT_NE(Operability::Allowed, Operability::CommandSlotBusy);
    EXPECT_NE(Operability::Allowed, Operability::NotIdle);
    EXPECT_NE(Operability::Allowed, Operability::DeviationExceeded);

    EXPECT_EQ(Operability::AlarmActive, Operability::AlarmActive);
    EXPECT_EQ(Operability::LimitTriggered, Operability::LimitTriggered);
}

// ─────────────────────────────────────────
// TS1.2.2: Allowed 表示通过（为唯一通过值）
// ─────────────────────────────────────────
TEST(OperabilityTest, AllowedIsTheOnlyPassValue) {
    // 只有 Allowed 表示操作通过
    auto isAllowed = [](Operability op) { return op == Operability::Allowed; };

    EXPECT_TRUE(isAllowed(Operability::Allowed));

    EXPECT_FALSE(isAllowed(Operability::TargetNotOperableInMode));
    EXPECT_FALSE(isAllowed(Operability::AlarmActive));
    EXPECT_FALSE(isAllowed(Operability::LimitTriggered));
    EXPECT_FALSE(isAllowed(Operability::LimitBlocksDirection));
    EXPECT_FALSE(isAllowed(Operability::NotEnabled));
    EXPECT_FALSE(isAllowed(Operability::CommandSlotBusy));
    EXPECT_FALSE(isAllowed(Operability::NotIdle));
    EXPECT_FALSE(isAllowed(Operability::DeviationExceeded));
}
