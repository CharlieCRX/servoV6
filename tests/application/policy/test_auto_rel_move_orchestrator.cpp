/**
 * @file test_auto_rel_move_orchestrator.cpp
 * @brief AutoRelMoveOrchestrator 的单元测试套件
 *
 * 本文件测试 AutoRelMoveOrchestrator——相对运动编排器的完整状态机行为。
 * 编排器负责协调多轴场景下的 "Enable → IssueMove → WaitMotionStart → WaitMotionFinish → Done"
 * 全生命周期，覆盖正常流程、异常恢复、幂等性保障等关键路径。
 *
 * 架构变更（组管理重构）：
 *   编排器不再直接持有 AxisRepository，改为通过 SystemManager → SystemContext 获取轴。
 *   EnableUseCase 已改为无状态（execute 时传入 manager + groupName + axisId）。
 *   指令下发通过 group->driver()->send() 完成。
 *
 * 测试架构：
 *   - FakeAxisDriver：由 FakePLC 构造的指令记录驱动，实现 ISystemDriver 接口
 *   - AutoRelMoveOrchestratorTest：集成 SystemManager 的测试夹具，
 *     提供便捷的轴状态注入与状态机快速推进工具方法
 *
 * 状态机流转示意：
 *   Initial → EnsuringEnabled → IssuingMove → WaitingMotionStart → WaitingMotionFinish → Done
 *               ↓                    ↓              ↓                    ↓
 *             Error ←────────── Error ←───────── Error ←───────────── Error
 */

#include <gtest/gtest.h>
#include "application/policy/AutoRelMoveOrchestrator.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/SystemManager.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// =====================================================================
// AutoRelMoveOrchestratorTest：测试夹具
// =====================================================================
// 使用 SystemManager + FakeAxisDriver 构建完整分组环境。
// 通过 FakePLC 模拟不同的轴状态反馈，观察编排器的状态跃迁。
// 覆盖正向流程、异常分支、边界条件和防御性编程（防误杀）。
// 新增 SystemManager 层错误路径（分组不存在）。
// =====================================================================

class AutoRelMoveOrchestratorTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;

    std::unique_ptr<AutoRelMoveOrchestrator> orchestrator;

    const std::string groupName = "TestGroup";
    AxisId targetId = AxisId::Y;

    void SetUp() override {
        ContextRejection reason;
        manager.createGroup(groupName, reason);

        SystemContext* group = nullptr;
        ContextRejection mgrReason;
        manager.tryGetGroup(groupName, group, mgrReason);
        group->setDriver(&driver);
        group->emergencyStopController().applyFeedback(false); // 默认不急停

        Axis* axisPtr = nullptr;
        ContextRejection ctxReason;
        ASSERT_TRUE(group->tryGetAxis(targetId, axisPtr, ctxReason));
        Axis& axis = *axisPtr;
        axis.applyFeedback({
            .state = AxisState::Idle,
            .absPos = 0.0, .relPos = 0.0, .relZeroAbsPos = 0.0,
            .posLimit = false, .negLimit = false,
            .posLimitValue = 1000.0, .negLimitValue = -1000.0
        });

        orchestrator = std::make_unique<AutoRelMoveOrchestrator>(manager, groupName);
    }

    /// 快捷获取轴对象引用
    Axis& axis() {
        SystemContext* group = nullptr;
        ContextRejection reason;
        manager.tryGetGroup(groupName, group, reason);
        Axis* axisPtr = nullptr;
        ContextRejection ctxReason;
        group->tryGetAxis(targetId, axisPtr, ctxReason);
        return *axisPtr;
    }
};

// =====================================================================
// Error 状态测试
// =====================================================================
// 全局最高优先级的错误拦截：任何阶段检测到 AxisState::Error → 立即熔断
// =====================================================================

