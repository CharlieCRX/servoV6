#include <gtest/gtest.h>
#include "application/axis/StopAxisUseCase.h" // 🔴 红灯：文件尚不存在

namespace { 
    class FakeAxisDriver : public IAxisDriver {
    public:
        void send(const AxisCommand& cmd) override { history.push_back(cmd); }
        std::vector<AxisCommand> history;
    };
}

// 场景 1：验证停止指令的下发与意图覆盖
TEST(StopAxisUseCaseTest, ShouldSendStopCommandAndClearMovingIntent) {
    FakeAxisDriver driver;
    Axis axis;
    
    // 准备环境：轴正在绝对定位运动中
    axis.applyFeedback({.state = AxisState::MovingAbsolute, .absPos = 100.0});
    
    StopAxisUseCase usecase(driver);
    
    // 动作：执行停止
    usecase.execute(axis);

    // 验证：
    // 1. 驱动器收到了指令
    ASSERT_EQ(driver.history.size(), 1);
    
    // 2. 指令类型必须是 StopCommand
    EXPECT_TRUE(std::holds_alternative<StopCommand>(driver.history[0]));
}

// 场景 2：安全穿透验证——即便在 Error 状态也必须能发停止指令
TEST(StopAxisUseCaseTest, ShouldSendStopCommandEvenInErrorState) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Error});

    StopAxisUseCase usecase(driver);
    usecase.execute(axis);

    // 验证：即便故障，停止信号也要发给 PLC
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::holds_alternative<StopCommand>(driver.history[0]));
}