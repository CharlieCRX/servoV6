// ============================================================================
// 业务模块：Jog 点动运动编排器 (JogOrchestrator) 单元测试
// 文件路径：tests/application/policy/test_jog_orchestrator.cpp
// 测试目标：验证点动运动全生命周期（启动→运行→停止→掉电）的状态机流转正确性
// ============================================================================
//
// 核心业务概念：
//   JogOrchestrator 是一个有限状态机，负责协调"点动"操作的完整流程：
//   Idle → EnsuringEnabled → IssuingJog → Jogging → IssuingStop → WaitingForIdle → EnsuringDisabled → Done
//   ⚠ 任意环节出错 → Error（终态）
//
// 测试策略：
//   1. 使用 RecordingAxisDriver 记录所有下发的驱动指令，验证指令序列的正确性
//   2. 通过 FakePLC 模拟不同的轴状态反馈，观察编排器的状态跃迁
//   3. 覆盖正向流程、异常分支、边界条件和防御性编程（防误杀）
//
// ============================================================================

#include <gtest/gtest.h>
#include "application/policy/JogOrchestrator.h"
#include "domain/entity/AxisId.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"
#include "application/axis/AxisRepository.h"

// ============================================================================
// 辅助测试桩：多轴版本指令记录驱动 (RecordingAxisDriver)
// ============================================================================
// 业务职责：
//   在测试环境中扮演"黑盒记录仪"，不真实驱动硬件，而是忠实记录所有收到的指令。
// 相比单轴版本的 FakeAxisDriver，本桩增强为多轴感知：
//   - 记录每条指令的 (轴ID, 指令内容) 二元组
//   - 提供按轴ID查询最后一条指令的能力 (lastForAxis)
//   - 提供按指令类型检视能力 (has<T>, last<T>)
// 设计意图：
//   测试用例关注的是"编排器发出去什么指令"，而非"驱动层怎么执行指令"。
//   因此本桩仅做录制，不做任何模拟执行。
//
class RecordingAxisDriver : public IAxisDriver {
public:
    // --------------------------------------------------------------------------
    // 驱动接口实现：将每次指令下发记录到历史队列
    // --------------------------------------------------------------------------
    void send(AxisId id, const AxisCommand& cmd) override {
        history.push_back({id, cmd});
    }

    // 指令记录的元组结构：(目标轴ID, 指令载荷)
    struct Record {
        AxisId id;
        AxisCommand cmd;
    };

    // 获取全部已下发指令的历史记录（按时间序）
    const std::vector<Record>& commands() const {
        return history;
    }

    // --------------------------------------------------------------------------
    // 指令检视查询 (通用版)
    // --------------------------------------------------------------------------

    // 是否存在某类指令？（例：是否有 EnableCommand 下发过）
    template<typename T>
    bool has() const {
        return std::any_of(history.begin(), history.end(),
            [](const Record& r) {
                return std::holds_alternative<T>(r.cmd);
            });
    }

    // 最后一次下发的某类指令是什么？（从后向前查）
    template<typename T>
    T last() const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (std::holds_alternative<T>(it->cmd)) {
                return std::get<T>(it->cmd);
            }
        }
        throw std::runtime_error("Command not found");
    }

    // --------------------------------------------------------------------------
    // 指令检视查询 (按轴ID过滤)
    // --------------------------------------------------------------------------

    // 针对指定轴的最后一次某类指令是什么？（多轴场景下精确匹配）
    template<typename T>
    T lastForAxis(AxisId expectedId) const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->id == expectedId && std::holds_alternative<T>(it->cmd)) {
                return std::get<T>(it->cmd);
            }
        }
        throw std::runtime_error("Command not found for axis");
    }

    // 历史记录是否为空？（没有任何指令下发的静默状态）
    bool isEmpty() const { return history.empty(); }

    std::vector<Record> history;
};

