#include <gtest/gtest.h>
#include "entity/Axis.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/AxisRepository.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/MoveRelativeUseCase.h"
#include "application/axis/JogAxisUseCase.h"
#include "application/axis/StopAxisUseCase.h"
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "application/policy/AutoRelMoveOrchestrator.h"
#include "application/policy/JogOrchestrator.h"
#include <cmath>

// ============================================================================
// 端到端系统集成测试（AxisId 多轴版本）
// 核心变化：所有 UseCase 接受 AxisRepository + IAxisDriver，execute() 接受 AxisId
// 所有 Orchestrator::start() 第一个参数为 AxisId
// 同步反馈：repo.getAxis(id).applyFeedback(plc.getFeedback(id))
// ============================================================================

class SystemIntegrationTest : public ::testing::Test {
protected:
    AxisRepository repo;
    FakePLC plc;
    FakeAxisDriver driver{plc};

    void SetUp() override {
        repo.registerAxis(AxisId::Y);
        repo.registerAxis(AxisId::Z);
        repo.registerAxis(AxisId::R);
    }

    // 辅助：将 PLC 反馈同步回 Axis 实例
    void sync(AxisId id) {
        repo.getAxis(id).applyFeedback(plc.getFeedback(id));
    }

    // 辅助：等待若干个 tick，每 tick 后执行 update 和 sync
    void runTicks(AxisId id, int tickCount, int tickMs = 10) {
        for (int i = 0; i < tickCount; ++i) {
            plc.tick(tickMs);
            sync(id);
        }
    }
};

// ============================================================================
// 案例 1：端到端绝对定位（通过 Orchestrator）
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldCompleteAbsoluteMoveEndToEnd) {
    EnableUseCase enableUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    sync(AxisId::Y);

    double targetPos = 100.0;
    orchestrator.start(AxisId::Y, targetPos);

    const int TICK_MS = 10;
    const int MAX_TICKS = 500;
    int ticks = 0;
    bool motionObserved = false;
    double lastPos = repo.getAxis(AxisId::Y).currentAbsolutePosition();

    while (orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Done &&
           orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Error &&
           ticks < MAX_TICKS) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (axis.state() == AxisState::MovingAbsolute) {
            if (std::abs(axis.currentAbsolutePosition() - lastPos) > 0.0001) {
                motionObserved = true;
            }
            lastPos = axis.currentAbsolutePosition();
        }
        ticks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_LT(ticks, MAX_TICKS);
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done);
    EXPECT_TRUE(motionObserved);
    EXPECT_NEAR(axis.currentAbsolutePosition(), targetPos, 0.01);
    EXPECT_EQ(axis.state(), AxisState::Disabled);
    EXPECT_FALSE(axis.hasPendingCommand());
}

// ============================================================================
// 案例 2：Jog 持续型行为与中断
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldJogStartAndStopCorrectly) {
    EnableUseCase enableUc(repo, driver);
    JogAxisUseCase jogUc(repo, driver);
    JogOrchestrator orchestrator(enableUc, jogUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setSimulatedJogVelocity(AxisId::Y, 10.0);
    sync(AxisId::Y);

    const int TICK_MS = 10;
    const int MAX_TICKS = 500;

    // 阶段 A：正向点动
    orchestrator.startJog(AxisId::Y, Direction::Forward);

    int ticks = 0;
    bool startedJogging = false;
    double posBeforeJog = repo.getAxis(AxisId::Y).currentAbsolutePosition();

    while (ticks < MAX_TICKS) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (axis.state() == AxisState::Jogging) {
            startedJogging = true;
            for (int i = 0; i < 50; ++i) {
                orchestrator.update(axis);
                plc.tick(TICK_MS);
                sync(AxisId::Y);
            }
            break;
        }
        ticks++;
    }

    EXPECT_TRUE(startedJogging);
    double posDuringJog = repo.getAxis(AxisId::Y).currentAbsolutePosition();
    EXPECT_GT(posDuringJog, posBeforeJog);

    // 阶段 B：停止点动
    orchestrator.stopJog(AxisId::Y, Direction::Forward);

    ticks = 0;
    bool fullyStopped = false;

    while (ticks < MAX_TICKS) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (orchestrator.currentStep() == JogOrchestrator::Step::Done) {
            fullyStopped = true;
            break;
        }
        ticks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_TRUE(fullyStopped);
    EXPECT_EQ(axis.state(), AxisState::Disabled);
    EXPECT_FALSE(axis.hasPendingCommand());

    double posAfterStop = axis.currentAbsolutePosition();
    runTicks(AxisId::Y, 20, TICK_MS);
    EXPECT_DOUBLE_EQ(axis.currentAbsolutePosition(), posAfterStop);
}

