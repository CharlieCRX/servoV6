#include <gtest/gtest.h>
#include "domain/entity/Axis.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/MoveRelativeUseCase.h"
#include "application/axis/JogAxisUseCase.h"
#include "application/axis/StopAxisUseCase.h"
#include <cmath>

// ============================================================================
// 端到端系统集成测试（分组感知版本 v2 — 适配 SystemManager/SystemContext 重构）
//
// ╔══════════════════════════════════════════════════════════════╗
// ║  架构变更：SystemManager 内部持有 SystemContext               ║
// ║  SystemContext 默认构造即包含 6 个轴 + GantryGroup             ║
// ║                                                              ║
// ║  SystemManager                                               ║
// ║  ├── "Machine_A" → SystemContext_A (driver → plcA)          ║
// ║  │   └── 使用 Y, Z, R 轴                                    ║
// ║  └── "Machine_B" → SystemContext_B (driver → plcB)          ║
// ║      └── 使用 X1, X2 轴                                     ║
// ╚══════════════════════════════════════════════════════════════╝
// ============================================================================

class SystemIntegrationTest : public ::testing::Test {
protected:
    // ---- Machine_A 的硬件虚拟化 ----
    FakePLC plcA;
    FakeAxisDriver driverA{plcA};

    // ---- Machine_B 的硬件虚拟化 ----
    FakePLC plcB;
    FakeAxisDriver driverB{plcB};

    // ---- 全局 SystemManager（内部持有 SystemContext） ----
    SystemManager manager;

    // ---- 从 manager 获取到的分组指针 ----
    SystemContext* ctxA = nullptr;
    SystemContext* ctxB = nullptr;

    // ---- 无状态 UseCases ----
    EnableUseCase enableUc;
    MoveAbsoluteUseCase moveAbsUc;
    MoveRelativeUseCase moveRelUc;
    JogAxisUseCase jogUc;
    StopAxisUseCase stopUc;

    static constexpr const char* GROUP_A = "Machine_A";
    static constexpr const char* GROUP_B = "Machine_B";

    void SetUp() override {
        // 创建两个独立分组
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP_A, reason));
        ASSERT_TRUE(manager.createGroup(GROUP_B, reason));

        ASSERT_TRUE(manager.tryGetGroup(GROUP_A, ctxA, reason));
        ASSERT_TRUE(manager.tryGetGroup(GROUP_B, ctxB, reason));

        // 绑定驱动（模拟硬件抽象层）
        ctxA->setDriver(&driverA);
        ctxB->setDriver(&driverB);

        // 默认限位配置
        plcA.setLimits(AxisId::Y, 1000.0, -1000.0);
        plcA.setLimits(AxisId::Z, 1000.0, -1000.0);
        plcA.setLimits(AxisId::R, 1000.0, -1000.0);
        plcB.setLimits(AxisId::X1, 1000.0, -1000.0);
        plcB.setLimits(AxisId::X2, 1000.0, -1000.0);
    }

    // 辅助：将 PLC 反馈同步回 Axis 实例（使用 tryGetAxis）
    void syncA(AxisId id) {
        Axis* a = nullptr;
        ContextRejection r;
        if (ctxA->tryGetAxis(id, a, r) && a) {
            a->applyFeedback(plcA.getFeedback(id));
        }
    }

    void syncB(AxisId id) {
        Axis* a = nullptr;
        ContextRejection r;
        if (ctxB->tryGetAxis(id, a, r) && a) {
            a->applyFeedback(plcB.getFeedback(id));
        }
    }

    // 辅助：推进 A 组物理引擎并同步
    void tickA(AxisId id, int tickCount, int tickMs = 10) {
        for (int i = 0; i < tickCount; ++i) {
            plcA.tick(tickMs);
            syncA(id);
        }
    }

    // 辅助：推进 B 组物理引擎并同步
    void tickB(AxisId id, int tickCount, int tickMs = 10) {
        for (int i = 0; i < tickCount; ++i) {
            plcB.tick(tickMs);
            syncB(id);
        }
    }

    // 辅助：根据 id 输出轴指针（tryGetAxis 包装）
    Axis* getAxisA(AxisId id) {
        Axis* a = nullptr;
        ContextRejection r;
        ctxA->tryGetAxis(id, a, r);
        return a;
    }

    Axis* getAxisB(AxisId id) {
        Axis* a = nullptr;
        ContextRejection r;
        ctxB->tryGetAxis(id, a, r);
        return a;
    }
};