// ============================================================================
// 测试夹具：JogOrchestratorTest
// ============================================================================
// 业务职责：
//   为所有测试用例提供统一初始化环境，包含：
//   - FakePLC：模拟 PLC 反馈
//   - RecordingAxisDriver：指令记录驱动
//   - AxisRepository：轴仓库（默认注册 Y 轴）
//   - EnableUseCase / JogAxisUseCase：领域用例实例（通过仓库+驱动构造）
//   - JogOrchestrator：被测编排器
//   - targetId：被测轴默认指定为 Y 轴
//
// 辅助方法：
//   - runToJoggingState：快速推进状态机至"正在点动"阶段（跳过前置条件初始化）
//   - runToWaitingForIdleState：推进至"等待轴空闲"阶段（用于测试停止流程）
//   - runToEnsuringDisabledState：推进至"确保掉电"阶段（用于测试掉电流程）
//
class JogOrchestratorTest : public ::testing::Test {
protected:
    FakePLC plc;
    RecordingAxisDriver driver;
    AxisRepository repo;

    EnableUseCase enableUc{repo, driver};
    JogAxisUseCase jogUc{repo, driver};

    JogOrchestrator orchestrator{enableUc, jogUc};

    // 默认测试目标轴为 Y 轴
    // 测试用例可通过直接赋值 targetId 切换测试轴（虽然本文件内未跨轴测试）
    AxisId targetId = AxisId::Y;

    // --------------------------------------------------------------------------
    // 初始化：注册 Y 轴并设定初始状态为 Idle（空闲可操作）
    // --------------------------------------------------------------------------
    void SetUp() override {
        repo.registerAxis(targetId);
        Axis& axisRef = repo.getAxis(targetId);
        axisRef.applyFeedback({
            .state = AxisState::Idle,
            .absPos = 0.0, .relPos = 0.0, .relZeroAbsPos = 0.0,
            .posLimit = false, .negLimit = false,
            .posLimitValue = 1000.0, .negLimitValue = -1000.0
        });
    }

    // 快捷获取轴对象引用
    Axis& axis() { return repo.getAxis(targetId); }

    // --------------------------------------------------------------------------
    // 状态机快速推进工具方法
    // --------------------------------------------------------------------------

    // 业务场景：从 Idle 开始，完成"上使能→下发点动→进入点动运行"全流程
    // 用于测试"点动运行中"的各类业务场景（停止、异常跌落、方向验证等）
    // 前置条件：轴初始状态为 Idle
    // 后置条件：编排器状态为 Jogging，driver 历史被清空（便于后续精确计数）
    void runToJoggingState(Direction dir = Direction::Forward) {
        axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
        orchestrator.startJog(targetId, dir);
        orchestrator.update(axis());  // 第1次：EnsuringEnabled（Idle → IssuingJog）
        orchestrator.update(axis());  // 第2次：IssuingJog（下发JogCommand → Jogging）
        driver.history.clear();       // 清除历史，便于后续精确统计新下发指令
    }

    // 业务场景：从 Jogging 开始，执行停止指令并推进到"等待轴空闲"阶段
    // 用于测试"停止流程"中的各类业务场景（停止指令幂等性、方向防误杀等）
    // 前置条件：编排器处于 Jogging 状态
    // 后置条件：编排器状态为 WaitingForIdle，driver 历史被清空
    void runToWaitingForIdleState(Direction dir = Direction::Forward) {
        runToJoggingState(dir);                    // 先推进到 Jogging
        orchestrator.stopJog(targetId, dir);       // 使用目标轴ID发起停止请求
        orchestrator.update(axis());               // IssuingStop（下发stop）→ WaitingForIdle
        driver.history.clear();                    // 清除历史，便于后续精确统计
    }

    // 业务场景：从 WaitingForIdle 开始，模拟轴变为 Idle，推进到"确保掉电"阶段
    // 用于测试"掉电流程"中的各类业务场景（掉电指令下发、Done终态等）
    // 前置条件：编排器处于 WaitingForIdle 状态
    // 后置条件：编排器状态为 EnsuringDisabled（此时已下发掉电指令但轴还未掉电）
    void runToEnsuringDisabledState(Direction dir = Direction::Forward) {
        runToWaitingForIdleState(dir);
        axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
        orchestrator.update(axis());  // WaitingForIdle（收到Idle反馈）→ EnsuringDisabled（并下发disable指令）
        driver.history.clear();       // 清除历史，便于后续精确统计
    }
};

