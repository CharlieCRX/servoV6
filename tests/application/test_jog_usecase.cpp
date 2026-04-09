#include <gtest/gtest.h>
#include <vector>
#include "application/axis/JogAxisUseCase.h"

// 构造一个 FakeDriver 用于验证命令流向
class FakeAxisDriver : public IAxisDriver {
public:
    void send(const AxisCommand& cmd) override { history.push_back(cmd); }
    std::vector<AxisCommand> history;
};

// 场景 1：Idle 状态下直接点动
TEST(JogAxisUseCaseTest, ShouldSendJogCommandWhenAxisIsIdle) {
    FakeAxisDriver driver;
    Axis axis;
    // 准备环境：Idle 状态且限位正常
    axis.applyFeedback({.state = AxisState::Idle, .posLimitValue = 1000.0, .negLimitValue = -1000.0});

    JogAxisUseCase usecase(driver);
    usecase.execute(axis, Direction::Forward);

    // 验证：驱动器收到了 JogCommand
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::holds_alternative<JogCommand>(driver.history[0]));
}

// 场景 2：Disabled 状态下自动下发使能指令
TEST(JogAxisUseCaseTest, ShouldSendEnableCommandWhenAxisIsDisabled) {
    FakeAxisDriver driver;
    Axis axis;
    // 准备环境：Disabled 状态
    axis.applyFeedback({.state = AxisState::Disabled});

    JogAxisUseCase usecase(driver);
    usecase.execute(axis, Direction::Forward);

    // 验证：
    // 1. 驱动器收到了指令
    ASSERT_EQ(driver.history.size(), 1);
    // 2. 该指令必须是 EnableCommand(true)
    ASSERT_TRUE(std::holds_alternative<EnableCommand>(driver.history[0]));
    EXPECT_TRUE(std::get<EnableCommand>(driver.history[0]).active);
}

// 场景 3：Error 状态下既不点动也不使能
TEST(JogAxisUseCaseTest, ShouldSendNothingWhenAxisIsInError) {
    FakeAxisDriver driver;
    Axis axis;
    // 准备环境：Error 状态
    axis.applyFeedback({.state = AxisState::Error});

    JogAxisUseCase usecase(driver);
    usecase.execute(axis, Direction::Forward);

    // 验证：驱动器不应收到任何指令
    EXPECT_EQ(driver.history.size(), 0);
}