// ============================================================================
// 案例 1：端到端绝对定位（Enable → MoveAbs → WaitDone）
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldCompleteAbsoluteMoveEndToEnd) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    syncA(AxisId::Y);

    double targetPos = 100.0;

    // Enable → Idle
    auto enableResult = enableUc.execute(manager, GROUP_A, AxisId::Y, true);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(enableResult));
    tickA(AxisId::Y, 20, 10); // 200ms > 150ms enable delay

    // MoveAbsolute → MovingAbsolute
    auto moveResult = moveAbsUc.execute(manager, GROUP_A, AxisId::Y, targetPos);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(moveResult));

    // 等待运动完成
    int ticks = 0;
    const int MAX_TICKS = 500;
    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);

    while (ticks < MAX_TICKS) {
        plcA.tick(10);
        syncA(AxisId::Y);
        if (axis->state() == AxisState::Idle) break;
        ticks++;
    }

    EXPECT_LT(ticks, MAX_TICKS);
    EXPECT_EQ(axis->state(), AxisState::Idle);
    EXPECT_NEAR(axis->currentAbsolutePosition(), targetPos, 0.01);
    EXPECT_FALSE(axis->hasPendingCommand());
}

// ============================================================================
// 案例 2：端到端相对定位（两次连续）
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldCompleteRelativeMoveEndToEnd) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    syncA(AxisId::Y);

    // Enable
    auto enableResult = enableUc.execute(manager, GROUP_A, AxisId::Y, true);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(enableResult));
    tickA(AxisId::Y, 20, 10);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);

    // 第一次相对定位 +50
    auto move1Result = moveRelUc.execute(manager, GROUP_A, AxisId::Y, 50.0);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(move1Result));

    int ticks = 0;
    while (ticks < 500) {
        plcA.tick(10);
        syncA(AxisId::Y);
        if (axis->state() == AxisState::Idle) break;
        ticks++;
    }

    EXPECT_NEAR(axis->currentAbsolutePosition(), 50.0, 0.01);

    // 第二次相对定位 -20 → 预期 30
    auto move2Result = moveRelUc.execute(manager, GROUP_A, AxisId::Y, -20.0);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(move2Result));

    ticks = 0;
    while (ticks < 500) {
        plcA.tick(10);
        syncA(AxisId::Y);
        if (axis->state() == AxisState::Idle) break;
        ticks++;
    }

    EXPECT_NEAR(axis->currentAbsolutePosition(), 30.0, 0.01);
}

// ============================================================================
// 案例 3：事前拦截 — 目标超出软限位应被 MoveUseCase 拒绝
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectMoveWhenTargetExceedsLimit) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setLimits(AxisId::Y, 80.0, -80.0);
    syncA(AxisId::Y);

    // Enable
    auto enableResult = enableUc.execute(manager, GROUP_A, AxisId::Y, true);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(enableResult));
    tickA(AxisId::Y, 20, 10);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);
    ASSERT_EQ(axis->state(), AxisState::Idle);

    // 试图运动到 150（超出正向限位 80）
    auto moveResult = moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 150.0);
    EXPECT_FALSE(std::holds_alternative<std::monostate>(moveResult))
        << "Move beyond positive limit should have been rejected!";

    // 应返回 TargetOutOfPositiveLimit
    if (std::holds_alternative<RejectionReason>(moveResult)) {
        EXPECT_EQ(std::get<RejectionReason>(moveResult), RejectionReason::TargetOutOfPositiveLimit);
    }

    // 轴状态应保持 Idle（未进入运动）
    EXPECT_EQ(axis->state(), AxisState::Idle);
    EXPECT_FALSE(axis->hasPendingCommand());
}

// ============================================================================
// 案例 4：半路夭折 — 运行中硬错误注入
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldDetectMidMoveError) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 20.0);
    plcA.setLimits(AxisId::Y, 200.0, -200.0);
    syncA(AxisId::Y);

    // Enable
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);

    // 开始运动
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 150.0)));

    // 推进一小段确认运动开始
    plcA.tick(200);
    syncA(AxisId::Y);
    ASSERT_EQ(axis->state(), AxisState::MovingAbsolute);

    // 注入硬件错误
    plcA.forceState(AxisId::Y, AxisState::Error);
    syncA(AxisId::Y);

    EXPECT_EQ(axis->state(), AxisState::Error);
}