// ============================================================================
// ╔═══════════════════════════════════════════════════════════════════════════╗
// ║                   核心测试用例 — 状态机流转验证                            ║
// ╚═══════════════════════════════════════════════════════════════════════════╝
// ============================================================================

// ============================================================================
// [测试场景 1] 异常状态拦截：轴处于 Error 状态时，立即跳转到 Error 终态
// ============================================================================
// 业务规则：
//   安全性约束 —— 在任何时刻，只要轴反馈 Error 状态，编排器必须立即熔断，
//   不执行任何指令（这是最高优先级检查，在 update 方法入口处执行）。
// 验收标准：
//   1. 编排器状态 === Error
//   2. driver 历史队列为空（未下发任何指令）
//
TEST_F(JogOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Error);
    EXPECT_TRUE(driver.isEmpty());
}

// ============================================================================
// [测试场景 2] 使能流程：轴 Disabled 时下发上电指令并进入 EnsuringEnabled
// ============================================================================
// 业务规则：
//   点动操作的前提是轴已上电。如果轴处于 Disabled（未上电）状态，
//   编排器需要先发送 EnableCommand(true) 给驱动层，并在 EnsuringEnabled
//   阶段等待轴状态变为 Idle（表示上电完成）。
// 验收标准：
//   1. 下发了 EnableCommand（active=true）给目标轴
//   2. 编排器状态 === EnsuringEnabled
//
TEST_F(JogOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_TRUE(cmd.active);
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::EnsuringEnabled);
}

// ============================================================================
// [测试场景 3] 未知状态等待：轴 Unknown 时不下发指令，保持等待
// ============================================================================
// 业务规则：
//   如果轴处于 Unknown（未知/初始化未完成）状态，编排器不应贸然下发指令，
//   而是停留在 EnsuringEnabled 阶段静默等待。这属于防御性编程，防止在
//   系统尚未就绪时发出错误指令。
// 验收标准：
//   1. 编排器状态 === EnsuringEnabled（保持等待）
//   2. 未下发任何 EnableCommand
//
TEST_F(JogOrchestratorTest, ShouldStayInEnsuringEnabledWhenAxisIsUnknown)
{
    axis().applyFeedback({.state = AxisState::Unknown, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::EnsuringEnabled);
    EXPECT_FALSE(driver.has<EnableCommand>());
}

// ============================================================================
// [测试场景 4] 使能完成后转入 IssuingJog（但尚未下发点动指令）
// ============================================================================
// 业务规则：
//   当轴从 Disabled 上电成功（变为 Idle）后，编排器从 EnsuringEnabled
//   跳转到 IssuingJog。但点动指令的实质下发推迟到下一次 update 周期，
//   这是为了解耦"状态判断"与"指令下发"两个动作，便于在下次 update 中
//   结合最新状态做最终的决策（如检查限位等）。
// 验收标准：
//   1. 编排器状态 === IssuingJog
//   2. 未下发任何 JogCommand（点动指令尚未实际发出）
//
TEST_F(JogOrchestratorTest, ShouldTransitionToIssuingJogButNotSendJogCommandYet)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis());  // EnsuringEnabled: 下发EnableCommand

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());  // EnsuringEnabled（收到Idle）→ IssuingJog

    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::IssuingJog);
    EXPECT_FALSE(driver.has<JogCommand>());
}

// ============================================================================
// [测试场景 5] 下发点动指令并进入 Jogging（正向点动，基础路径）
// ============================================================================
// 业务规则：
//   JogOrchestrator 的主路径：轴处于 Idle 状态 → 启动正方向点动 →
//   下发 JogCommand(dir=Forward, active=true) → 进入 Jogging 运行状态。
//   这是一个端到端的基本成功场景。
// 验收标准：
//   1. 下发了 JogCommand，方向为 Forward，active=true
//   2. 编排器状态 === Jogging
//
TEST_F(JogOrchestratorTest, ShouldSendJogCommandAndEnterJogging)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis());  // EnsuringEnabled: Idle → IssuingJog
    orchestrator.update(axis());  // IssuingJog: 下发JogCommand → Jogging

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_EQ(cmd.dir, Direction::Forward);
    EXPECT_TRUE(cmd.active); 
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Jogging);
}

