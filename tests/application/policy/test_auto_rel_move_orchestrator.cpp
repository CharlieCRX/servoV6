/**
 * @file test_auto_rel_move_orchestrator.cpp
 * @brief AutoRelMoveOrchestrator 的单元测试套件
 *
 * 本文件测试 AutoRelMoveOrchestrator——相对运动编排器的完整状态机行为。
 * 编排器负责协调多轴场景下的 "Enable → IssueMove → WaitMotionStart → WaitMotionFinish → Done" 
 * 全生命周期，覆盖正常流程、异常恢复、幂等性保障等关键路径。
 *
 * 测试架构：
 *   - RecordingAxisDriver：多轴版本的录制备份驱动，记录每条发送的指令，支持按轴查询
 *   - AutoRelMoveOrchestratorTest：集成 AxisRepository 的测试夹具，提供便捷的轴状态注入
 *
 * 状态机流转示意：
 *   Initial → EnsuringEnabled → IssuingMove → WaitingMotionStart → WaitingMotionFinish → Done
 *               ↓                    ↓              ↓                    ↓
 *             Error ←────────── Error ←───────── Error ←───────────── Error
 */

#include <gtest/gtest.h>
#include "application/policy/AutoRelMoveOrchestrator.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// =====================================================================
// RecordingAxisDriver：多轴录制备份驱动
// =====================================================================
// 该适配器实现 IAxisDriver 接口，记录所有发送至任意轴的指令历史。
// 提供模板化的查询方法（has<T>()、last<T>()、lastForAxis<T>()），
// 使测试代码能精确断言特定类型的指令是否按预期产生。
// =====================================================================

class RecordingAxisDriver : public IAxisDriver {
public:
    void send(AxisId id, const AxisCommand& cmd) override {
        history.push_back({id, cmd});
    }

    struct Record {
        AxisId id;          ///< 目标轴标识
        AxisCommand cmd;    ///< 发出的指令
    };

    const std::vector<Record>& commands() const {
        return history;
    }

    /**
     * @brief 检查历史中是否存在指定类型的指令
     * @tparam T 指令类型（如 EnableCommand、MoveCommand）
     * @return true 如果历史中存在至少一条该类型指令
     */
    template<typename T>
    bool has() const {
        return std::any_of(history.begin(), history.end(),
            [](const Record& r) {
                return std::holds_alternative<T>(r.cmd);
            });
    }

    /**
     * @brief 获取历史中最后一条指定类型的指令
     * @tparam T 指令类型
     * @return 匹配的指令值
     * @throws std::runtime_error 如果未找到
     */
    template<typename T>
    T last() const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (std::holds_alternative<T>(it->cmd)) {
                return std::get<T>(it->cmd);
            }
        }
        throw std::runtime_error("Command not found");
    }

    /**
     * @brief 获取历史中某轴最后一条指定类型的指令
     * @tparam T 指令类型
     * @param expectedId 要查询的目标轴
     * @return 匹配的指令值
     * @throws std::runtime_error 如果未找到该轴的该类型指令
     *
     * 多轴场景下，不同轴可能同时发送不同类型指令。
     * 此方法可精确检索指定轴的最后一条相关指令，用于断言验证。
     */
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

// =====================================================================
// AutoRelMoveOrchestratorTest：测试夹具
// =====================================================================
// 集成 AxisRepository 管理轴状态，通过 plc + RecordingAxisDriver 模拟底层硬件。
// 每个 TestCase 在 SetUp 中预注册目标轴为 Idle 状态，并设置合理的限位范围。
//
// 测试策略：
//   - 所有测试用例操作同一目标轴 AxisId::Y
//   - 通过 axis() 快捷方法获取轴的 mutable 引用，方便注入反馈状态
//   - orchestrator 被注入 enableUc / moveUc，模拟轴使能和相对运动用例
// =====================================================================

class AutoRelMoveOrchestratorTest : public ::testing::Test {
protected:
    FakePLC plc;
    RecordingAxisDriver driver;
    AxisRepository repo;

    MoveRelativeUseCase moveUc{repo, driver};
    EnableUseCase enableUc{repo, driver};

    AutoRelMoveOrchestrator orchestrator{enableUc, moveUc};

    AxisId targetId = AxisId::Y;

