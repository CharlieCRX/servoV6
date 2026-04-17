#include <gtest/gtest.h>
#include "application/policy/AutoRelMoveOrchestrator.h"

class RecordingAxisDriver : public IAxisDriver {
public:
    void send(const AxisCommand& cmd) override {
        history.push_back(cmd);
    }

    const std::vector<AxisCommand>& commands() const {
        return history;
    }

    template<typename T>
    bool has() const {
        return std::any_of(history.begin(), history.end(),
            [](const AxisCommand& c) {
                return std::holds_alternative<T>(c);
            });
    }

    template<typename T>
    T last() const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (std::holds_alternative<T>(*it)) {
                return std::get<T>(*it);
            }
        }
        throw std::runtime_error("Command not found");
    }
    std::vector<AxisCommand> history;
};

class AutoRelMoveOrchestratorTest : public ::testing::Test {
protected:
    RecordingAxisDriver driver;

    MoveRelativeUseCase moveUc{driver};
    EnableUseCase enableUc{driver};

    AutoRelMoveOrchestrator orchestrator{enableUc, moveUc};

    Axis axis;

    void SetUp() override {
        axis.applyFeedback({
            AxisState::Idle,
            0.0, 0.0, 0.0,
            false, false,
            1000.0, -1000.0
        });
    }
};

/**
 * EnsuringEnabled - TDD 测试用例设计
 */
// Test 1：Error 必须立即终止
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis.applyFeedback({
        AxisState::Error,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0); // Δ=1.0

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Error);

    EXPECT_TRUE(driver.history.empty());
}

// Test 2：Disabled → 必须 Enable
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis);

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_TRUE(cmd.active);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::EnsuringEnabled);
}

// Test 3：非 Idle 状态不能进入下一阶段
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayInEnsuringEnabledWhenAxisNotIdle)
{
    axis.applyFeedback({
        AxisState::MovingAbsolute,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::EnsuringEnabled);

    EXPECT_FALSE(driver.has<MoveCommand>());
}


// Test 4：Idle → 进入 IssuingMove（但不发 Move）
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotSendMoveDuringEnableToIdleTransition)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    // 还没 Idle（假设中间状态）
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_FALSE(driver.has<MoveCommand>());
}




// ========================
// IssuingMove 语义约束
// ========================
/**
 * IssuingMove（RelMove）语义约束
 *
 * 1. 触发条件
 *    - 仅当 Step == IssuingMove 时允许发起 MoveRelative
 *
 * 2. 执行方式
 *    - 必须通过 moveUc.execute(axis, distance)
 *
 * 3. 分 tick 执行（禁止跨阶段）
 *    - 第一次 update：进入 IssuingMove（不发送 Move）
 *    - 第二次 update：发送 MoveCommand
 *
 * 4. 幂等性
 *    - MoveCommand 只能发送一次（后续 update 不得重复发送）
 *
 * 5. 起点采样（RelMove核心）
 *    - MoveCommand.startAbs 必须等于“发送命令瞬间”的 absolute position
 *    - startPos 必须在发送 Move 的同一 tick 记录
 *
 * 6. 成功路径
 *    - moveUc.execute() == RejectionReason::None
 *      → 记录 startPos
 *      → 进入 WaitingMotionStart
 *
 * 7. 失败路径
 *    - moveUc.execute() != None
 *      → 必须发送 Enable(false)
 *      → 进入 Error
 *
 * 8. 状态隔离
 *    - IssuingMove 仅负责“发起 Move”
 *    - 不允许在该阶段判断运动/完成
 */


// Test 1：进入 IssuingMove 不应立即发送 Move
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotSendMoveOnFirstTickOfIssuingMove)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::IssuingMove);

    EXPECT_FALSE(driver.has<MoveCommand>());
}