// ============================================================================
// [测试场景 6] 反向点动指令正确下发
// ============================================================================
// 业务规则：
//   点动支持正反两个方向。本用例验证反向点动时，JogCommand 的 dir 字段
//   被正确设置为 Direction::Backward。这是确保方向透传完整性的验证。
// 验收标准：
//   1. 下发了 JogCommand，方向为 Backward
//
TEST_F(JogOrchestratorTest, ShouldSendBackwardJogCommandCorrectly)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Backward);  // 指定反方向
    orchestrator.update(axis()); 
    orchestrator.update(axis()); 

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_EQ(cmd.dir, Direction::Backward); 
}

// ============================================================================
// [测试场景 7] 幂等性：JogCommand 只下发一次（防止重复下发）
// ============================================================================
// 业务规则：
//   进入 Jogging 状态后，即使后续 update 反复被调用，编排器也必须保证
//   JogCommand 只被下发一次。这是防止驱动层收到重复指令导致异常的重要
//   防御机制。
// 验收标准：
//   1. 历史记录中 JogCommand 的出现次数 === 1
//
TEST_F(JogOrchestratorTest, ShouldSendJogCommandOnlyOnce)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward);
    orchestrator.update(axis()); 
    orchestrator.update(axis());  // JogCommand 在此次 update 下发
    orchestrator.update(axis());  // 额外 update (不应再下发)
    orchestrator.update(axis());  // 额外 update (不应再下发)

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const RecordingAxisDriver::Record& r){
            return std::holds_alternative<JogCommand>(r.cmd);
        });
    EXPECT_EQ(count, 1);
}

// ============================================================================
// [测试场景 8] 点动被拒：轴上正限位触发 → 掉电熔断 → Error
// ============================================================================
// 业务规则：
//   如果轴已经在正限位 (posLimit=true) 上，仍然尝试正方向点动，领域层
//   会返回 RejectionReason::AtPositiveLimit 拒绝本次操作。编排器收到
//   拒绝后，执行安全熔断：下发 EnableCommand(false) 掉电，并进入 Error 终态。
// 验收标准：
//   1. 编排器状态 === Error
//   2. errorReason === AtPositiveLimit
//   3. 下发了 EnableCommand（active=false，即掉电指令）
//
TEST_F(JogOrchestratorTest, ShouldDisableAndEnterErrorWhenJogRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = true, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.startJog(targetId, Direction::Forward); 
    orchestrator.update(axis()); 
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Error);
    EXPECT_EQ(orchestrator.errorReason(), RejectionReason::AtPositiveLimit);
    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.lastForAxis<EnableCommand>(targetId).active); 
}

// ============================================================================
// [测试场景 9] 停止流程触发：合法停止请求导致状态跃迁至 IssuingStop
// ============================================================================
// 业务规则：
//   在 Jogging 状态下，接收到与当前方向匹配的 stopJog 请求，编排器应
//   立即从 Jogging 跳转到 IssuingStop，准备下发停止指令。
// 验收标准：
//   1. 编排器状态 === IssuingStop
//
TEST_F(JogOrchestratorTest, ShouldTransitionToIssuingStopOnValidStopJog)
{
    runToJoggingState(Direction::Forward);
    orchestrator.stopJog(targetId, Direction::Forward);
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::IssuingStop);
}

// ============================================================================
// [测试场景 10] 方向防误杀：停止方向与运行方向不匹配时忽略
// ============================================================================
// 业务规则：
//   多方向点动场景中，如果 UI 误传了与当前运动方向相反的停止指令，
//   编排器应安全忽略，不中断正在进行的点动。
//   例如：轴正在 Forward 点动，收到 Backward 停止指令 → 忽略。
// 验收标准：
//   1. 编排器状态保持不变（仍为 Jogging）
//
TEST_F(JogOrchestratorTest, ShouldIgnoreStopJogWithWrongDirection)
{
    runToJoggingState(Direction::Forward);
    orchestrator.stopJog(targetId, Direction::Backward); 
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Jogging);
}

