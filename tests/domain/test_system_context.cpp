// tests/domain/test_system_context.cpp
#include <gtest/gtest.h>
#include "entity/SystemContext.h"
#include "safety/EmergencyStopController.h"

class SystemContextTest : public ::testing::Test {
protected:
    SystemContext context;
    Axis* outAxis = nullptr;
    ContextRejection reason = ContextRejection::None;

    void SetUp() override {
        // 安全域首次同步：注入 PLC 反馈"设备急停中 = false"
        // 完成 NotSynchronized → Running，使所有现有测试不受安全域干扰
        context.emergencyStopController().applyFeedback(false);
        // 此时 isSystemLocked() == false，tryGetAxis() 的 Layer 0 拦截不生效
    }
};

// ============================================================
// 安全急停 — 拦截优先级 Layer 0 测试
// ============================================================

// 默认 SetUp 后，安全域状态为 Running，访问不受影响
TEST_F(SystemContextTest, Safety_SystemNotLockedByDefault_AfterSync) {
    EXPECT_FALSE(context.emergencyStopController().isSystemLocked());
    EXPECT_EQ(context.emergencyStopController().state(), SafetyState::Running);
}

// 急停中：所有轴（包括非龙门轴 Y/Z/R）都拒绝访问
TEST_F(SystemContextTest, Safety_EmergencyStopped_RejectsAllAxes) {
    // Step 1: 模拟 PLC 反馈急停激活
    context.emergencyStopController().applyFeedback(true);
    ASSERT_TRUE(context.emergencyStopController().isSystemLocked());

    // Y 轴（非龙门轴）被拒绝
    EXPECT_FALSE(context.tryGetAxis(AxisId::Y, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);
    EXPECT_EQ(outAxis, nullptr);

    // Z 轴（非龙门轴）被拒绝
    EXPECT_FALSE(context.tryGetAxis(AxisId::Z, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);

    // R 轴（非龙门轴）被拒绝
    EXPECT_FALSE(context.tryGetAxis(AxisId::R, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);

    // X 轴（龙门逻辑轴）被拒绝 — Layer 0 先于龙门检查
    EXPECT_FALSE(context.tryGetAxis(AxisId::X, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);

    // X1 轴（龙门物理轴）被拒绝 — Layer 0 先于龙门检查
    EXPECT_FALSE(context.tryGetAxis(AxisId::X1, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);

    // X2 轴（龙门物理轴）被拒绝 — Layer 0 先于龙门检查
    EXPECT_FALSE(context.tryGetAxis(AxisId::X2, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);
}

// 急停解除中（过渡态）：所有轴也拒绝访问
TEST_F(SystemContextTest, Safety_ReleasingEmergencyStop_RejectsAllAxes) {
    // Step 1: 先进入 EmergencyStopped
    context.emergencyStopController().applyFeedback(true);
    ASSERT_TRUE(context.emergencyStopController().isEmergencyStopped());

    // Step 2: 请求解除 → 进入 ReleasingEmergencyStop
    auto result = context.emergencyStopController().requestReleaseEmergencyStop();
    ASSERT_EQ(result, SafetyRejection::None);
    ASSERT_EQ(context.emergencyStopController().state(), SafetyState::ReleasingEmergencyStop);
    ASSERT_TRUE(context.emergencyStopController().isSystemLocked());

    // 过渡态也拒绝所有轴访问
    EXPECT_FALSE(context.tryGetAxis(AxisId::Y, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked);
}

// 急停解除后（PLC 反馈 false），访问恢复
TEST_F(SystemContextTest, Safety_AfterRelease_AccessRestored) {
    // Step 1: 急停激活
    context.emergencyStopController().applyFeedback(true);
    ASSERT_FALSE(context.tryGetAxis(AxisId::Y, outAxis, reason));

    // Step 2: 请求解除
    context.emergencyStopController().requestReleaseEmergencyStop();
    // Step 3: PLC 反馈解除
    context.emergencyStopController().applyFeedback(false);

    // 现在应该恢复 Running，访问正常
    ASSERT_EQ(context.emergencyStopController().state(), SafetyState::Running);
    ASSERT_FALSE(context.emergencyStopController().isSystemLocked());

    EXPECT_TRUE(context.tryGetAxis(AxisId::Y, outAxis, reason));
    EXPECT_EQ(reason, ContextRejection::None);
}

// Layer 0 安全拦截优先于龙门 NotSynchronized 拦截
TEST_F(SystemContextTest, Safety_Layer0_BeforeGantryNotSynchronized) {
    // 即使龙门状态未同步，安全域锁定后 Layer 0 率先拦截
    // 默认 GantryCouplingController 处于 NotSynchronized（SetUp 未对其做同步）
    // 但 SetUp 中已对安全域做了同步 → Running

    // 验证：安全域 Running + 龙门 NotSynchronized → 返回 GantryNotSynchronized
    {
        EXPECT_FALSE(context.tryGetAxis(AxisId::X, outAxis, reason));
        EXPECT_EQ(reason, ContextRejection::GantryNotSynchronized);
    }

    // 现在激活急停 → Layer 0 优先拦截，不再返回 GantryNotSynchronized
    context.emergencyStopController().applyFeedback(true);
    {
        EXPECT_FALSE(context.tryGetAxis(AxisId::X, outAxis, reason));
        EXPECT_EQ(reason, ContextRejection::SystemSafetyLocked); // <-- Layer 0 优先
    }
}

// 即使安全域未同步，非龙门轴访问也被 Layer 0 拦截（不受 GantryNotSynchronized 影响）
TEST_F(SystemContextTest, Safety_NotSynchronized_RejectsAllAxes)
{
    SystemContext context;

    Axis* outAxis = nullptr;
    ContextRejection reason;

    EXPECT_FALSE(
        context.tryGetAxis(AxisId::Y, outAxis, reason));

    EXPECT_EQ(reason,
              ContextRejection::SystemSafetyLocked);
}

// 即使龙门处于 Coupled 状态，安全域锁定后访问仍被 Layer 0 拦截
TEST_F(SystemContextTest, SafetyOverridesCoupledAccess)
{
    context.gantryCouplingController()
        .applyFeedback({ .isCoupled = true });

    ASSERT_TRUE(
        context.tryGetAxis(AxisId::X,
                           outAxis,
                           reason));

    context.emergencyStopController().applyFeedback(true);

    EXPECT_FALSE(context.tryGetAxis(AxisId::X,
                           outAxis,
                           reason));

    EXPECT_EQ(reason,
              ContextRejection::SystemSafetyLocked);
}

// ============================================================
// GantryPowerController 集成测试
// ============================================================

// 构造后 gantryPowerController() 返回非空引用
TEST_F(SystemContextTest, GantryMotor_ShouldReturnNonNullOnConstruction) {
    GantryPowerController& motor = context.gantryPowerController();
    EXPECT_NO_THROW(motor.status());
}

// gantryPowerController() 与 gantryCouplingController() 返回不同对象
TEST_F(SystemContextTest, GantryMotor_ShouldBeDistinctFromGantry) {
    void* gantryAddr = &context.gantryCouplingController();
    void* motorAddr  = &context.gantryPowerController();
    EXPECT_NE(gantryAddr, motorAddr);
}

// 默认构造后 GantryPowerController 处于 NotSynchronized
TEST_F(SystemContextTest, GantryMotor_ShouldBeNotSynchronizedByDefault) {
    GantryPowerController& motor = context.gantryPowerController();
    EXPECT_TRUE(motor.isNotSynchronized());
    EXPECT_FALSE(motor.isSynchronized());
    EXPECT_FALSE(motor.isEnabled());
}

// gantryPowerController() 在任何龙门联动状态下均可访问（不经过 tryGetAxis 锁定逻辑）
TEST_F(SystemContextTest, GantryMotor_AccessibleInAllCouplingStates) {
    // NotSynchronized 态下可访问
    {
        GantryPowerController& motor = context.gantryPowerController();
        EXPECT_TRUE(motor.isNotSynchronized());
    }

    // Decoupled 态下可访问
    context.gantryPowerController().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    {
        GantryPowerController& motor = context.gantryPowerController();
        EXPECT_TRUE(motor.isSynchronized());
    }
}

// gantryPowerController 的同步状态独立于 GantryGroup（各自独立接收 applyFeedback）
TEST_F(SystemContextTest, GantryMotor_SynchronizationIndependentOfGantry) {
    // GantryGroup 未同步
    EXPECT_TRUE(context.gantryCouplingController().isNotSynchronized());

    // 直接向 GantryPowerController 注入反馈 → 独立同步
    GantryPowerController& motor = context.gantryPowerController();
    motor.applyFeedback({ .enable = true, .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(motor.isSynchronized());
    EXPECT_TRUE(motor.isEnabled());

    // GantryGroup 仍然未同步（未收到反馈）
    EXPECT_TRUE(context.gantryCouplingController().isNotSynchronized());
}

// setDriver / driver 接口不变
TEST_F(SystemContextTest, GantryMotor_DriverInjectionWorks) {
    context.setDriver(nullptr);
    EXPECT_EQ(context.driver(), nullptr);

    ISystemDriver* fake = reinterpret_cast<ISystemDriver*>(0x1);
    context.setDriver(fake);
    EXPECT_EQ(context.driver(), fake);

    context.setDriver(nullptr);
}

// ============================================================
// 龙门 NotSynchronized 态：X / X1 / X2 全部拒绝
// （安全域已同步 → Running，龙门 NotSynchronized 拦截生效）
// ============================================================

TEST_F(SystemContextTest, TryGet_X_ShouldFail_WhenNotSynchronized) {
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
    context.gantryCouplingController().applyFeedback({ .isCoupled = true, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_X1_ShouldFail_InCoupledMode) {
    context.gantryCouplingController().applyFeedback({ .isCoupled = true, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::PhysicalAxisLockedByGantry);
}

TEST_F(SystemContextTest, TryGet_X2_ShouldFail_InCoupledMode) {
    context.gantryCouplingController().applyFeedback({ .isCoupled = true, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X2, outAxis, reason);
    EXPECT_FALSE(success);
    EXPECT_EQ(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::PhysicalAxisLockedByGantry);
}

// ============================================================
// 解耦模式 (Decoupled)：X 锁定，X1/X2 可访问
// ============================================================

TEST_F(SystemContextTest, TryGet_X_ShouldFail_InDecoupledMode) {
    context.gantryCouplingController().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X, outAxis, reason);
    EXPECT_FALSE(success);
    EXPECT_EQ(reason, ContextRejection::LogicalAxisUnavailableWhenDecoupled);
}

TEST_F(SystemContextTest, TryGet_X1_ShouldSucceed_InDecoupledMode) {
    context.gantryCouplingController().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X1, outAxis, reason);
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

TEST_F(SystemContextTest, TryGet_X2_ShouldSucceed_InDecoupledMode) {
    context.gantryCouplingController().applyFeedback({ .isCoupled = false, .errorCode = 0 });
    bool success = context.tryGetAxis(AxisId::X2, outAxis, reason);
    EXPECT_TRUE(success);
    EXPECT_NE(outAxis, nullptr);
    EXPECT_EQ(reason, ContextRejection::None);
}

// ============================================================
// emergencyStopController() 集成测试
// ============================================================

// 构造后 emergencyStopController() 返回非空引用（值语义，始终存在）
TEST_F(SystemContextTest, Safety_EmergencyStopControllerAccessible) {
    EmergencyStopController& esc = context.emergencyStopController();
    EXPECT_NO_THROW(esc.state());
}

// SetUp 同步后，安全域处于 Running
TEST_F(SystemContextTest, Safety_AfterSetUpSync_StateIsRunning) {
    EXPECT_EQ(context.emergencyStopController().state(), SafetyState::Running);
    EXPECT_FALSE(context.emergencyStopController().isSystemLocked());
    EXPECT_FALSE(context.emergencyStopController().isEmergencyStopped());
    EXPECT_FALSE(context.emergencyStopController().isNotSynchronized());
    EXPECT_FALSE(context.emergencyStopController().isTransitioning());
}
