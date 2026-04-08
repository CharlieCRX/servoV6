#include <gtest/gtest.h>
#include "application/axis/JogAxisUseCase.h" // 此时该文件尚不存在，编译会报错

// 构造一个 FakeDriver 用于验证命令流向
class FakeAxisDriver : public IAxisDriver {
public:
    void send(const AxisCommand& cmd) override { history.push_back(cmd); }
    std::vector<AxisCommand> history;
};

TEST(JogAxisUseCaseTest, ShouldSendCommandWhenAxisAllows) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({AxisState::Idle}); // 准备环境：Idle 状态允许 Jog

    JogAxisUseCase usecase(driver);
    usecase.execute(axis, Direction::Forward);

    // 验证：驱动器是否收到且仅收到了 1 个命令
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::holds_alternative<JogCommand>(driver.history[0]));
}