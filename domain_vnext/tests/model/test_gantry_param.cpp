// ============================================================================
// test_gantry_param.cpp —— P1 model: GantryParam（纯配置领域模型）
// ============================================================================
#include <gtest/gtest.h>

#include "domain_vnext/model/GantryParam.h"

namespace domain_vnext::model {
namespace {

TEST(GantryParamModelTest, Defaults) {
    GantryParamModel p;
    EXPECT_FALSE(p.valid);
    EXPECT_FALSE(p.readyToCouple);
    EXPECT_EQ(p.dirX1, 0);
    EXPECT_EQ(p.dirX2, 0);
    EXPECT_EQ(p.ratioNumX1, 0);
    EXPECT_EQ(p.ratioDenX1, 0);
    EXPECT_EQ(p.ratioNumX2, 0);
    EXPECT_EQ(p.ratioDenX2, 0);
    EXPECT_FLOAT_EQ(p.positionOffsetX1, 0.f);
    EXPECT_FLOAT_EQ(p.positionOffsetX2, 0.f);
    EXPECT_FLOAT_EQ(p.coupleSkewLimit, 0.f);
    EXPECT_FLOAT_EQ(p.runningSkewLimit, 0.f);
    EXPECT_EQ(p.skewDelayMs, 0);
    EXPECT_EQ(p.coupleTimeoutMs, 0);
    EXPECT_EQ(p.decoupleTimeoutMs, 0);
    EXPECT_FALSE(p.trusted);
}

TEST(GantryParamModelTest, CanBePopulated) {
    GantryParamModel p;
    p.valid = true;
    p.readyToCouple = true;
    p.dirX1 = 1;
    p.dirX2 = -1;
    p.ratioNumX1 = 1;
    p.ratioDenX1 = 1;
    p.ratioNumX2 = 1;
    p.ratioDenX2 = 1;
    p.positionOffsetX1 = 2.f;
    p.positionOffsetX2 = -2.f;
    p.coupleSkewLimit = 0.5f;
    p.runningSkewLimit = 1.0f;
    p.skewDelayMs = 100;
    p.coupleTimeoutMs = 5000;
    p.decoupleTimeoutMs = 5000;
    p.trusted = true;

    EXPECT_TRUE(p.valid);
    EXPECT_TRUE(p.readyToCouple);
    EXPECT_EQ(p.dirX1, 1);
    EXPECT_EQ(p.dirX2, -1);
    EXPECT_EQ(p.ratioNumX1, 1);
    EXPECT_EQ(p.ratioDenX1, 1);
    EXPECT_FLOAT_EQ(p.positionOffsetX1, 2.f);
    EXPECT_FLOAT_EQ(p.coupleSkewLimit, 0.5f);
    EXPECT_EQ(p.coupleTimeoutMs, 5000);
    EXPECT_TRUE(p.trusted);
}

}  // namespace
}  // namespace domain_vnext::model