// ============================================================================
// [测试场景 11] 轴ID防误杀：停止指令的 AxisId 不匹配时忽略
// ============================================================================
// ⭐ 关键安全特性：多轴环境下的终极防误杀机制
//
// 业务规则：
//   在多轴系统中，每个轴各自持有一个 JogOrchestrator 实例。如果某个
//   其他轴的 UI 组件意外发送了停止指令（例如 Z 轴停止误发送到 Y 轴的
//   编排器），编排器必须根据 AxisId 匹配严格过滤：
//     - 停止请求中的 AxisId 必须与本编排器管理的目标轴一致
//     - 否则安全忽略，当前运行轴不被干扰
// 业务场景示例：
//   用户界面存在并行正向点动 Y 和反向点动 Z 的场景，UI 层误将
//   Z 轴的停止指令路由到了 Y 轴的 Orchestrator → 安全过滤，Y 轴继续运行。
// 验收标准：
//   1. 使用错误的 AxisId（如 Z）调用 stopJog → 编排器忽略
//   2. 编排器状态保持不变（仍为 Jogging）
//
TEST_F(JogOrchestratorTest, ShouldIgnoreStopJogWithWrongAxisId)
{
    runToJoggingState(Direction::Forward);
    
    // ⭐ 模拟其他轴的 UI 组件意外发出了停止指令（例如目标为 Z 轴，但发到了 Y 轴的编排器）
    orchestrator.stopJog(AxisId::Z, Direction::Forward); 
    
    // ⭐ 断言：由于目标不匹配，当前正在运行的 Y 轴绝不能被逼停
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Jogging);
}

// ============================================================================
// [测试场景 12] 异常跌落监测：点动中轴突然变为 Idle → 自动切入 IssuingStop
// ============================================================================
// 业务规则：
//   当轴在 Jogging 状态下意外跌回 Idle（例如驱动层保护性停机、硬件故障等），
//   编排器应能监测到此异常，并主动接管进入停止收尾流程（IssuingStop），
//   确保后续掉电流程能正常执行。
// 验收标准：
//   1. 编排器状态 === IssuingStop（主动夺权，进入停止流程）
//
TEST_F(JogOrchestratorTest, ShouldAutoTransitionToIssuingStopIfAxisDropsToIdle)
{
    runToJoggingState(Direction::Forward);
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = true, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::IssuingStop);
}

// ============================================================================
// [测试场景 13] 停止指令下发：IssuingStop → WaitingForIdle 跃迁
// ============================================================================
// 业务规则：
//   进入 IssuingStop 后，编排器下发 JogCommand(active=false) 停止点动，
//   并立即跃迁到 WaitingForIdle 阶段，等待底层物理轴完全停稳。
// 验收标准：
//   1. 下发了 JogCommand，active=false（即停止指令）
//   2. 编排器状态 === WaitingForIdle
//
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandAndTransitionToWaitingForIdle)
{
    runToJoggingState(Direction::Forward);
    orchestrator.stopJog(targetId, Direction::Forward);
    driver.history.clear();  // 清空前序 JogCommand 记录
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_FALSE(cmd.active);               // 停止指令（active=false）
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::WaitingForIdle);
}

// ============================================================================
// [测试场景 14] 停止指令幂等性：只下发一次 stop
// ============================================================================
// 业务规则：
//   与 JogCommand 相同，停止指令也必须保证幂等性。即使 WaitingForIdle
//   阶段被多次 update 驱动，也不应重复下发停止指令。
// 验收标准：
//   1. JogCommand(active=false) 在历史记录中仅出现一次
//
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandOnlyOnce)
{
    // 使用反方向点动测试，确保方向参数不影响幂等性检查
    runToJoggingState(Direction::Backward); 
    orchestrator.stopJog(targetId, Direction::Backward);
    driver.history.clear();
    orchestrator.update(axis());  // 第1次：下发 stop
    orchestrator.update(axis());  // 第2次：不应再次下发
    orchestrator.update(axis());  // 第3次：不应再次下发

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const RecordingAxisDriver::Record& r){
            if (auto* jogCmd = std::get_if<JogCommand>(&r.cmd)) {
                return jogCmd->active == false;  // 统计停止指令
            }
            return false;
        });
    EXPECT_EQ(count, 1);
}