// ============================================================================
// 案例 3：事前拦截（超限拒绝）
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldFailToStartWhenTargetExceedsLimit) {
    EnableUseCase enableUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setLimits(AxisId::Y, 80.0, -80.0);
    sync(AxisId::Y);

    orchestrator.start(AxisId::Y, 150.0);

    const int TICK_MS = 10;
    int ticks = 0;
    while (orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Done &&
           orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Error &&
           ticks < 100) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);
        ticks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Error);
    EXPECT_NE(axis.state(), AxisState::MovingAbsolute);
    EXPECT_EQ(axis.state(), AxisState::Disabled);
}

// ============================================================================
// 案例 4：半路夭折（运行中硬错误注入）
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldFailWhenMoveInterruptedMidway) {
    EnableUseCase enableUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setLimits(AxisId::Y, 200.0, -200.0);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    sync(AxisId::Y);

    orchestrator.start(AxisId::Y, 150.0);

    const int TICK_MS = 10;
    int ticks = 0;
    bool startedMoving = false;
    bool errorHandled = false;

    while (ticks < 500) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (axis.state() == AxisState::MovingAbsolute && !startedMoving) {
            startedMoving = true;
            plc.forceState(AxisId::Y, AxisState::Error);
        }

        if (orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Error) {
            errorHandled = true;
            break;
        }
        ticks++;
    }

    EXPECT_TRUE(startedMoving);
    EXPECT_TRUE(errorHandled);
    EXPECT_NE(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done);
}

// ============================================================================
// 案例 5：连续相对定位
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldCompleteRelativeMoveEndToEnd) {
    EnableUseCase enableUc(repo, driver);
    MoveRelativeUseCase moveRelUc(repo, driver);
    AutoRelMoveOrchestrator orchestrator(enableUc, moveRelUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    sync(AxisId::Y);

    const int TICK_MS = 10;
    const int MAX_TICKS = 500;

    // 阶段 A：第一次相对定位 (+50.0)
    orchestrator.start(AxisId::Y, 50.0);

    int ticks = 0;
    while (orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Done &&
           orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Error &&
           ticks < MAX_TICKS) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);
        ticks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.01);
    EXPECT_EQ(axis.state(), AxisState::Disabled);

    // 阶段 B：第二次相对定位 (-20.0)
    orchestrator.start(AxisId::Y, -20.0);

    ticks = 0;
    while (orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Done &&
           orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Error &&
           ticks < MAX_TICKS) {
        Axis& axisY = repo.getAxis(AxisId::Y);
        orchestrator.update(axisY);
        plc.tick(TICK_MS);
        sync(AxisId::Y);
        ticks++;
    }

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done);
    EXPECT_NEAR(repo.getAxis(AxisId::Y).currentAbsolutePosition(), 30.0, 0.01);
    EXPECT_EQ(repo.getAxis(AxisId::Y).state(), AxisState::Disabled);
}

