#include <gtest/gtest.h>
#include "application/policy/AutoAbsMoveOrchestrator.h" 

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


class AutoAbsMoveOrchestratorTest : public ::testing::Test {
protected:
    RecordingAxisDriver driver;

    MoveAbsoluteUseCase moveUc{driver};
    EnableUseCase enableUc{driver};

    AutoAbsMoveOrchestrator orchestrator{enableUc, moveUc};

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

TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis.applyFeedback({
        AxisState::Error,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);

    EXPECT_TRUE(driver.commands().empty()); // 不应发送任何命令
}


TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis);

    ASSERT_FALSE(driver.history.empty());

    const auto& cmd = driver.history.back();
    ASSERT_TRUE(std::holds_alternative<EnableCommand>(cmd));

    EXPECT_TRUE(std::get<EnableCommand>(cmd).active);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::EnsuringEnabled);
}

TEST_F(AutoAbsMoveOrchestratorTest, ShouldGoToIssuingMoveWhenAxisIsIdle)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_TRUE(driver.history.empty()); // 不应发送命令
}


TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotMoveBeforeAxisBecomesIdle)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // Enable

    // 模拟：还没完全使能（仍非 Idle）
    axis.applyFeedback({
        AxisState::Disabled, // 注意：仍然是 Disabled，未过渡到 Idle
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    // ❌ 不应该发 Move
    EXPECT_FALSE(driver.has<MoveCommand>());
}


/**
 * IssuingMove 语义约束 TDD
 * 1. 只能在 AxisState == Idle 时触发 （任何非 Idle → 都不能 Move）
 * 2. 必须调用 moveAbsolute（通过 UseCase）
 * 3. MoveCommand 只能发送一次（幂等）
 * 4. 如果 move 被拒绝：
   → 必须发送 Enable(false)
   → 状态进入 Error
 * 5. 如果 move 成功：
   → 进入 WaitingMotionStart
 */

// 测试1：只有 Idle 才能进入 IssuingMove
TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotSendMoveWhenAxisNotIdle)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // 只会 Enable

    EXPECT_FALSE(driver.has<MoveCommand>());
}

// 测试2：Idle 时只允许“进入 IssuingMove”，不能发 Move
TEST_F(AutoAbsMoveOrchestratorTest, ShouldOnlyTransitionToIssuingMoveWhenAxisBecomesIdle)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    // 进入 Idle
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.update(axis);

    // ❗关键断言
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_FALSE(driver.has<MoveCommand>()); // 🚨 不允许发 Move
}

// 测试3：进入 IssuingMove 后，下一次 update 才发 Move
TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendMoveOnlyOnNextTickAfterEnteringIssuingMove)
{
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    // 第1次 update：进入 IssuingMove
    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_FALSE(driver.has<MoveCommand>());

    // 第2次 update：才发 Move
    orchestrator.update(axis);

    ASSERT_TRUE(driver.has<MoveCommand>());

    auto cmd = driver.last<MoveCommand>();
    EXPECT_TRUE(cmd.type == MoveType::Absolute);
    EXPECT_DOUBLE_EQ(cmd.target, 1.0);
}

// 测试4：MoveCommand 只能发送一次（幂等）
TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendMoveOnlyOnce)
{
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move
    orchestrator.update(axis); // 再 update
    orchestrator.update(axis); // 再 update

    int moveCount = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const AxisCommand& c){
            return std::holds_alternative<MoveCommand>(c);
        });

    EXPECT_EQ(moveCount, 1);
}

// 测试5：如果 move 被拒绝，必须发送 Enable(false) 并进入 Error
TEST_F(AutoAbsMoveOrchestratorTest, ShouldDisableAndEnterErrorWhenMoveRejected)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        100.0, -100.0   // 限位很小
    });

    orchestrator.start(9999.0); // 必然越界

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move 被拒绝

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active); // Disable

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);
}

// 测试6：Move 成功 → 进入 WaitingMotionStart
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterWaitingMotionStartAfterMoveSuccess)
{
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);
}


/**
 * 
WaitingMotionStart 语义约束
1. 进入前提：
   已成功发送 MoveCommand

2. 目标：
   确认“运动已经发生”（不是命令发了，而是物理运动）

3. 判定“运动发生”的条件（任一成立）：
   ✔ AxisState == MovingAbsolute
   ✔ abs(currentPos - startPos) > epsilon

4. 未满足条件：
   → 保持 WaitingMotionStart（不能误推进）

5. 一旦确认运动发生：
   → motionObserved = true（锁存）
   → 进入 WaitingMotionFinish

6. Error 优先级最高：
   AxisState == Error → 立即进入 Error
 */

// Test 1：小位移（无 Moving）也必须进入下一阶段
TEST_F(AutoAbsMoveOrchestratorTest, ShouldDetectMotionByPositionDeltaEvenWithoutMovingState)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(0.5);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);

    // 模拟：直接跳到新位置（没有 Moving）
    axis.applyFeedback({
        AxisState::Idle,
        0.5, 0.5, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 2：通过 MovingAbsolute 进入下一阶段
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterWaitingMotionFinishWhenAxisStartsMoving)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    axis.applyFeedback({
        AxisState::MovingAbsolute,
        0.1, 0.1, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 3：Idle + 无位移 → 不允许推进
TEST_F(AutoAbsMoveOrchestratorTest, ShouldStayIfNoMotionObserved)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 仍然没动
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);
}

// Test 4：Error 立即中断
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterErrorWhenAxisReportsError)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    axis.applyFeedback({
        AxisState::Error,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);
}


// Test 5：motionObserved 一旦成立不能回退
TEST_F(AutoAbsMoveOrchestratorTest, ShouldLatchMotionObserved)
{
    axis.applyFeedback({
        AxisState::Idle,
        0.0, 0.0, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.start(1.0);

    orchestrator.update(axis); // → IssuingMove
    orchestrator.update(axis); // → Move

    // 第一次：发生位移
    axis.applyFeedback({
        AxisState::Idle,
        0.2, 0.2, 0.0,
        false, false,
        1000.0, -1000.0
    });

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);

    // 即使后面回到接近原点（抖动）
    axis.applyFeedback({
        AxisState::Idle,
        0.01, 0.01, 0.0,
        false, false,
        1000.0, -1000.0
    });

    // 不应回到 WaitingMotionStart（不会回退）
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}