// ============================================================================
// 案例 5：限位处 Jog 操作死锁 — 到达正限位后禁止正向点动和定位
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectOperationsAtPositiveLimit) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    plcA.setLimits(AxisId::Y, 50.0, -1000.0);
    syncA(AxisId::Y);

    // Enable
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);

    // 运动到正限位 50
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 50.0)));

    int ticks = 0;
    while (ticks < 500) {
        plcA.tick(10);
        syncA(AxisId::Y);
        if (axis->state() == AxisState::Idle) break;
        ticks++;
    }

    ASSERT_NEAR(axis->currentAbsolutePosition(), 50.0, 0.01);
    ASSERT_TRUE(plcA.getFeedback(AxisId::Y).posLimit);

    // 死锁 A：正向点动被拒绝
    auto jogFwdResult = jogUc.execute(manager, GROUP_A, AxisId::Y, Direction::Forward);
    EXPECT_TRUE(std::holds_alternative<RejectionReason>(jogFwdResult));
    EXPECT_EQ(std::get<RejectionReason>(jogFwdResult), RejectionReason::AtPositiveLimit);

    // 死锁 B：正向定位被拒绝（目标 > 当前位置）
    auto moveBeyondResult = moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 100.0);
    EXPECT_FALSE(std::holds_alternative<std::monostate>(moveBeyondResult));

    // 生路：反向点动应允许
    auto jogBwdResult = jogUc.execute(manager, GROUP_A, AxisId::Y, Direction::Backward);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(jogBwdResult));
}

// ============================================================================
// 案例 6：急停 — StopUseCase 中断运动
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldStopMidMove) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 20.0);
    syncA(AxisId::Y);

    // Enable
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    // 开始运动（目标很远）
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 500.0)));

    // 推进一小段
    plcA.tick(500);
    syncA(AxisId::Y);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);
    ASSERT_EQ(axis->state(), AxisState::MovingAbsolute);

    double posBeforeStop = axis->currentAbsolutePosition();

    // 急停
    auto stopResult = stopUc.execute(manager, GROUP_A, AxisId::Y);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(stopResult));

    // 等待停止生效
    plcA.tick(20);
    syncA(AxisId::Y);

    // 位置不应再变化（或已停止）
    double posAfterStop = axis->currentAbsolutePosition();
    plcA.tick(100);
    syncA(AxisId::Y);

    EXPECT_NEAR(axis->currentAbsolutePosition(), posAfterStop, 0.01)
        << "Position should not change after stop!";
}

// ============================================================================
// 案例 7：分组隔离 — A 组运动不影响 B 组
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldIsolateGroupAFromGroupB) {
    // A 组 Y 轴使能
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    syncA(AxisId::Y);

    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    // B 组先解耦龙门，使 X1 可独立操作（SystemContext 默认耦合）
    ctxB->setCoupledState(false);

    // B 组 X1 轴使能（独立）
    plcB.forceState(AxisId::X1, AxisState::Disabled);
    plcB.setSimulatedMoveVelocity(AxisId::X1, 50.0);
    syncB(AxisId::X1);

    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_B, AxisId::X1, true)));
    tickB(AxisId::X1, 20, 10);

    // A 组 Y 轴运动到 100
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 100.0)));

    // 推进 A 组 1000ms
    tickA(AxisId::Y, 100, 10);

    Axis* axisA_Y = getAxisA(AxisId::Y);
    Axis* axisB_X1 = getAxisB(AxisId::X1);

    ASSERT_NE(axisA_Y, nullptr);
    ASSERT_NE(axisB_X1, nullptr);

    // A 组 Y 轴在运动
    EXPECT_EQ(axisA_Y->state(), AxisState::MovingAbsolute);
    EXPECT_NEAR(axisA_Y->currentAbsolutePosition(), 50.0, 0.01);

    // B 组 X1 轴不受影响
    EXPECT_EQ(axisB_X1->state(), AxisState::Idle);
    EXPECT_NEAR(axisB_X1->currentAbsolutePosition(), 0.0, 0.001);

    // A 组继续运动到完成
    tickA(AxisId::Y, 100, 10);
    syncA(AxisId::Y);

    EXPECT_EQ(axisA_Y->state(), AxisState::Idle);
    EXPECT_NEAR(axisA_Y->currentAbsolutePosition(), 100.0, 0.01);

    // B 组 X1 轴仍不受影响
    EXPECT_EQ(axisB_X1->state(), AxisState::Idle);
    EXPECT_NEAR(axisB_X1->currentAbsolutePosition(), 0.0, 0.001);
}