// Test 2：第二次 update 才发送 Move
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveRelativeOnSecondTick)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → 发 Move

    ASSERT_TRUE(driver.has<MoveCommand>());

    auto cmd = driver.last<MoveCommand>();

    EXPECT_EQ(cmd.type, MoveType::Relative);   // ⭐ 必须加
    EXPECT_DOUBLE_EQ(cmd.target, 1.0);
}

// Test 3：MoveCommand.startAbs 必须正确
TEST_F(AutoRelMoveOrchestratorTest, ShouldCaptureCorrectStartPosition)
{
    axis.applyFeedback({
        AxisState::Idle,
        5.0, 5.0, 0.0,
        false, false,
        1000, -1000
    });

    orchestrator.start(2.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    auto cmd = driver.last<MoveCommand>();

    EXPECT_DOUBLE_EQ(cmd.startAbs, 5.0);
}


// Test 4：Move 只能发送一次
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveOnlyOnce)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move
    orchestrator.update(axis); // 再 update

    int count = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const AxisCommand& c){
            return std::holds_alternative<MoveCommand>(c);
        });

    EXPECT_EQ(count, 1);
}

// Test 5：Move 被拒绝 → Disable + Error
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableAndEnterErrorWhenMoveRejected)
{
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,
        10.0, -10.0
    });

    orchestrator.start(100.0); // 超限

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move失败

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active);   // Disable

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Error);
}

// Test 6：成功后进入 WaitingMotionStart
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterWaitingMotionStartAfterMove)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}


// Test 7：即使 Move 被拒绝，Axis 状态仍可能是 Idle（未进入 Error）
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorEvenIfAxisStateIsStillIdleWhenMoveRejected)
{
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,
        10.0, -10.0
    });

    orchestrator.start(100.0); // 超限

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move失败

    // Axis 仍然是 Idle（没有Error状态）
    EXPECT_EQ(axis.state(), AxisState::Idle);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Error);
}

/**
 * WaitingMotionStart（RelMove）语义约束
 *
 * 1. 进入前提
 *    - 已成功发送 MoveRelative（来自 IssuingMove）
 *
 * 2. 阶段目标
 *    - 确认“运动已经发生”（不是命令发送成功，而是物理运动发生）
 *
 * 3. 运动发生判定（任一成立）
 *    - AxisState == MovingRelative
 *    - abs(currentAbsPos - startPos) > epsilon
 *
 * 4. 状态推进
 *    - 一旦检测到运动发生：
 *      → m_motionObserved = true
 *      → 进入 WaitingMotionFinish
 *
 * 5. 未发生运动
 *    - 保持 WaitingMotionStart（不得误推进）
 *
 * 6. Error 优先级最高
 *    - AxisState == Error → 立即进入 Error
 *
 * 7. 单调性（关键）
 *    - m_motionObserved 一旦为 true，不允许回退为 false
 *
 * 8. RelMove 特别约束
 *    - 运动判定必须基于“startPos”
 *    - 不允许使用 target（因为 target = startPos + Δ）
 */

// Test 1：Idle + 无位移 → 不允许推进
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayIfNoMotionObserved)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 没有发生任何位移
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}