// ============================================================================
// 案例 6：限位处中断 + 反向逃逸 + 操作死锁
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldInterruptJogAtLimitAndRestrictFurtherMoves) {
    EnableUseCase enableUc(repo, driver);
    JogAxisUseCase jogUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);
    JogOrchestrator jogOrch(enableUc, jogUc);
    AutoAbsMoveOrchestrator moveOrch(enableUc, moveUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setSimulatedJogVelocity(AxisId::Y, 20.0);
    plc.setLimits(AxisId::Y, 50.0, -1000.0);
    sync(AxisId::Y);

    const int TICK_MS = 10;

    // 阶段 1：作死正向点动，直到撞上 50 限位
    jogOrch.startJog(AxisId::Y, Direction::Forward);

    int ticks = 0;
    while (ticks < 500) {
        Axis& axis = repo.getAxis(AxisId::Y);
        jogOrch.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (jogOrch.currentStep() == JogOrchestrator::Step::Done ||
            jogOrch.currentStep() == JogOrchestrator::Step::Error) {
            break;
        }
        ticks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_TRUE(plc.getFeedback(AxisId::Y).posLimit);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.01);
    EXPECT_EQ(axis.state(), AxisState::Disabled);

    // 阶段 2：死锁 A — 企图继续正向点动
    jogOrch.startJog(AxisId::Y, Direction::Forward);
    ticks = 0;
    while (ticks < 100) {
        Axis& axisY = repo.getAxis(AxisId::Y);
        jogOrch.update(axisY);
        plc.tick(TICK_MS);
        sync(AxisId::Y);
        if (jogOrch.currentStep() == JogOrchestrator::Step::Error) break;
        ticks++;
    }

    EXPECT_EQ(jogOrch.currentStep(), JogOrchestrator::Step::Error);
    EXPECT_EQ(jogOrch.errorReason(), RejectionReason::AtPositiveLimit);

    // 阶段 3：死锁 B — 企图使用 Move 指令
    moveOrch.start(AxisId::Y, 0.0);
    ticks = 0;
    while (ticks < 100) {
        Axis& axisY = repo.getAxis(AxisId::Y);
        moveOrch.update(axisY);
        plc.tick(TICK_MS);
        sync(AxisId::Y);
        if (moveOrch.currentStep() == AutoAbsMoveOrchestrator::Step::Error) break;
        ticks++;
    }

    EXPECT_EQ(moveOrch.currentStep(), AutoAbsMoveOrchestrator::Step::Error);
    EXPECT_EQ(moveOrch.errorReason(), RejectionReason::AtPositiveLimit);

    // 阶段 4：唯一生路 — 反向逃逸
    jogOrch.startJog(AxisId::Y, Direction::Backward);
    ticks = 0;
    bool escapedLimit = false;

    while (ticks < 300) {
        Axis& axisY = repo.getAxis(AxisId::Y);
        jogOrch.update(axisY);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (axisY.state() == AxisState::Jogging && !plc.getFeedback(AxisId::Y).posLimit) {
            escapedLimit = true;
            break;
        }
        ticks++;
    }

    Axis& axisY_final = repo.getAxis(AxisId::Y);
    EXPECT_TRUE(escapedLimit);
    EXPECT_LT(axisY_final.currentAbsolutePosition(), 49.9);
    jogOrch.stopJog(AxisId::Y, Direction::Backward);
}

// ============================================================================
// 案例 7：急停 — 紧急停止注入
// ============================================================================
TEST_F(SystemIntegrationTest, ShouldHaltCompletelyAndReportErrorWhenEmergencyStopInvoked) {
    EnableUseCase enableUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);
    StopAxisUseCase stopUc(repo, driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    plc.forceState(AxisId::Y, AxisState::Disabled);
    plc.setSimulatedMoveVelocity(AxisId::Y, 20.0);
    sync(AxisId::Y);

    orchestrator.start(AxisId::Y, 200.0);

    const int TICK_MS = 10;
    int ticks = 0;
    bool startedMoving = false;

    while (ticks < 200) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (axis.state() == AxisState::MovingAbsolute) {
            startedMoving = true;
        }
        ticks++;
    }

    EXPECT_TRUE(startedMoving);

    // 急停注入
    stopUc.execute(AxisId::Y);

    int stopTicks = 0;
    while (stopTicks < 500) {
        Axis& axis = repo.getAxis(AxisId::Y);
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        sync(AxisId::Y);

        if (orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Error ||
            orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Done) {
            break;
        }
        stopTicks++;
    }

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_LT(axis.currentAbsolutePosition(), 150.0);
    EXPECT_NE(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done);
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Error);
}

// ============================================================================
// 案例 8：普通生命周期测试（简化版）
// ============================================================================
TEST_F(SystemIntegrationTest, FullMoveAbsoluteLifeCycle) {
    EnableUseCase enableUc(repo, driver);
    MoveAbsoluteUseCase moveUc(repo, driver);

    plc.forceState(AxisId::Y, AxisState::Idle);
    plc.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    sync(AxisId::Y);

    Axis& axis = repo.getAxis(AxisId::Y);
    EXPECT_EQ(axis.state(), AxisState::Idle);

    RejectionReason reason = moveUc.execute(AxisId::Y, 100.0);
    EXPECT_EQ(reason, RejectionReason::None);

    runTicks(AxisId::Y, 100, 10); // 1000ms，走 50.0

    sync(AxisId::Y);
    EXPECT_EQ(axis.state(), AxisState::MovingAbsolute);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.001);

    runTicks(AxisId::Y, 100, 10); // 再 1000ms，到 100.0
    sync(AxisId::Y);

    EXPECT_EQ(axis.state(), AxisState::Idle);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 100.0, 0.001);
    EXPECT_FALSE(axis.hasPendingCommand());
}
