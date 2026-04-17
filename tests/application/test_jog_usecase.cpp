#include <gtest/gtest.h>
#include <vector>
#include "application/axis/JogAxisUseCase.h"

// 构造一个 FakeDriver 用于验证命令流向
namespace { 
    class FakeAxisDriver : public IAxisDriver {
    public:
        void send(const AxisCommand& cmd) override { history.push_back(cmd); }
        std::vector<AxisCommand> history;
    };
}

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

// 场景 2：Error 状态下既不点动也不使能
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


// 确保 UseCase 能够正确透传 Domain 层的拦截原因
// 场景 1：成功时应返回 None
TEST(JogAxisUseCaseTest, ShouldReturnNoneOnSuccessfulJog) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Idle, .posLimitValue = 1000.0, .negLimitValue = -1000.0});

    JogAxisUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::None);
    EXPECT_EQ(driver.history.size(), 1);
}

// ⭐ 场景 2：限位拦截时，UseCase 应准确汇报原因
TEST(JogAxisUseCaseTest, ShouldReturnLimitReasonWhenAtBoundary) {
    FakeAxisDriver driver;
    Axis axis;
    // 环境：已经在正限位 Bit 触发状态
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .absPos = 1000.0, 
        .posLimit = true, 
        .posLimitValue = 1000.0
    });

    JogAxisUseCase usecase(driver);
    // 动作：尝试向正方向点动
    RejectionReason result = usecase.execute(axis, Direction::Forward);

    // 验证：UseCase 必须告知 UI 是因为撞了正限位
    EXPECT_EQ(result, RejectionReason::AtPositiveLimit);
    EXPECT_EQ(driver.history.size(), 0); // 确保没发指令
}

// ⭐ 场景 3：故障锁死时，UseCase 应汇报状态非法
TEST(JogAxisUseCaseTest, ShouldReturnInvalidStateWhenAxisIsInError) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Error}); //

    JogAxisUseCase usecase(driver);
    RejectionReason result = usecase.execute(axis, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_EQ(driver.history.size(), 0);
}

// stopJog 的测试
// 场景 4：验证停止点动意图的下发
TEST(JogAxisUseCaseTest, ShouldSendStopCommandWhenStopJogRequested) {
    FakeAxisDriver driver;
    Axis axis;
    
    // 1. 准备环境：必须是 Idle 状态才能“启动”点动
    axis.applyFeedback({
        .state = AxisState::Idle, 
        .posLimitValue = 1000.0, 
        .negLimitValue = -1000.0
    });

    JogAxisUseCase usecase(driver);

    // 2. 动作 A：通过 UseCase 启动点动
    // 这样 driver.history 会记录第一条指令
    usecase.execute(axis, Direction::Forward);
    
    // 3. 动作 B：调用停止接口
    usecase.stop(axis, Direction::Forward);

    // 验证：
    // A. 驱动器收到了两条指令：启动 和 停止
    ASSERT_EQ(driver.history.size(), 2); 
    
    // B. 第一条是启动指令 (active = true)
    auto firstCmd = std::get<JogCommand>(driver.history[0]);
    EXPECT_TRUE(firstCmd.active);
    
    // C. 第二条是停止指令 (active = false)
    auto lastCmd = std::get<JogCommand>(driver.history[1]);
    EXPECT_FALSE(lastCmd.active); 
    EXPECT_EQ(lastCmd.dir, Direction::Forward); // 验证方向记忆是否正确
}

// 场景 5：即使在错误状态下，停止也必须被允许并下发
TEST(JogAxisUseCaseTest, ShouldSendStopCommandEvenIfAxisIsInError) {
    FakeAxisDriver driver;
    Axis axis;
    axis.applyFeedback({.state = AxisState::Error});

    JogAxisUseCase usecase(driver);
    usecase.stop(axis, Direction::Backward);

    // 验证：驱动器依然收到了指令，确保硬件能收到“设为 False”的信号
    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_FALSE(std::get<JogCommand>(driver.history[0]).active);
}



// Disabled 必须失败
TEST(JogAxisUseCaseTest, ShouldRejectWhenAxisIsDisabled)
{
    FakeAxisDriver driver;
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Disabled });

    JogAxisUseCase usecase(driver);

    RejectionReason result = usecase.execute(axis, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
    EXPECT_EQ(driver.history.size(), 0);
}


// UseCase 不允许“修复状态”
TEST(JogAxisUseCaseTest, ShouldNotAutoEnableAxis)
{
    FakeAxisDriver driver;
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Disabled });

    JogAxisUseCase usecase(driver);

    usecase.execute(axis, Direction::Forward);

    // 核心验证：绝对不能发送 EnableCommand
    EXPECT_EQ(driver.history.size(), 0);
}


// 只允许纯 Jog 行为
TEST(JogAxisUseCaseTest, ShouldOnlySendJogCommand)
{
    FakeAxisDriver driver;
    Axis axis;

    axis.applyFeedback({
        .state = AxisState::Idle,
        .posLimitValue = 1000,
        .negLimitValue = -1000
    });

    JogAxisUseCase usecase(driver);

    usecase.execute(axis, Direction::Forward);

    ASSERT_EQ(driver.history.size(), 1);
    EXPECT_TRUE(std::holds_alternative<JogCommand>(driver.history[0]));
}


// 返回值必须忠实透传
TEST(JogAxisUseCaseTest, ShouldReturnDomainRejectionWithoutModification)
{
    FakeAxisDriver driver;
    Axis axis;

    axis.applyFeedback({ .state = AxisState::Disabled });

    JogAxisUseCase usecase(driver);

    auto result = usecase.execute(axis, Direction::Forward);

    EXPECT_EQ(result, RejectionReason::InvalidState);
}