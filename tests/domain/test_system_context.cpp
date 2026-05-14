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
// 新增：GantryMotorController 集成测试
// ============================================================

// 构造后 gantryMotor() 返回非空引用
TEST_F(SystemContextTest, GantryMotor_ShouldReturnNonNullOnConstruction) {
    GantryMotorController& motor = context.gantryMotor();
    // 引用本身无法判空，但调用方法不崩溃即说明对象存在
    EXPECT_NO_THROW(motor.status());
}

// gantryMotor() 与 gantry() 返回不同对象
TEST_F(SystemContextTest, GantryMotor_ShouldBeDistinctFromGantry) {
    void* gantryAddr = &context.gantry();
    void* motorAddr  = &context.gantryMotor();

    EXPECT_NE(gantryAddr, motorAddr);
}

// 默认构造后 GantryMotorController 处于 NotSynchronized
TEST_F(SystemContextTest, GantryMotor_ShouldBeNotSynchronizedByDefault) {
    GantryMotorController& motor = context.gantryMotor();

    EXPECT_TRUE(motor.isNotSynchronized());
    EXPECT_FALSE(motor.isSynchronized());
    EXPECT_FALSE(motor.isEnabled());
}

// gantryMotor() 在任何龙门联动状态下均可访问（不经过 tryGetAxis 锁定逻辑）
TEST_F(SystemContextTest, GantryMotor_AccessibleInAllCouplingStates) {
    // NotSynchronized 态下可访问
    {
        GantryMotorController& motor = context.gantryMotor();
        EXPECT_TRUE(motor.isNotSynchronized());
    }

    // Decoupled 态下可访问
    context.gantryMotor().applyFeedback({ .isCoupled = false, .errorCode = 0 });

    {
        GantryMotorController& motor = context.gantryMotor();
        EXPECT_TRUE(motor.isSynchronized());
    }
}

// gantryMotor 的同步状态独立于 GantryGroup（各自独立接收 applyFeedback）
TEST_F(SystemContextTest, GantryMotor_SynchronizationIndependentOfGantry) {
    // GantryGroup 未同步
    EXPECT_TRUE(context.gantry().isNotSynchronized());

    // 直接向 GantryMotorController 注入反馈 → 独立同步
    GantryMotorController& motor = context.gantryMotor();
    motor.applyFeedback({ .enable = true, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(motor.isSynchronized());
    EXPECT_TRUE(motor.isEnabled());

    // GantryGroup 仍然未同步（未收到反馈）
    EXPECT_TRUE(context.gantry().isNotSynchronized());
    // 但 gantryMotor 已同步，证明彼此独立
}

// setDriver / driver 接口不变
TEST_F(SystemContextTest, GantryMotor_DriverInjectionWorks) {
    context.setDriver(nullptr);
    EXPECT_EQ(context.driver(), nullptr);

    // driver() 可被 ISystemDriver* 赋值验证（编译期类型检查）
    ISystemDriver* fake = reinterpret_cast<ISystemDriver*>(0x1);
    context.setDriver(fake);
    EXPECT_EQ(context.driver(), fake);

    context.setDriver(nullptr);
}

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
