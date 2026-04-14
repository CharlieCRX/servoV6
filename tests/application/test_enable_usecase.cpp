// tests/application/test_enable_usecase.cpp
#include <gtest/gtest.h>
#include "application/axis/EnableUseCase.h"
namespace { 
    class FakeAxisDriver : public IAxisDriver {
    public:
        void send(const AxisCommand& cmd) override { history.push_back(cmd); }
        std::vector<AxisCommand> history;
    };
}
// 场景 1：正常上电
TEST(EnableUseCaseTest, ShouldSendEnableCommandWhenAxisIsDisabled) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Disabled});

    EnableUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, true);

    EXPECT_EQ(result, RejectionReason::None);
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::get<EnableCommand>(driver.history[0]).active);
}

// 场景 2：安全拦截——运动中禁止断电
TEST(EnableUseCaseTest, ShouldRejectDisableWhenAxisIsMoving) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::MovingAbsolute});

    EnableUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, false); // 尝试断电

    EXPECT_EQ(result, RejectionReason::AlreadyMoving); // Domain 层应拦截
    EXPECT_EQ(driver.history.size(), 0);
}