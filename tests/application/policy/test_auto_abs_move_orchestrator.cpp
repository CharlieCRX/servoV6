#include <gtest/gtest.h>
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

class RecordingAxisDriver : public IAxisDriver {
public:
    // 新两参数接口（AxisId + AxisCommand）
    void send(AxisId id, const AxisCommand& cmd) override {
        history.push_back({id, cmd});
    }

    struct Record {
        AxisId id;
        AxisCommand cmd;
    };

    const std::vector<Record>& commands() const {
        return history;
    }

    template<typename T>
    bool has() const {
        return std::any_of(history.begin(), history.end(),
            [](const Record& r) {
                return std::holds_alternative<T>(r.cmd);
            });
    }

    template<typename T>
    T last() const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (std::holds_alternative<T>(it->cmd)) {
                return std::get<T>(it->cmd);
            }
        }
        throw std::runtime_error("Command not found");
    }

    template<typename T>
    T lastForAxis(AxisId expectedId) const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->id == expectedId && std::holds_alternative<T>(it->cmd)) {
                return std::get<T>(it->cmd);
            }
        }
        throw std::runtime_error("Command not found for axis");
    }

    bool isEmpty() const { return history.empty(); }

    std::vector<Record> history;
};

class AutoAbsMoveOrchestratorTest : public ::testing::Test {
protected:
    FakePLC plc;
    RecordingAxisDriver driver;
    FakeAxisDriver fakeDriver{plc};
    AxisRepository repo;

    MoveAbsoluteUseCase moveUc{repo, driver};
    EnableUseCase enableUc{repo, driver};

    AutoAbsMoveOrchestrator orchestrator{enableUc, moveUc};

    AxisId targetId = AxisId::Y;

    void SetUp() override {
        repo.registerAxis(targetId);
        Axis& axis = repo.getAxis(targetId);
        axis.applyFeedback({
            .state = AxisState::Idle,
            .absPos = 0.0,
            .relPos = 0.0,
            .relZeroAbsPos = 0.0,
            .posLimit = false,
            .negLimit = false,
            .posLimitValue = 1000.0,
            .negLimitValue = -1000.0
        });
    }

    Axis& axis() { return repo.getAxis(targetId); }
};

// ══════════════════════════════════════════════════════════
// Error 状态测试
// ══════════════════════════════════════════════════════════

TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis().applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);

    EXPECT_TRUE(driver.isEmpty()); // 不应发送任何命令
}

// ══════════════════════════════════════════════════════════
// EnsuringEnabled 阶段
// ══════════════════════════════════════════════════════════

TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis().applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());

    ASSERT_FALSE(driver.isEmpty());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_TRUE(cmd.active);

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::EnsuringEnabled);
}

TEST_F(AutoAbsMoveOrchestratorTest, ShouldGoToIssuingMoveWhenAxisIsIdle)
{
    // SetUp 已经设为 Idle
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_TRUE(driver.isEmpty()); // 不应发送命令
}

TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotMoveBeforeAxisBecomesIdle)
{
    axis().applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // Enable

    // 模拟：还没使能（仍 Disabled）
    axis().applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    // ❌ 不应该发 Move
    EXPECT_FALSE(driver.has<MoveCommand>());
}

// ══════════════════════════════════════════════════════════
// IssuingMove 阶段
// ══════════════════════════════════════════════════════════

// 测试1：只有 Idle 才能进入 IssuingMove
TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotSendMoveWhenAxisNotIdle)
{
    axis().applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // 只会 Enable

    EXPECT_FALSE(driver.has<MoveCommand>());
}

// 测试2：Idle 时只允许"进入 IssuingMove"，不能发 Move
TEST_F(AutoAbsMoveOrchestratorTest, ShouldOnlyTransitionToIssuingMoveWhenAxisBecomesIdle)
{
    axis().applyFeedback({
        .state = AxisState::Disabled,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    // 进入 Idle
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    // ❗关键断言
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_FALSE(driver.has<MoveCommand>()); // 🚨 不允许发 Move
}

// 测试3：进入 IssuingMove 后，下一次 update 才发 Move
TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendMoveOnlyOnNextTickAfterEnteringIssuingMove)
{
    // SetUp 已为 Idle
    orchestrator.start(targetId, 1.0);

    // 第1次 update：进入 IssuingMove
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::IssuingMove);

    EXPECT_FALSE(driver.has<MoveCommand>());

    // 第2次 update：才发 Move
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<MoveCommand>());

    // 验证：命令发送给目标轴
    auto cmd = driver.lastForAxis<MoveCommand>(targetId);
    EXPECT_TRUE(cmd.type == MoveType::Absolute);
    EXPECT_DOUBLE_EQ(cmd.target, 1.0);
}