// Test 2：通过 MovingRelative 判定运动
TEST_F(AutoRelMoveOrchestratorTest, ShouldDetectMotionWhenStateIsMovingRelative)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    axis.applyFeedback({
        AxisState::MovingRelative,
        0.1,0.1,0,
        false,false,
        1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 3：通过“位置变化”判定（无 Moving 状态）
TEST_F(AutoRelMoveOrchestratorTest, ShouldDetectMotionByPositionDelta)
{
    axis.applyFeedback({
        AxisState::Idle,
        5.0, 5.0, 0.0,
        false,false,
        1000,-1000
    });

    orchestrator.start(0.5);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 直接跳位置（无 MovingRelative）
    axis.applyFeedback({
        AxisState::Idle,
        5.5, 5.5, 0.0,
        false,false,
        1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 4：Error 优先级最高
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisReportsError)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    axis.applyFeedback({
        AxisState::Error,
        0,0,0,false,false,
        1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Error);
}

// Test 5：一旦进入 Finish，不允许回退（锁存）
TEST_F(AutoRelMoveOrchestratorTest, ShouldLatchMotionObserved)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 第一次：发生位移
    axis.applyFeedback({
        AxisState::Idle,
        0.2,0.2,0,
        false,false,
        1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionFinish);

    // 再次 update，不允许回退
    axis.applyFeedback({
        AxisState::Idle,
        0.01,0.01,0,
        false,false,
        1000,-1000
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}


/**
 * WaitingMotionFinish（RelMove）语义约束
 *
 * 1. 进入前提
 *    - 已观测到运动发生（m_motionObserved == true）
 *
 * 2. 完成判定（唯一标准）
 *    - 必须依赖 Axis 提供的语义接口：
 *      axis.isMoveCompleted()
 *
 * 3. 状态推进
 *    - 当 isMoveCompleted() == true：
 *      → 发送 Enable(false)
 *      → 进入 Done
 *
 * 4. 未完成
 *    - isMoveCompleted() == false：
 *      → 保持 WaitingMotionFinish
 *
 * 5. 防假完成（关键）
 *    - 若 m_motionObserved == false：
 *      → 不允许完成
 *
 * 6. 职责边界（非常重要）
 *    - 不允许在该阶段：
 *      ❌ 判断位置误差（pos-target）
 *      ❌ 计算 target（startPos + Δ）
 *
 * 7. Error 优先级（由外层统一处理）
 */


// Test 1：完成 → 进入 Done + Disable
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableAndEnterDoneWhenMoveCompleted)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 发生运动
    axis.applyFeedback({AxisState::MovingRelative, 0.5,0.5,0,false,false,1000,-1000});
    orchestrator.update(axis); // → WaitingMotionFinish

    // ⭐ 模拟 Axis 已完成（pending 被清）
    axis.applyFeedback({AxisState::Idle, 1.0,1.0,0,false,false,1000,-1000});

    orchestrator.update(axis);

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Done);
}

// Test 2：未完成 → 不能 Done
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayIfMoveNotCompleted)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 已开始运动
    axis.applyFeedback({AxisState::MovingRelative, 0.5,0.5,0,false,false,1000,-1000});
    orchestrator.update(axis); // → WaitingMotionFinish

    // ⭐ 未到位（pending仍存在）
    axis.applyFeedback({AxisState::Idle, 0.8,0.8,0,false,false,1000,-1000});

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::WaitingMotionFinish);

    EXPECT_FALSE(driver.has<EnableCommand>());
}


// Test 3：未观测到运动 → 不允许完成（防假完成）
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfMotionNeverObserved)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // ⚠️ 直接 Idle + 到位（没有 motionObserved）
    axis.applyFeedback({AxisState::Idle, 1.0,1.0,0,false,false,1000,-1000});

    orchestrator.update(axis);

    EXPECT_NE(orchestrator.currentStep(),
              AutoRelMoveOrchestrator::Step::Done);
}


// Test 4：完成后不重复发送 Disable（幂等）
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableOnlyOnce)
{
    axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});

    orchestrator.start(1.0);

    orchestrator.update(axis);
    orchestrator.update(axis);

    axis.applyFeedback({AxisState::MovingRelative, 0.5,0.5,0,false,false,1000,-1000});
    orchestrator.update(axis);

    axis.applyFeedback({AxisState::Idle, 1.0,1.0,0,false,false,1000,-1000});
    orchestrator.update(axis);

    orchestrator.update(axis);
    orchestrator.update(axis);

    int count = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const AxisCommand& c){
            return std::holds_alternative<EnableCommand>(c) &&
                   !std::get<EnableCommand>(c).active;
        });

    EXPECT_EQ(count, 1);
}