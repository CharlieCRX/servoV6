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

    // 辅助函数：快速把状态机推入 Jogging 阶段
    void runToJoggingState(Direction dir = Direction::Forward) {
        axis.applyFeedback({AxisState::Idle, 0,0,0,false,false,1000,-1000});
        orchestrator.startJog(dir);
        orchestrator.update(axis); // -> 到 EnsuringEnabled (发现Idle) -> IssuingJog
        orchestrator.update(axis); // -> 发送 JogCommand，进入 Jogging
        driver.history.clear();    // 清理历史，保证接下来的断言干净
    }

    // 辅助函数：快速把状态机推入 WaitingForIdle 阶段
    void runToWaitingForIdleState(Direction dir = Direction::Forward) {
        runToJoggingState(dir);             // 先跑起来
        orchestrator.stopJog(dir);          // 喊停
        orchestrator.update(axis);          // Tick: 执行 IssuingStop，流转到 WaitingForIdle
        driver.history.clear();             // 清理干净历史记录
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


/**
 * ========================
 * IssuingJog 语义约束
 * ========================
 */

// Test 5：成功下发 Jog 指令，并进入 Jogging 阶段
TEST_F(JogOrchestratorTest, ShouldSendJogCommandAndEnterJogging)
{
    // Arrange: 处于 Idle 状态
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,false,false,1000,-1000
    });
    orchestrator.startJog(Direction::Forward);
    orchestrator.update(axis); // Tick 1: EnsuringEnabled -> IssuingJog

    // Act: Tick 2: 在 IssuingJog 阶段执行
    orchestrator.update(axis); 

    // Assert: 验证指令下发
    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.last<JogCommand>();
    EXPECT_EQ(cmd.dir, Direction::Forward);
    EXPECT_TRUE(cmd.active); // active 为 true 代表开始点动

    // Assert: 验证状态流转
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Jogging);
}

// Test 6：反向点动测试，确保方向参数正确透传
TEST_F(JogOrchestratorTest, ShouldSendBackwardJogCommandCorrectly)
{
    axis.applyFeedback({ AxisState::Idle, 0,0,0,false,false,1000,-1000 });
    
    orchestrator.startJog(Direction::Backward); // ⭐ 发起反向点动
    orchestrator.update(axis); // -> IssuingJog
    orchestrator.update(axis); // -> 执行 IssuingJog

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.last<JogCommand>();
    EXPECT_EQ(cmd.dir, Direction::Backward); // 必须是 Backward
    EXPECT_TRUE(cmd.active);
}

// Test 7：幂等性 - 无论 update 多少次，JogCommand 只能发送一次
TEST_F(JogOrchestratorTest, ShouldSendJogCommandOnlyOnce)
{
    axis.applyFeedback({ AxisState::Idle, 0,0,0,false,false,1000,-1000 });
    orchestrator.startJog(Direction::Forward);
    orchestrator.update(axis); // -> IssuingJog
    
    // Act: 疯狂调用 update
    orchestrator.update(axis); // -> 发送 Jog，进入 Jogging
    orchestrator.update(axis); 
    orchestrator.update(axis);
    orchestrator.update(axis);

    // Assert: 统计 JogCommand 发送次数，必须严格等于 1
    int count = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const AxisCommand& c){
            return std::holds_alternative<JogCommand>(c);
        });

    EXPECT_EQ(count, 1);
}

// Test 8：失败熔断 - 触发限位被拒时，必须安全掉电并记录 Error
TEST_F(JogOrchestratorTest, ShouldDisableAndEnterErrorWhenJogRejected)
{
    // Arrange: 处于 Idle，但是正限位已经被触发 (posLimit = true)
    axis.applyFeedback({
        AxisState::Idle,
        0,0,0,
        true, false, // ⭐ 正限位触发
        1000,-1000
    });

    orchestrator.startJog(Direction::Forward); // 尝试向限位方向点动
    orchestrator.update(axis); // -> IssuingJog

    // Act: 执行 IssuingJog，此时底层实体 Axis 将拒绝此操作
    orchestrator.update(axis);

    // Assert 1: 验证状态进入 Error
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Error);

    // Assert 2: 验证记录了正确的拒绝原因
    EXPECT_EQ(orchestrator.errorReason(), RejectionReason::AtPositiveLimit);

    // Assert 3: 验证发出了掉电保护指令
    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active); // 必须是掉电 (active = false)
}



/**
 * ========================
 * Jogging 语义约束
 * ========================
 */

// Test 9：收到方向匹配的 stopJog 请求，必须进入 IssuingStop
TEST_F(JogOrchestratorTest, ShouldTransitionToIssuingStopOnValidStopJog)
{
    // Arrange
    runToJoggingState(Direction::Forward);

    // Act
    orchestrator.stopJog(Direction::Forward);

    // Assert
    // 注意：stopJog 是直接改变状态的外部事件，不需要等 update
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::IssuingStop);
}

// Test 10：方向防误杀 - 收到错误的 stopJog 方向，必须忽略
TEST_F(JogOrchestratorTest, ShouldIgnoreStopJogWithWrongDirection)
{
    // Arrange
    runToJoggingState(Direction::Forward);

    // Act
    orchestrator.stopJog(Direction::Backward); // 传一个错的方向

    // Assert
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Jogging);
}

