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