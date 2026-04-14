#include <gtest/gtest.h>
#include "application/axis/MoveRelativeUseCase.h" // 🔴 红灯：文件尚不存在

namespace { 
    class FakeAxisDriver : public IAxisDriver {
    public:
        void send(const AxisCommand& cmd) override { history.push_back(cmd); }
        std::vector<AxisCommand> history;
    };
}
// 场景 1：正常相对位移下发
TEST(MoveRelativeUseCaseTest, ShouldSendMoveCommandWhenDistanceIsLegal) {
    FakeAxisDriver driver;
    Axis axis;
    // 环境：当前位置 100.0，限位 [0, 1000]
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 100.0, .posLimitValue = 1000.0, .negLimitValue = 0.0});

    MoveRelativeUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, 50.0); // 预期终点 150.0

    EXPECT_EQ(result, RejectionReason::None);
    ASSERT_EQ(driver.history.size(), 1);
    auto cmd = std::get<MoveCommand>(driver.history[0]);
    EXPECT_EQ(cmd.type, MoveType::Relative);
    EXPECT_DOUBLE_EQ(cmd.target, 50.0);
}

// 场景 2：未使能拦截（不执行自愈）
TEST(MoveRelativeUseCaseTest, ShouldReturnInvalidStateWhenDisabled) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Disabled});

    MoveRelativeUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, 10.0);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_EQ(driver.history.size(), 0);
}

// 场景 3：增量导致终点越界拦截
TEST(MoveRelativeUseCaseTest, ShouldRejectWhenRelativeDistanceExceedsLimit) {
    FakeAxisDriver driver;
    Axis axis;
    // 环境：当前位置 900.0，正限位 1000.0
    axis.applyFeedback({.state = AxisState::Idle, .absPos = 900.0, .posLimitValue = 1000.0});

    MoveRelativeUseCase usecase(driver);
    // 动作：移动 +200.0 (预期终点 1100.0，超限)
    RejectionReason result = usecase.execute(axis, 200.0);

    EXPECT_EQ(result, RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_EQ(driver.history.size(), 0);
}