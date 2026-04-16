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