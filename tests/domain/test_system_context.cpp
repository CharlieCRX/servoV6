// tests/TestSystemContext.cpp
#include <gtest/gtest.h>
#include "entity/SystemContext.h"

class SystemContextTest : public ::testing::Test {
protected:
    SystemContext context;
    Axis* outAxis = nullptr;
    ContextRejection reason = ContextRejection::None;
};

// ============================================================
// NotSynchronized 态：X / X1 / X2 全部拒绝
// ============================================================

TEST_F(SystemContextTest, TryGet_X_ShouldFail_WhenNotSynchronized) {
    // 默认构造后 GantryGroup 处于 NotSynchronized
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::GantryNotSynchronized);
}

TEST_F(SystemContextTest, TryGet_X1_ShouldFail_WhenNotSynchronized) {
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::GantryNotSynchronized);
}

TEST_F(SystemContextTest, TryGet_X2_ShouldFail_WhenNotSynchronized) {
    bool success = context.tryGetAxis(AxisId::X2, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::GantryNotSynchronized);
}

// 非龙门轴 (Y/Z/R) 不受 NotSynchronized 影响，始终可访问
TEST_F(SystemContextTest, TryGet_Y_ShouldSucceed_EvenWhenNotSynchronized) {
    bool success = context.tryGetAxis(AxisId::Y, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_Z_ShouldSucceed_EvenWhenNotSynchronized) {
    bool success = context.tryGetAxis(AxisId::Z, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_R_ShouldSucceed_EvenWhenNotSynchronized) {
    bool success = context.tryGetAxis(AxisId::R, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

// ============================================================
// 联动模式 (Coupled)：X 可访问，X1/X2 锁定
// ============================================================

TEST_F(SystemContextTest, TryGet_X_ShouldSucceed_InCoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 退出 NotSynchronized → Coupled
    
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_X1_ShouldFail_InCoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = true, .errorCode = 0 });
    
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(SystemContextTest, TryGet_X2_ShouldFail_InCoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = true, .errorCode = 0 });
    
    bool success = context.tryGetAxis(AxisId::X2, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::PhysicalAxisLockedByGantry);
}

// ============================================================
// 解耦模式 (Decoupled)：X 锁定，X1/X2 可访问
// ============================================================

TEST_F(SystemContextTest, TryGet_X_ShouldFail_InDecoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = false, .errorCode = 0 }); // 退出 NotSynchronized → Decoupled
    
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    
    EXPECT_FALSE(success);
    EXPECT_EQ(reason, ContextRejection::LogicalAxisUnavailableWhenDecoupled);
}

TEST_F(SystemContextTest, TryGet_X1_ShouldSucceed_InDecoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_X2_ShouldSucceed_InDecoupledMode) {
    context.gantry().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    
    bool success = context.tryGetAxis(AxisId::X2, outAxis, reason);
    
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}