    void SetUp() override {
        // 预注册目标轴，否则 MoveRelativeUseCase 验证时会拒绝
        repo.registerAxis(targetId);
        Axis& axis = repo.getAxis(targetId);
        // 初始状态：Idle + 位置归零 + 限位范围 [-1000, 1000]
        axis.applyFeedback({
            .state = AxisState::Idle,
            .absPos = 0.0, .relPos = 0.0, .relZeroAbsPos = 0.0,
            .posLimit = false, .negLimit = false,
            .posLimitValue = 1000.0, .negLimitValue = -1000.0
        });
    }

    /// 快捷获取目标轴的引用
    Axis& axis() { return repo.getAxis(targetId); }
};

// =====================================================================
// EnsuringEnabled 阶段测试
// =====================================================================
// 编排器启动后首先进入 EnsuringEnabled 阶段。在此阶段：
//   - 如果轴处于 Error 状态 → 直接进入 Error
//   - 如果轴处于 Disabled 状态 → 发送 EnableCommand(true)
//   - 如果轴处于 Idle 状态 → 跳至 IssuingMove
//   - 如果轴处于其他状态 → 保持等待
// 关键约束：在此阶段不得发送 Move 指令（幂等入口保护）
// =====================================================================

/**
 * @test 轴处于 Error 状态时，编排器应直接进入 Error 阶段
 *
 * 场景：启动编排器前轴已处于异常状态
 * 验证：currentStep() == Error，且不发送任何指令
 *
 * 这是最高优先级的全局错误拦截逻辑的测试：
 * 即使 orchestator.start() 已启动，update() 时轴状态为 Error
 * 也应立即收敛到 Error 而非继续执行后续流程
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0); 
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Error);
    EXPECT_TRUE(driver.isEmpty());
}

/**
 * @test 轴处于 Disabled 状态时，编排器应发送 Enable 指令
 *
 * 场景：轴断电状态下启动相对运动
 * 验证：
 *   - 发出 EnableCommand(true)
 *   - 阶段保持在 EnsuringEnabled（等待轴变为 Idle）
 *
 * 这是正常启动流程的入口测试：Disabled → 主动使能
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.last<EnableCommand>();
    EXPECT_TRUE(cmd.active);
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::EnsuringEnabled);
}

/**
 * @test 轴处于非 Idle/Disabled 状态时，编排器应保持 EnsuringEnabled 等待
 *
 * 场景：轴正在执行其他运动（如绝对运动）时收到相对运动请求
 * 验证：不发送 Move 指令，保持在 EnsuringEnabled 阶段
 *
 * 约束：编排器不应干扰轴正在进行的其他运动，
 * 必须等待轴自然回到 Idle 后再接管
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayInEnsuringEnabledWhenAxisNotIdle)
{
    axis().applyFeedback({.state = AxisState::MovingAbsolute, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::EnsuringEnabled);
    EXPECT_FALSE(driver.has<MoveCommand>());
}

/**
 * @test 在 EnsuringEnabled → Idle 过渡期间不得发送 Move 指令
 *
 * 场景：轴从 Disabled 开始使能，持续多帧仍未变为 Idle
 * 验证：确保整个等待过程中不提前发出 Move 指令
 *
 * 此测试验证了一个重要的时序约束：
 * 必须确保轴完全进入 Idle 后（即下一帧确认 Idle 状态）
 * 才能过渡到 IssuingMove 阶段。确保 Enable 与 Move 之间
 * 有明确的状态确认间隔，防止指令竞争。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotSendMoveDuringEnableToIdleTransition)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // Enable

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.has<MoveCommand>());

    // 假设下一帧还没 Idle，验证不会提前发出 Move
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_FALSE(driver.has<MoveCommand>());
}

// =====================================================================
// IssuingMove 阶段测试
// =====================================================================
// 当轴进入 Idle 状态后，编排器过渡到 IssuingMove 阶段。
// 关键约束：
//   1. 首帧不发 Move —— 先切换阶段，下帧再发（两阶段分离设计）
//   2. 第二帧发起 MoveRelative 指令
//   3. 记录起点位置（startAbs）供后续完成判定
//   4. 幂等性：Move 指令最多只发一次
//   5. 拒绝处理：Move 被领域层拒绝时 → 掉电 + Error
//   6. Move 发出后 → 过渡到 WaitingMotionStart
// =====================================================================

/**
 * @test 进入 IssuingMove 的首帧不应发送 Move 指令
 *
 * 场景：轴已 Idle，编排器从 EnsuringEnabled 过渡到 IssuingMove 的瞬间
 * 验证：driver 没有 Move 指令记录
 *
 * 设计原因：编排器采用"先切阶段、再发指令"的两帧策略，
 * 确保状态切换和指令发送之间有一个 tick 的观察窗口，
 * 避免在同一个 update() 内既切换状态又发送指令导致的逻辑耦合
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotSendMoveOnFirstTickOfIssuingMove)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // → IssuingMove

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::IssuingMove);
    EXPECT_FALSE(driver.has<MoveCommand>());
}

/**
 * @test IssuingMove 的第二帧应发送 MoveRelative 指令
 *
 * 场景：首帧已过渡到 IssuingMove，次帧 update
 * 验证：
 *   - 发出 MoveCommand 类型为 Relative
 *   - 目标距离为 1.0
 *   - 指令发送到正确的目标轴
 *
 * 这是正常流程中的关键一跳：Idle → Move 指令发出
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveRelativeOnSecondTick)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → 发 Move

    ASSERT_TRUE(driver.has<MoveCommand>());
    auto cmd = driver.lastForAxis<MoveCommand>(targetId);
    EXPECT_EQ(cmd.type, MoveType::Relative);
    EXPECT_DOUBLE_EQ(cmd.target, 1.0);
}

/**
 * @test 应正确捕获 Move 指令发出时的起点位置
 *
 * 场景：轴当前位置为 5.0 时发起 2.0 的相对运动
 * 验证：MoveCommand 的 startAbs 字段应记录为 5.0
 *
 * 起点位置用于 WaitingMotionFinish 阶段验证：
 * 最终位置 = startAbs + distance 才被视为真正的运动完成
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldCaptureCorrectStartPosition)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 5.0, .relPos = 5.0, .relZeroAbsPos = 0.0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 2.0);
    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move

    auto cmd = driver.lastForAxis<MoveCommand>(targetId);
    EXPECT_DOUBLE_EQ(cmd.startAbs, 5.0);
}

/**
 * @test Move 指令最多只发送一次（幂等性）
 *
 * 场景：Move 发出后持续调用 update() 多帧
 * 验证：driver 历史中只记录了一条 MoveCommand
 *
 * 幂等约束：防止因持续的 update() 导致重复发送 Move 指令，
 * 从而引发轴控层指令堆积或异常
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldSendMoveOnlyOnce)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move
    orchestrator.update(axis()); // 再 update（不应产生第二条 Move）

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const RecordingAxisDriver::Record& r){
        return std::holds_alternative<MoveCommand>(r.cmd);
    });
    EXPECT_EQ(count, 1);
}

/**
 * @test Move 被领域层拒绝时，应掉电并进入 Error 状态
 *
 * 场景：目标距离 100.0 超出了限位 [+10.0, -10.0]
 * 验证：
 *   - 发出 EnableCommand(false) 断电
 *   - 阶段进入 Error
 *
 * 这是安全机制的关键测试：当 MoveRelativeUseCase 返回非 None 的
 * RejectionReason（如超限拒绝）时，编排器不应继续等待运动，
 * 而应立即断电并宣告失败。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableAndEnterErrorWhenMoveRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 10.0, .negLimitValue = -10.0});
    orchestrator.start(targetId, 100.0); // 超限
    orchestrator.update(axis()); // → IssuingMove
    orchestrator.update(axis()); // → Move失败

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_FALSE(cmd.active);   // Disable（掉电）
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Error);
}

/**
 * @test Move 发出后应进入 WaitingMotionStart 阶段
 *
 * 场景：正常流程 Move 指令发出后
 * 验证：currentStep() == WaitingMotionStart
 *
 * 这是正常流程的正向验证：证明编排器按预期完成了
 * IssuingMove → WaitingMotionStart 的状态过渡
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterWaitingMotionStartAfterMove)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}

/**
 * @test Move 被拒绝时，即使轴状态仍为 Idle 也应进入 Error
 *
 * 场景：超限拒绝后，轴的状态反馈仍然是 Idle（未被修改）
 * 验证：编排器不依赖轴状态变化来做错误判定，而是基于 UseCase 的返回值
 *
 * 这是对编排器错误处理独立性的验证：
 * 错误判定不应依赖于轴的反馈状态，因为可能存在反馈延迟、
 * 通信中断等情景。编排器应基于 UseCase 的执行结果（同步）做决策。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorEvenIfAxisStateIsStillIdleWhenMoveRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 10.0, .negLimitValue = -10.0});
    orchestrator.start(targetId, 100.0); 
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    EXPECT_EQ(axis().state(), AxisState::Idle);
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Error);
}

// =====================================================================
// WaitingMotionStart 阶段测试
// =====================================================================
// 指令发出后，编排器进入 WaitingMotionStart，等待轴实际开始运动。
// 运动起动的判定条件（任一满足即可）：
//   1. 轴状态变为 MovingRelative
//   2. 当前位置相对于起始位置发生了超过 epsilon 的位移
//   3. 轴报告运动完成（isMoveCompleted() 为 true）
//
// 关键约束：
//   - 运动观测具有"锁存"（latch）行为：一旦观测到运动，即使后续
//     位置回退也保持在 WaitingMotionFinish
//   - 如果在 WaitingMotionStart 期间轴进入 Error 状态 → 全局拦截
// =====================================================================

/**
 * @test 无运动发生时保持 WaitingMotionStart
 *
 * 场景：Move 发出后，轴状态仍为 Idle，且位置未发生变化
 * 验证：阶段保持 WaitingMotionStart，不推进
 *
 * 模拟轴控层延迟或尚未响应的情景：
 * 编排器应耐心等待，不应在没有运动证据时草率推进
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayIfNoMotionObserved)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 
    orchestrator.update(axis()); // 没有发生任何位移

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionStart);
}

/**
 * @test 轴状态变为 MovingRelative 时检测到运动
 *
 * 场景：Move 发出后，轴反馈状态变为 MovingRelative
 * 验证：阶段推进到 WaitingMotionFinish
 *
 * 这是主流的运动检测方式：轴控层通过状态枚举通知上位机轴正在运动
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDetectMotionWhenStateIsMovingRelative)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.1, .relPos = 0.1, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test 通过位置变化检测运动
 *
 * 场景：轴状态未变更为 MovingRelative，但实际位置已发生变化
 * 验证：阶段推进到 WaitingMotionFinish
 *
 * 备选检测路径：即使状态枚举没有更新（可能由于驱动层实现差异），
 * 只要位置发生超过 epsilon 的变化，也应认为运动已经开始。
 * 这增强了编排器对不同驱动实现的兼容性。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDetectMotionByPositionDelta)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 5.0, .relPos = 5.0, .relZeroAbsPos = 0.0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 0.5);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    // 直接跳位置（无 MovingRelative 中间状态）
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 5.5, .relPos = 5.5, .relZeroAbsPos = 0.0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test WaitingMotionStart 期间轴进入 Error 时应进入 Error 阶段
 *
 * 场景：等待运动开始的过程中，轴报告 Error 状态
 * 验证：阶段进入 Error
 *
 * 全局错误拦截的另一个覆盖路径：
 * 在任何阶段，只要检测到 Error 状态，编排器都应立即停止
 * 并进入 Error，而不是继续等待运动
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldEnterErrorWhenAxisReportsError)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Error);
}

/**
 * @test 运动观测具有锁存特性
 *
 * 场景：
 *   - 观测到运动后进入 WaitingMotionFinish
 *   - 后续位置回退到接近起点
 * 验证：阶段保持在 WaitingMotionFinish 不会回退
 *
 * 锁存机制防止了由于位置抖动导致的阶段反复横跳。
 * 一旦判定运动开始，编排器就信任轴已经开始运动，
 * 不再重新检测，这对控制系统的稳定性至关重要。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldLatchMotionObserved)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    // 观测到运动（位置变化）
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.2, .relPos = 0.2, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);

    // 位置回退，但不应回到 WaitingMotionStart
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.01, .relPos = 0.01, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

// =====================================================================
// WaitingMotionFinish 阶段测试
// =====================================================================
// 运动中的最终阶段：等待运动完成。
// 完成判定三角验证：
//   1. 轴状态停用（isMoveCompleted() 返回 true）
//   2. 当前位置 ≈ 起点 + 目标距离（物理级最终验证）
// 满足以上两个条件 → Done + 掉电（发送 EnableCommand(false)）
// 如果物理位置与目标偏差过大 → Error（可能被外力打断）
//
// 安全机制：
//   - 防假完成：如果从未观测到运动（m_motionObserved == false），
//     即使 isMoveCompleted() 为 true 也忽略
//   - 掉电只发一次（幂等）
// =====================================================================

/**
 * @test 运动完成时应掉电并进入 Done
 *
 * 场景：
 *   1. 起始位置 0.0，目标距离 1.0
 *   2. 轴 moving → 位置 0.5
 *   3. 轴 idle → 位置 1.0（完成）
 * 验证：
 *   - 发出 EnableCommand(false) 掉电
 *   - 阶段进入 Done
 *
 * 这是最标准的"Happy Path"完成流程测试。
 * 注意：掉电（disable）是完成流程的安全收尾动作，
 * 确保运动完成后轴回到安全的断电状态。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableAndEnterDoneWhenMoveCompleted)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    // 运动开始
    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis()); 

    // 运动完成（位置正好到达目标）
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 1.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_FALSE(cmd.active);
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done);
}

/**
 * @test 运动未完成时应保持 WaitingMotionFinish
 *
 * 场景：位置已变化但未到达目标
 *   起点 0.0 → 已运动到 0.8（目标为 1.0），轴停用
 * 验证：阶段保持在 WaitingMotionFinish
 *
 * 验证了编排器在"轴已停但位置未到位"场景下的耐心等待行为。
 * 此时编排器不会提前判定完成，而是继续等待下一帧。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldStayIfMoveNotCompleted)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    // 运动开始
    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis()); 

    // 轴停但位置 0.8 ≠ 目标 1.0
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0.8, .relPos = 0.8, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::WaitingMotionFinish);
}

/**
 * @test 从未观测到运动时，即使轴 Idle 且位置到达目标也不应完成
 *
 * 场景：
 *   1. 发出 Move（IssuingMove 阶段）
 *   2. 下一帧轴直接从 Idle 跳到了目标位置（未经过 MovingRelative 也未产生位移变化）
 *   3. 轴 isMoveCompleted() = true
 * 验证：阶段不会进入 Done
 *
 * 防假完成机制验证：
 * 如果从未观测到"运动开始"的证据，即使轴看起来已完成运动，
 * 编排器也不应判定为 Done。这防止了"开局即巅峰"的假完成场景，
 * 如轴从未真正启动过（指令未送达、驱动未响应等）。
 *
 * 注意：在实际应用中，此类场景需要超时机制兜底，
 * 但在本单元测试中仅验证编排器的状态机行为。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldNotCompleteIfMotionNeverObserved)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    // 未经过 WaitingMotionStart 的运动观测，直接跳位置
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 1.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_NE(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done);
}

/**
 * @test 完成时的掉电指令只发一次（幂等）
 *
 * 场景：运动完成后持续调用 update()
 * 验证：历史中只有一条有效的掉电指令
 *
 * 与 IssuingMove 阶段的幂等性类似：
 * 完成阶段的掉电也应保证只执行一次，
 * 防止重复发送 EnableCommand(false) 引发驱动层问题。
 * 此处通过 count 统计 active = false 的 EnableCommand 数量。
 */
TEST_F(AutoRelMoveOrchestratorTest, ShouldDisableOnlyOnce)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.start(targetId, 1.0);
    orchestrator.update(axis());
    orchestrator.update(axis());

    // 运动开始
    axis().applyFeedback({.state = AxisState::MovingRelative, .absPos = 0.5, .relPos = 0.5, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    // 运动完成
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 1.0, .relPos = 1.0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    // 多余的一帧 update，验证不会产生第二个掉电
    orchestrator.update(axis());

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const RecordingAxisDriver::Record& r){
        return std::holds_alternative<EnableCommand>(r.cmd) && !std::get<EnableCommand>(r.cmd).active;
    });

    EXPECT_EQ(count, 1);
}