// ============================================================================
// 案例 8：SystemManager 层错误 — 查询不存在的分组
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectWhenGroupNotFound) {
    auto result = enableUc.execute(manager, "NonExistentGroup", AxisId::Y, true);
    EXPECT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::GroupNotFound);
}

// ============================================================================
// 案例 9：SystemContext 层错误 — 查询未注册的轴
//
// 注：SystemContext 默认构造包含全部 6 个轴，因此不再存在"未注册轴"场景。
// 改为测试龙门联动拦截：联动时 X1 被 tryGetAxis 拒绝。
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectPhysicalAxisWhenGantryCoupled) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    syncA(AxisId::Y);

    // X1 在 ctxA 的龙门联动下被拦截（isGantryCoupled 默认为 true）
    auto result = enableUc.execute(manager, GROUP_A, AxisId::X1, true);
    EXPECT_TRUE(std::holds_alternative<ContextRejection>(result));
    EXPECT_EQ(std::get<ContextRejection>(result), ContextRejection::PhysicalAxisLockedByGantry);
}

// ============================================================================
// 案例 10：Axis 领域层错误 — 故障态使能应被拒绝
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectEnableWhenInErrorState) {
    plcA.forceState(AxisId::Y, AxisState::Error);
    syncA(AxisId::Y);

    // 故障状态下使能应被拒绝
    auto r = enableUc.execute(manager, GROUP_A, AxisId::Y, true);
    EXPECT_TRUE(std::holds_alternative<RejectionReason>(r));
    EXPECT_EQ(std::get<RejectionReason>(r), RejectionReason::InvalidState);
}

// ============================================================================
// 案例 11：Jog 点动完整流程
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldJogContinuouslyAndStop) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedJogVelocity(AxisId::Y, 10.0);
    syncA(AxisId::Y);

    // Enable
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);
    ASSERT_EQ(axis->state(), AxisState::Idle);

    double initialPos = axis->currentAbsolutePosition();

    // 启动正向点动
    auto jogResult = jogUc.execute(manager, GROUP_A, AxisId::Y, Direction::Forward);
    ASSERT_TRUE(std::holds_alternative<std::monostate>(jogResult));

    // 推进 100ms（速度 10 → 位移 1.0）
    tickA(AxisId::Y, 10, 10);
    syncA(AxisId::Y);

    EXPECT_EQ(axis->state(), AxisState::Jogging);
    EXPECT_NEAR(axis->currentAbsolutePosition(), initialPos + 1.0, 0.001);

    // 停止点动
    jogUc.stop(manager, GROUP_A, AxisId::Y, Direction::Forward);
    tickA(AxisId::Y, 10, 10);

    EXPECT_EQ(axis->state(), AxisState::Idle);
}

// ============================================================================
// 案例 12：禁用状态下不可运动
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectMoveWhenDisabled) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    syncA(AxisId::Y);

    auto moveResult = moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 100.0);
    EXPECT_TRUE(std::holds_alternative<RejectionReason>(moveResult));
    EXPECT_EQ(std::get<RejectionReason>(moveResult), RejectionReason::InvalidState);
}

// ============================================================================
// 案例 13：运动中掉电应被拒绝
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldRejectDisableWhileMoving) {
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 20.0);
    syncA(AxisId::Y);

    // Enable
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        enableUc.execute(manager, GROUP_A, AxisId::Y, true)));
    tickA(AxisId::Y, 20, 10);

    // 开始慢速运动
    ASSERT_TRUE(std::holds_alternative<std::monostate>(
        moveAbsUc.execute(manager, GROUP_A, AxisId::Y, 500.0)));

    plcA.tick(100);
    syncA(AxisId::Y);

    Axis* axis = getAxisA(AxisId::Y);
    ASSERT_NE(axis, nullptr);
    ASSERT_EQ(axis->state(), AxisState::MovingAbsolute);

    // 运动中掉电应被拒绝
    auto disableResult = enableUc.execute(manager, GROUP_A, AxisId::Y, false);
    EXPECT_TRUE(std::holds_alternative<RejectionReason>(disableResult));
    EXPECT_EQ(std::get<RejectionReason>(disableResult), RejectionReason::AlreadyMoving);
}
