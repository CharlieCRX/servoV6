// tests/TestSystemContext.cpp
#include <gtest/gtest.h>
#include "entity/SystemContext.h"

class SystemContextTest : public ::testing::Test {
protected:
    SystemContext context;
    Axis* outAxis = nullptr;
    ContextRejection reason = ContextRejection::None;
};

// 1. 测试联动模式下访问逻辑轴 X
TEST_F(SystemContextTest, TryGet_X_ShouldSucceed_InCoupledMode) {
    context.setCoupledState(true);
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

// 2. 测试联动模式下访问物理轴 X1 (核心约束)
TEST_F(SystemContextTest, TryGet_X1_ShouldFail_InCoupledMode) {
    context.setCoupledState(true);
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::PhysicalAxisLockedByGantry); // 理由：状态非法（联动中不可动物理轴）
}

// 3. 测试解耦模式下访问逻辑轴 X (核心约束)
TEST_F(SystemContextTest, TryGet_X_ShouldFail_InDecoupledMode) {
    context.setCoupledState(false);
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(reason, ContextRejection::LogicalAxisUnavailableWhenDecoupled); 
}