// Test 11：意外跌落保护 - 如果底层自动 Idle，必须主动进入 IssuingStop
TEST_F(JogOrchestratorTest, ShouldAutoTransitionToIssuingStopIfAxisDropsToIdle)
{
    // Arrange
    runToJoggingState(Direction::Forward);

    // 模拟一种跌落场景：触发了正限位。
    // 根据我们之前的 Axis 实体逻辑，如果限位触发，它会自动清空 m_pending_intent 并跌落
    axis.applyFeedback({
        AxisState::Idle,  // 状态变成了空闲
        0,0,0,
        true, false,      // ⭐ 正限位被压下
        1000,-1000
    });

    // Act
    orchestrator.update(axis);

    // Assert
    // 尽管外部没人调 stopJog，编排器也应该敏锐地发现异常并切入停止流程
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::IssuingStop);
}

// Test 12：全局熔断 - 运行中报错
TEST_F(JogOrchestratorTest, ShouldEnterErrorWhenAxisReportsErrorDuringJogging)
{
    // Arrange
    runToJoggingState(Direction::Forward);

    // 模拟运行中硬件报警
    axis.applyFeedback({AxisState::Error, 0,0,0, false, false, 1000,-1000});

    // Act
    orchestrator.update(axis);

    // Assert
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Error);
}


/**
 * ========================
 * IssuingStop 语义约束
 * ========================
 */

// Test 13：进入 IssuingStop 后，必须下发停止指令并推进到 WaitingForIdle
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandAndTransitionToWaitingForIdle)
{
    // Arrange: 让系统先跑起来
    runToJoggingState(Direction::Forward);

    // 外部请求停止，状态机切入 IssuingStop
    orchestrator.stopJog(Direction::Forward);
    
    // 清空之前启动时的历史记录，方便断言
    driver.history.clear(); 

    // Act: Tick 驱动执行 IssuingStop 逻辑
    orchestrator.update(axis);

    // Assert 1: 验证确实下发了停止指令 (JogCommand 且 active 为 false)
    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.last<JogCommand>();
    EXPECT_EQ(cmd.dir, Direction::Forward); // 方向必须匹配
    EXPECT_FALSE(cmd.active);               // active 必须为 false (代表停止)

    // Assert 2: 验证状态流转到了下一步（等待停稳）
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::WaitingForIdle);
}

// Test 14：指令幂等性 - 无论由于何种异常导致 update 多次执行，停止指令只发一次
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandOnlyOnce)
{
    // Arrange
    runToJoggingState(Direction::Backward); // 这次测一下反转
    orchestrator.stopJog(Direction::Backward);
    driver.history.clear();

    // Act: 模拟系统异常，在极短时间内疯狂调用 update
    orchestrator.update(axis); // 第一次：应该发指令并流转
    
    // 假设因为某种 Bug，状态被强行改回了 IssuingStop，或者多线程重入
    // 我们要确保底层 Driver 依然安全
    orchestrator.update(axis);
    orchestrator.update(axis);

    // Assert: 统计发送的 Stop 指令数量
    int stopCommandCount = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const AxisCommand& c){
            if (auto* jogCmd = std::get_if<JogCommand>(&c)) {
                return jogCmd->active == false; // 只统计停止指令
            }
            return false;
        });

    // 必须严格等于 1
    EXPECT_EQ(stopCommandCount, 1);
}



/**
 * ========================
 * WaitingForIdle 语义约束
 * ========================
 */

// Test 15：阻塞等待 - 如果轴尚未停稳（非 Idle），必须保持等待，且不发任何指令
TEST_F(JogOrchestratorTest, ShouldStayInWaitingForIdleIfAxisIsNotYetIdle)
{
    // Arrange: 进入等待停止阶段
    runToWaitingForIdleState(Direction::Forward);

    // 模拟底层反馈：电机还在减速中，状态仍为 Jogging
    axis.applyFeedback({
        AxisState::Jogging, 
        0,0,0,false,false,1000,-1000
    });

    // Act
    orchestrator.update(axis);

    // Assert 1: 状态必须保持不变
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::WaitingForIdle);
    
    // Assert 2: 绝对不能在这个等待期间瞎发任何指令（包括 stop 或 enable）
    EXPECT_TRUE(driver.history.empty()); 
}

// Test 16：平滑跃迁 - 一旦轴彻底停稳（变为 Idle），必须流转至 EnsuringDisabled
TEST_F(JogOrchestratorTest, ShouldTransitionToEnsuringDisabledWhenAxisBecomesIdle)
{
    // Arrange: 进入等待停止阶段
    runToWaitingForIdleState(Direction::Forward);

    // 模拟底层反馈：电机减速完毕，完全停稳
    axis.applyFeedback({
        AxisState::Idle, 
        0,0,0,false,false,1000,-1000
    });

    // Act
    orchestrator.update(axis);

    // Assert: 状态必须干净利落地进入断电准备阶段
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::EnsuringDisabled);
}