/**
 * @test 轴处于 Error 状态时，编排器应直接进入 Error 阶段
 *
 * 场景：启动编排器前轴已处于异常状态
 * 验证：currentStep() == Error，且不发送任何指令
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Error);
    EXPECT_TRUE(driver.history.empty());
}

// =====================================================================
// EnsuringEnabled 阶段测试
// =====================================================================

/**
 * @test 轴处于 Disabled 状态时，编排器应发送 Enable 指令
 *
 * 场景：轴断电状态下启动相对运动
 * 验证：发出 EnableCommand(true)，阶段保持在 EnsuringEnabled
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick();

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_TRUE(cmd.active);
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::EnsuringEnabled);
}

/**
 * @test 轴处于 Idle 状态时，编排器应跳至 IssuingMove
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldGoToIssuingMoveWhenAxisIsIdle)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::IssuingMove);
    EXPECT_TRUE(driver.history.empty());
}

/**
 * @test 轴尚未 Idle 时不应发送 Move 指令
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotMoveBeforeAxisBecomesIdle)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_FALSE(driver.has<MoveCommand>());
}

// =====================================================================
// IssuingMove 阶段测试
// =====================================================================

/**
 * @test 非 Idle 状态不能发送 Move 指令
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotSendMoveWhenAxisNotIdle)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick();

    EXPECT_FALSE(driver.has<MoveCommand>());
}

/**
 * @test 进入 IssuingMove 的首帧不应发送 Move 指令（两帧分离设计）
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldOnlyTransitionToIssuingMoveWhenAxisBecomesIdle)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::IssuingMove);
    EXPECT_FALSE(driver.has<MoveCommand>());
}

/**
 * @test IssuingMove 的第二帧应发送 MoveRelative 指令
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveOnlyOnNextTickAfterEnteringIssuingMove)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → 发 Move

    ASSERT_TRUE(driver.has<MoveCommand>());
    auto cmd = driver.lastForAxis<MoveCommand>(targetId);
    EXPECT_EQ(cmd.type, MoveType::Relative);
    EXPECT_DOUBLE_EQ(cmd.target, 1.0);
}

/**
 * @test Move 指令最多只发送一次（幂等性）
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveOnlyOnce)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move
    orchestrator->tick();
    orchestrator->tick();

    int moveCount = std::count_if(driver.history.begin(), driver.history.end(), [](const FakeAxisDriver::Record& r){
        return std::holds_alternative<MoveCommand>(r.cmd);
    });
    EXPECT_EQ(moveCount, 1);
}

/**
 * @test Move 被领域层拒绝时，应掉电并进入 Error 状态
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableAndEnterErrorWhenMoveRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 100.0, .negLimitValue = -100.0});
    orchestrator->startRel(targetId, 9999.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move 被拒绝

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_FALSE(cmd.active);
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Error);
}

/**
 * @test Move 发出后应进入 WaitingMotionStart 阶段
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterWaitingMotionStartAfterMoveSuccess)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}

// =====================================================================
// WaitingMotionStart 阶段测试
// =====================================================================

/**
 * @test 通过位置变化检测运动（小位移场景，无 Moving 状态）
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDetectMotionByPositionDeltaEvenWithoutMovingState)
{
    orchestrator->startRel(targetId, 0.5);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionStart);

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test 轴状态变为 MovingRelative 时检测到运动
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterWaitingMotionFinishWhenAxisStartsMoving)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.1, .relPos = 0.1, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test 无运动发生时保持 WaitingMotionStart
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayIfNoMotionObserved)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}

/**
 * @test WaitingMotionStart 期间轴进入 Error 时应进入 Error 阶段
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisReportsError)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Error);
}

/**
 * @test 运动观测具有锁存特性
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldLatchMotionObserved)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.2, .relPos = 0.2, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.01, .relPos = 0.01, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

// =====================================================================
// WaitingMotionFinish 阶段测试
// =====================================================================

/**
 * @test 运动完成时应掉电并进入 Done
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableWhenMotionFinished)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick(); // → WaitingMotionFinish

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 1.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_FALSE(cmd.active);
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Done);
}

/**
 * @test 运动未到位时不应完成
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfNotInPosition)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick(); // → WaitingMotionFinish

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.8, .relPos = 0.8, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_NE(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Done);
}

/**
 * @test Idle + 未到位 → 必须保持 WaitingMotionFinish
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfNotInPosition2)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick(); // → WaitingMotionFinish

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.3, .relPos = 0.3, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_NE(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Done);
    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test 从未观测到运动时，即使轴 Idle 且位置到达目标也不应完成
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfMotionNeverObserved)
{
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 1.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_FALSE(driver.has<EnableCommand>());
}

// =====================================================================
// 相对运动特有：非零起点完成判定
// =====================================================================

/**
 * @test 非零起点的相对运动应正确判定完成
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldCompleteRelativeMoveFromNonZeroStart)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 5.0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 3.0); // 期望终点 = 5.0 + 3.0 = 8.0
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 6.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick(); // → WaitingMotionFinish

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 8.0, .relPos = 3.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Done);
    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_FALSE(cmd.active);
}

/**
 * @test 非零起点下端到位 ≠ 完成
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfNotReachingStartPlusDistance)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 5.0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 3.0);
    orchestrator->tick(); // → IssuingMove
    orchestrator->tick(); // → Move

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 6.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick(); // → WaitingMotionFinish

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 7.0, .relPos = 2.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_NE(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Done);
}

// =====================================================================
// SystemManager 层错误测试
// =====================================================================

/**
 * @test 分组不存在 → 立即 Error + GroupNotFound
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenGroupNotFound)
{
    AutoRelMoveOrchestrator badOrch(manager, "NonExistentGroup");
    badOrch.startRel(AxisId::Y, 100.0);
    badOrch.tick();

    EXPECT_EQ(badOrch.currentStep(), AutoRelMoveOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<ContextRejection>(badOrch.lastError()));
    EXPECT_EQ(std::get<ContextRejection>(badOrch.lastError()), ContextRejection::GroupNotFound);
}

/**
 * @test Error 终态幂等：多次 tick 保持 Error
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldRemainInErrorAfterMultipleTicks)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startRel(targetId, 1.0);
    orchestrator->tick(); // → Error
    orchestrator->tick();
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), AutoRelMoveOrchestrator::Step::Error);
}