// 测试4：MoveCommand 只能发送一次（幂等）
TEST_F(AutoAbsMoveOrchestratorTest, ShouldSendMoveOnlyOnce)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move
    orchestrator.update(axis()); // 再 update
    orchestrator.update(axis()); // 再 update

    int moveCount = std::count_if(
        driver.history.begin(), driver.history.end(),
        [](const RecordingAxisDriver::Record& r){
            return std::holds_alternative<MoveCommand>(r.cmd);
        });

    EXPECT_EQ(moveCount, 1);
}

// 测试5：如果 move 被拒绝，必须发送 Enable(false) 并进入 Error
TEST_F(AutoAbsMoveOrchestratorTest, ShouldDisableAndEnterErrorWhenMoveRejected)
{
    // 限位很小
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 100.0,
        .negLimitValue = -100.0
    });

    orchestrator.start(targetId, 9999.0); // 必然越界

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move 被拒绝

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active); // Disable

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);
}

// 测试6：Move 成功 → 进入 WaitingMotionStart
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterWaitingMotionStartAfterMoveSuccess)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);
}

// ══════════════════════════════════════════════════════════
// WaitingMotionStart 阶段
// ══════════════════════════════════════════════════════════

// Test 1：小位移（无 Moving）也必须进入下一阶段
TEST_F(AutoAbsMoveOrchestratorTest, ShouldDetectMotionByPositionDeltaEvenWithoutMovingState)
{
    orchestrator.start(targetId, 0.5);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);

    // 模拟：直接跳到新位置（没有 Moving）
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.5,
        .relPos = 0.5,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 2：通过 MovingAbsolute 进入下一阶段
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterWaitingMotionFinishWhenAxisStartsMoving)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    axis().applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 0.1,
        .relPos = 0.1,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 3：Idle + 无位移 → 不允许推进
TEST_F(AutoAbsMoveOrchestratorTest, ShouldStayIfNoMotionObserved)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    // 仍然没动
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionStart);
}

// Test 4：Error 立即中断
TEST_F(AutoAbsMoveOrchestratorTest, ShouldEnterErrorWhenAxisReportsError)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    axis().applyFeedback({
        .state = AxisState::Error,
        .absPos = 0.0,
        .relPos = 0.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Error);
}

// Test 5：motionObserved 一旦成立不能回退
TEST_F(AutoAbsMoveOrchestratorTest, ShouldLatchMotionObserved)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    // 第一次：发生位移
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.2,
        .relPos = 0.2,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);

    // 即使后面回到接近原点（抖动）
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.01,
        .relPos = 0.01,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    // 不应回到 WaitingMotionStart（不会回退）
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// ══════════════════════════════════════════════════════════
// WaitingMotionFinish 阶段
// ══════════════════════════════════════════════════════════

// Test 1：满足所有条件 → 进入 Done（成功路径）
TEST_F(AutoAbsMoveOrchestratorTest, ShouldDisableWhenMotionFinished)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    // 模拟进入 WaitingMotionFinish
    axis().applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 0.5,
        .relPos = 0.5,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    orchestrator.update(axis()); // → WaitingMotionFinish

    // 完成条件全部满足
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1.0,
        .relPos = 1.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<EnableCommand>());

    auto cmd = driver.last<EnableCommand>();
    EXPECT_FALSE(cmd.active); // Disable

    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Done);
}

// Test 2：未到位 → 不允许完成
TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotCompleteIfNotInPosition)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    axis().applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 0.5,
        .relPos = 0.5,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    orchestrator.update(axis()); // → WaitingMotionFinish

    // 回到 Idle，但位置不对
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.8,
        .relPos = 0.8,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_NE(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Done);
}

// Test 3：Idle + 未到位 → 必须 Error（强语义）
TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotCompleteIfNotInPosition2)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    axis().applyFeedback({
        .state = AxisState::MovingAbsolute,
        .absPos = 0.5,
        .relPos = 0.5,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });
    orchestrator.update(axis()); // → WaitingMotionFinish

    // Idle 但未到位
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 0.3,
        .relPos = 0.3,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    // ❗不应该完成
    EXPECT_NE(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::Done);

    // ❗应该继续等待
    EXPECT_EQ(orchestrator.currentStep(),
              AutoAbsMoveOrchestrator::Step::WaitingMotionFinish);
}

// Test 4：未进入过运动 → 不允许完成
TEST_F(AutoAbsMoveOrchestratorTest, ShouldNotCompleteIfMotionNeverObserved)
{
    orchestrator.start(targetId, 1.0);

    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    // ⚠️ 直接 Idle + 到位（但没有 motionObserved）
    axis().applyFeedback({
        .state = AxisState::Idle,
        .absPos = 1.0,
        .relPos = 1.0,
        .relZeroAbsPos = 0.0,
        .posLimit = false,
        .negLimit = false,
        .posLimitValue = 1000.0,
        .negLimitValue = -1000.0
    });

    orchestrator.update(axis());

    EXPECT_FALSE(driver.has<EnableCommand>());
}
