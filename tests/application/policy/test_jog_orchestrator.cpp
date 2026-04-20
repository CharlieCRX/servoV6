#include <gtest/gtest.h>
#include "application/policy/JogOrchestrator.h" // 根据实际路径调整

// 复用你设计的极佳的测试辅助类
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

class JogOrchestratorTest : public ::testing::Test {
protected:
    RecordingAxisDriver driver;

    EnableUseCase enableUc{driver};
    JogAxisUseCase jogUc{driver};

    JogOrchestrator orchestrator{enableUc, jogUc};

    Axis axis;

    void SetUp() override {
        // 默认初始化为一个静止、健康的空闲态
        axis.applyFeedback({
            AxisState::Idle,
            0.0, 0.0, 0.0,
            false, false,
            1000.0, -1000.0
        });
    }
};

/**
 * ========================
 * EnsuringEnabled 语义约束 (Jog 专属)
 * ========================
 * 1. 全局最高优先级
 * - 任何阶段遇到 AxisState::Error，立即进入 Error 阶段。
 *
 * 2. 状态驱动上电
 * - 当 AxisState 为 Disabled 时，必须触发 EnableUseCase 发送上电指令，并保持在 EnsuringEnabled。
 *
 * 3. 阻塞等待
 * - 如果 AxisState 为 Unknown 或其他非稳态，保持在 EnsuringEnabled 等待。
 * * 4. 阶段流转
 * - 当 AxisState 变为 Idle 时，流转至 IssuingJog，但当前 tick 不允许发送 JogCommand。
 */

// Test 1：全局拦截 - Error 必须立即终止流程
TEST_F(JogOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis.applyFeedback({
        AxisState::Error,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.startJog(Direction::Forward);

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              JogOrchestrator::Step::Error);

    EXPECT_TRUE(driver.history.empty());
}

// Test 2：Disabled → 必须触发 Enable
TEST_F(JogOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.startJog(Direction::Forward);

    orchestrator.update(axis);

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_TRUE(cmd.active);

    EXPECT_EQ(orchestrator.currentStep(),
              JogOrchestrator::Step::EnsuringEnabled);
}

// Test 3：状态未知（Unknown）→ 阻塞等待，不进入下一阶段
TEST_F(JogOrchestratorTest, ShouldStayInEnsuringEnabledWhenAxisIsUnknown)
{
    axis.applyFeedback({
        AxisState::Unknown,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.startJog(Direction::Forward);

    orchestrator.update(axis);

    EXPECT_EQ(orchestrator.currentStep(),
              JogOrchestrator::Step::EnsuringEnabled);

    // 未知状态下不应发指令
    EXPECT_FALSE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<JogCommand>());
}

// Test 4：Idle → 进入 IssuingJog（但当前 tick 绝不发 Jog）
TEST_F(JogOrchestratorTest, ShouldTransitionToIssuingJogButNotSendJogCommandYet)
{
    axis.applyFeedback({
        AxisState::Disabled,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.startJog(Direction::Forward);

    orchestrator.update(axis); // Tick 1: 发送 Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<JogCommand>());

    // 模拟底层反馈：上电成功，进入 Idle
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });

    orchestrator.update(axis); // Tick 2: 流转状态

    EXPECT_EQ(orchestrator.currentStep(),
              JogOrchestrator::Step::IssuingJog);

    // 严苛约束：状态流转的瞬间不允许跨阶段执行（不允许带出 Jog 指令）
    EXPECT_FALSE(driver.has<JogCommand>());
}