// ============================================================================
// [测试场景 15] 等待空闲保持：轴尚未 Idle 时阻塞等待
// ============================================================================
// 业务规则：
//   下发停止指令后，编排器进入 WaitingForIdle 阶段，在此阶段编历器
//   不做任何操作（不主动下发指令），只是被动等待轴状态变为 Idle。
//   这是"让硬件自己停稳"的设计哲学。
// 验收标准：
//   1. 编排器状态 === WaitingForIdle（保持等待）
//   2. driver 历史队列为空（未下发新指令）
//
TEST_F(JogOrchestratorTest, ShouldStayInWaitingForIdleIfAxisIsNotYetIdle)
{
    runToWaitingForIdleState(Direction::Forward);
    // 模拟轴仍在 Jogging（尚未停稳）
    axis().applyFeedback({.state = AxisState::Jogging, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::WaitingForIdle);
    EXPECT_TRUE(driver.history.empty());  // 未下发任何新指令
}

// ============================================================================
// [测试场景 16] 轴停稳跃迁：WaitingForIdle → EnsuringDisabled
// ============================================================================
// 业务规则：
//   当轴反馈 Idle 状态（物理停稳），编排器从 WaitingForIdle 跃迁到
//   EnsuringDisabled，准备执行最后一步：掉电操作。
// 验收标准：
//   1. 编排器状态 === EnsuringDisabled
//
TEST_F(JogOrchestratorTest, ShouldTransitionToEnsuringDisabledWhenAxisBecomesIdle)
{
    runToWaitingForIdleState(Direction::Forward);
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::EnsuringDisabled);
}

// ============================================================================
// [测试场景 17] 掉电指令下发：EnsuringDisabled 阶段
// ============================================================================
// 业务规则：
//   进入 EnsuringDisabled 后，编排器下发 EnableCommand(false) 给驱动层，
//   通知硬件执行掉电操作。然后保持等待，直至轴状态变为 Disabled。
// 验收标准：
//   1. 下发了 EnableCommand，active=false（掉电指令）
//
TEST_F(JogOrchestratorTest, ShouldIssueDisableCommandAndWait)
{
    runToWaitingForIdleState(Direction::Forward);
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());  // WaitingForIdle → EnsuringDisabled（并下发掉电指令）
    orchestrator.update(axis());

    ASSERT_TRUE(driver.has<EnableCommand>());
    EXPECT_FALSE(driver.lastForAxis<EnableCommand>(targetId).active);
}

// ============================================================================
// [测试场景 18] 掉电完成跃迁：EnsuringDisabled → Done
// ============================================================================
// 业务规则：
//   当轴反馈 Disabled 状态（硬件已完成掉电），编排器从 EnsuringDisabled
//   跃迁到 Done 终态，表示本次点动操作安全、完整地结束。
// 验收标准：
//   1. 编排器状态 === Done
//
TEST_F(JogOrchestratorTest, ShouldTransitionToDoneWhenAxisIsDisabled)
{
    runToEnsuringDisabledState();
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Done);
}

// ============================================================================
// [测试场景 19] Done 终态沉默：不再下发任何指令
// ============================================================================
// 业务规则：
//   编排器进入 Done 终态后，对后续所有 update 调用均不做响应，
//   保持沉默直到下一次 startJog 重置状态机。这是防止"幽灵指令"
//   在操作结束后仍然被发出的关键安全机制。
// 验收标准：
//   1. 编排器状态 === Done（保持不变）
//   2. driver 历史队列为空（不再发出新指令）
//
TEST_F(JogOrchestratorTest, ShouldRemainSilentWhenInDoneState)
{
    runToEnsuringDisabledState();
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());  // EnsuringDisabled → Done
    driver.history.clear();       // 清空此前的掉电指令

    // 后续多次 update，Done 状态不应再下发任何指令
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator.update(axis());
    orchestrator.update(axis());

    EXPECT_EQ(orchestrator.currentStep(), JogOrchestrator::Step::Done);
    EXPECT_TRUE(driver.history.empty());
}
