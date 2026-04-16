#include <gtest/gtest.h>
#include "application/axis/MoveAbsoluteUseCase.h" 

namespace { 
    class FakeAxisDriver : public IAxisDriver {
    public:
        void send(const AxisCommand& cmd) override { history.push_back(cmd); }
        std::vector<AxisCommand> history;
    };
}

// 1. 成功场景：Idle 状态下正常发送定位指令
TEST(MoveAbsoluteUseCaseTest, ShouldSendMoveCommandWhenIdle) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Idle, .posLimitValue = 1000.0});

    MoveAbsoluteUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, 500.0);

    EXPECT_EQ(result, RejectionReason::None);
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::holds_alternative<MoveCommand>(driver.history[0]));
}

// 2. 拦截场景：未使能时直接返回 InvalidState，不执行自愈
TEST(MoveAbsoluteUseCaseTest, ShouldReturnInvalidStateWhenDisabled) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Disabled});

    MoveAbsoluteUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, 500.0);

    // ⭐ 核心约束：不再发送 EnableCommand，诚实汇报错误
    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_EQ(driver.history.size(), 0);
}

// 3. 拦截场景：目标超限时透传 RejectionReason
TEST(MoveAbsoluteUseCaseTest, ShouldReturnTargetOutOfLimitWhenTargetIsIllegal) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Idle, .posLimitValue = 1000.0});

    MoveAbsoluteUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, 1500.0); // 目标越界

    EXPECT_EQ(result, RejectionReason::TargetOutOfPositiveLimit);
    EXPECT_EQ(driver.history.size(), 0);
}