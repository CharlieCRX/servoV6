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
// 架构变更（组管理重构）：
//   编排器不再直接持有 AxisRepository，改为通过 SystemManager → SystemContext 获取轴。
//   EnableUseCase 已改为无状态（execute 时传入 manager + groupName + axisId）。
//   指令下发通过 group->driver()->send() 完成。
//
// 测试策略：
//   1. 使用 SystemManager + FakeAxisDriver 构建完整分组环境
//   2. 通过 FakePLC 模拟不同的轴状态反馈，观察编排器的状态跃迁
//   3. 覆盖正向流程、异常分支、边界条件和防御性编程（防误杀）
//   4. 新增 SystemManager 层错误路径（分组不存在）
// ============================================================================

#include <gtest/gtest.h>
#include "application/policy/JogOrchestrator.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/SystemManager.h"
#include "infrastructure/FakeAxisDriver.h"
#include "infrastructure/FakePLC.h"

// ============================================================================
// 测试夹具：JogOrchestratorTest
// ============================================================================
// 业务职责：
//   为所有测试用例提供统一初始化环境，包含：
//   - FakePLC：模拟 PLC 反馈
//   - FakeAxisDriver：指令记录驱动（由 FakePLC 构造）
//   - SystemManager：系统管理器（注册分组 "TestGroup" 并挂载驱动）
//   - JogOrchestrator：被测编排器（绑定 manager + "TestGroup"）
//   - targetId：被测轴默认指定为 Y 轴
//
// 辅助方法：
//   - runToJoggingState：快速推进状态机至"正在点动"阶段
//   - runToWaitingForIdleState：推进至"等待轴空闲"阶段
//   - runToEnsuringDisabledState：推进至"确保掉电"阶段
//
class JogOrchestratorTest : public ::testing::Test {
protected:
    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;

    std::unique_ptr<JogOrchestrator> orchestrator;

    const std::string groupName = "TestGroup";
    AxisId targetId = AxisId::Y;

    // --------------------------------------------------------------------------
    // 初始化：创建分组，注册 Y 轴，设定初始状态为 Idle
    // --------------------------------------------------------------------------
    void SetUp() override {
        ContextRejection reason;
        manager.createGroup(groupName, reason);

        SystemContext* group = nullptr;
        ContextRejection mgrReason;
        manager.tryGetGroup(groupName, group, mgrReason);
        group->setDriver(&driver);
        group->emergencyStopController().applyFeedback(false); // 默认不急停

        Axis* axisPtr = nullptr;
        ContextRejection ctxReason;
        ASSERT_TRUE(group->tryGetAxis(targetId, axisPtr, ctxReason));
        Axis& axis = *axisPtr;
        axis.applyFeedback({
            .state = AxisState::Idle,
            .absPos = 0.0, .relPos = 0.0, .relZeroAbsPos = 0.0,
            .posLimit = false, .negLimit = false,
            .posLimitValue = 1000.0, .negLimitValue = -1000.0
        });

        orchestrator = std::make_unique<JogOrchestrator>(manager, groupName);
    }

    // 快捷获取轴对象引用
    Axis& axis() {
        SystemContext* group = nullptr;
        ContextRejection reason;
        manager.tryGetGroup(groupName, group, reason);
        Axis* axisPtr = nullptr;
        ContextRejection ctxReason;
        group->tryGetAxis(targetId, axisPtr, ctxReason);
        return *axisPtr;
    }

    // --------------------------------------------------------------------------
    // 状态机快速推进工具方法
    // --------------------------------------------------------------------------

    // 业务场景：从 Idle 开始，完成"上使能→下发点动→进入点动运行"全流程
    // 用于测试"点动运行中"的各类业务场景（停止、异常跌落、方向验证等）
    // 前置条件：轴已 Idle
    // 后置条件：编排器状态为 Jogging，driver 历史被清空（便于后续精确计数）
    void runToJoggingState(Direction dir = Direction::Forward) {
        axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
        orchestrator->startJog(targetId, dir);
        orchestrator->tick();  // EnsuringEnabled（Idle → IssuingJog）
        orchestrator->tick();  // IssuingJog（下发 JogCommand → Jogging）
        driver.history.clear();
    }

    // 业务场景：从 Jogging 开始，执行停止指令并推进到"等待轴空闲"阶段
    // 前置条件：编排器处于 Jogging 状态
    // 后置条件：编排器状态为 WaitingForIdle，driver 历史被清空
    void runToWaitingForIdleState(Direction dir = Direction::Forward) {
        runToJoggingState(dir);
        orchestrator->stopJog(targetId, dir);
        orchestrator->tick();  // IssuingStop（下发stop）→ WaitingForIdle
        driver.history.clear();
    }

    // 业务场景：从 WaitingForIdle 开始，模拟轴变为 Idle，推进到"确保掉电"阶段
    // 前置条件：编排器处于 WaitingForIdle 状态
    // 后置条件：编排器状态为 EnsuringDisabled（此时已下发掉电指令但轴还未掉电）
    void runToEnsuringDisabledState(Direction dir = Direction::Forward) {
        runToWaitingForIdleState(dir);
        axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
        orchestrator->tick();  // WaitingForIdle → EnsuringDisabled（并下发disable指令）
        driver.history.clear();
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
//   不执行任何指令（这是最高优先级检查，在 tick 方法入口处执行）。
// 验收标准：
//   1. 编排器状态 === Error
//   2. driver 历史队列为空（未下发任何指令）
//
TEST_F(JogOrchestratorTest, ShouldEnterErrorWhenAxisIsInErrorState)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Error);
    EXPECT_TRUE(driver.history.empty());
}

// ============================================================================
// [测试场景 2] 使能流程：轴 Disabled 时下发上电指令并进入 EnsuringEnabled
// ============================================================================
// 业务规则：
//   点动操作的前提是轴已上电。如果轴处于 Disabled（未上电）状态，
//   编排器通过 EnableUseCase（无状态）下发给驱动层，
//   并在下一 tick 等待轴状态变为 Idle。
// 验收标准：
//   1. 下发了 EnableCommand（active=true）给目标轴
//   2. 编排器状态 === EnsuringEnabled
//
TEST_F(JogOrchestratorTest, ShouldSendEnableWhenAxisIsDisabled)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();

    ASSERT_TRUE(driver.has<EnableCommand>());
    auto cmd = driver.lastForAxis<EnableCommand>(targetId);
    EXPECT_TRUE(cmd.active);
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::EnsuringEnabled);
}

// ============================================================================
// [测试场景 3] 未知状态等待：轴 Unknown 时不下发指令，保持等待
// ============================================================================
// 业务规则：
//   如果轴处于 Unknown（未知/初始化未完成）状态，编排器不应贸然下发指令，
//   而是停留在 EnsuringEnabled 阶段静默等待。
// 验收标准：
//   1. 编排器状态 === EnsuringEnabled（保持等待）
//   2. 未下发任何 EnableCommand
//
TEST_F(JogOrchestratorTest, ShouldStayInEnsuringEnabledWhenAxisIsUnknown)
{
    axis().applyFeedback({.state = AxisState::Unknown, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::EnsuringEnabled);
    EXPECT_FALSE(driver.has<EnableCommand>());
}

// ============================================================================
// [测试场景 4] 使能完成后转入 IssuingJog（但尚未下发点动指令）
// ============================================================================
// 业务规则：
//   当轴从 Disabled 上电成功（变为 Idle）后，编排器从 EnsuringEnabled
//   跳转到 IssuingJog。但点动指令的实质下发推迟到下一次 tick 周期。
//   这是两帧分离策略：状态判断与指令下发解耦。
// 验收标准：
//   1. 编排器状态 === IssuingJog
//   2. 未下发任何 JogCommand（点动指令尚未实际发出）
//
TEST_F(JogOrchestratorTest, ShouldTransitionToIssuingJogButNotSendJogCommandYet)
{
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();  // EnsuringEnabled: 下发EnableCommand

    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();  // EnsuringEnabled（收到Idle）→ IssuingJog

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::IssuingJog);
    EXPECT_FALSE(driver.has<JogCommand>());
}

// ============================================================================
// [测试场景 5] 下发点动指令并进入 Jogging（正向点动，基础路径）
// ============================================================================
// 业务规则：
//   JogOrchestrator 的主路径：轴处于 Idle 状态 → 启动正方向点动 →
//   下发 JogCommand(dir=Forward, active=true) → 进入 Jogging 运行状态。
// 验收标准：
//   1. 下发了 JogCommand，方向为 Forward，active=true
//   2. 编排器状态 === Jogging
//
TEST_F(JogOrchestratorTest, ShouldSendJogCommandAndEnterJogging)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();  // EnsuringEnabled: Idle → IssuingJog
    orchestrator->tick();  // IssuingJog: 下发JogCommand → Jogging

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_EQ(cmd.dir, Direction::Forward);
    EXPECT_TRUE(cmd.active);
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Jogging);
}

// ============================================================================
// [测试场景 6] 反向点动指令正确下发
// ============================================================================
// 业务规则：
//   点动支持正反两个方向。验证反向点动时，JogCommand 的 dir 字段
//   被正确设置为 Direction::Backward。
// 验收标准：
//   1. 下发了 JogCommand，方向为 Backward
//
TEST_F(JogOrchestratorTest, ShouldSendBackwardJogCommandCorrectly)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Backward);
    orchestrator->tick();
    orchestrator->tick();

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_EQ(cmd.dir, Direction::Backward);
}

// ============================================================================
// [测试场景 7] 幂等性：JogCommand 只下发一次（防止重复下发）
// ============================================================================
// 业务规则：
//   进入 Jogging 状态后，即使后续 tick 反复被调用，编排器也必须保证
//   JogCommand 只被下发一次。这是防止驱动层收到重复指令的重要防御机制。
//   重构后使用 m_jogIssued 布尔标志保证幂等。
// 验收标准：
//   1. 历史记录中 JogCommand 的出现次数 === 1
//
TEST_F(JogOrchestratorTest, ShouldSendJogCommandOnlyOnce)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();
    orchestrator->tick();  // JogCommand 在此次 tick 下发
    orchestrator->tick();  // 额外 tick (不应再下发)
    orchestrator->tick();  // 额外 tick (不应再下发)

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const FakeAxisDriver::Record& r){
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
//   拒绝后，执行安全熔断：通过 EnableUseCase 下发掉电，并进入 Error 终态。
// 验收标准：
//   1. 编排器状态 === Error
//   2. errorReason === AtPositiveLimit
//   3. 下发了 EnableCommand（active=false，即掉电指令）
//
TEST_F(JogOrchestratorTest, ShouldDisableAndEnterErrorWhenJogRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = true, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Error);
    EXPECT_EQ(orchestrator->errorReason(), RejectionReason::AtPositiveLimit);
    ASSERT_TRUE(driver.has<EnableCommand>());
    // 找到最后一条掉电指令
    bool foundDisable = false;
    for (auto it = driver.history.rbegin(); it != driver.history.rend(); ++it) {
        if (std::holds_alternative<EnableCommand>(it->cmd)) {
            auto cmd = std::get<EnableCommand>(it->cmd);
            if (!cmd.active) {
                foundDisable = true;
            }
            break;
        }
    }
    EXPECT_TRUE(foundDisable);
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
    orchestrator->stopJog(targetId, Direction::Forward);
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::IssuingStop);
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
    orchestrator->stopJog(targetId, Direction::Backward);
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Jogging);
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
    orchestrator->stopJog(AxisId::Z, Direction::Forward);

    // ⭐ 断言：由于目标不匹配，当前正在运行的 Y 轴绝不能被逼停
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Jogging);
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
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::IssuingStop);
}

// ============================================================================
// [测试场景 13] 停止指令下发：IssuingStop → WaitingForIdle 跃迁
// ============================================================================
// 业务规则：
//   进入 IssuingStop 后，编排器调用 axis->stopJog() 产生停止指令，
//   然后通过 group->driver()->send() 下发，并立即跃迁到 WaitingForIdle
//   阶段，等待底层物理轴完全停稳。
// 验收标准：
//   1. 下发了 JogCommand，active=false（即停止指令）
//   2. 编排器状态 === WaitingForIdle
//
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandAndTransitionToWaitingForIdle)
{
    runToJoggingState(Direction::Forward);
    orchestrator->stopJog(targetId, Direction::Forward);
    driver.history.clear();
    orchestrator->tick();

    ASSERT_TRUE(driver.has<JogCommand>());
    auto cmd = driver.lastForAxis<JogCommand>(targetId);
    EXPECT_FALSE(cmd.active);               // 停止指令（active=false）
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::WaitingForIdle);
}

// ============================================================================
// [测试场景 14] 停止指令幂等性：只下发一次 stop
// ============================================================================
// 业务规则：
//   与 JogCommand 相同，停止指令也必须保证幂等性。即使 WaitingForIdle
//   阶段被多次 tick 驱动，也不应重复下发停止指令。
//   重构后使用 m_stopIssued 布尔标志保证幂等。
// 验收标准：
//   1. JogCommand(active=false) 在历史记录中仅出现一次
//
TEST_F(JogOrchestratorTest, ShouldIssueStopCommandOnlyOnce)
{
    runToJoggingState(Direction::Backward);
    orchestrator->stopJog(targetId, Direction::Backward);
    driver.history.clear();
    orchestrator->tick();  // 第1次：下发 stop
    orchestrator->tick();  // 第2次：不应再次下发
    orchestrator->tick();  // 第3次：不应再次下发

    int count = std::count_if(driver.history.begin(), driver.history.end(), [](const FakeAxisDriver::Record& r){
            if (auto* jogCmd = std::get_if<JogCommand>(&r.cmd)) {
                return jogCmd->active == false;
            }
            return false;
        });
    EXPECT_EQ(count, 1);
}

// ============================================================================
// [测试场景 15] 等待空闲保持：轴尚未 Idle 时阻塞等待
// ============================================================================
// 业务规则：
//   下发停止指令后，编排器进入 WaitingForIdle 阶段，在此阶段编排器
//   不做任何操作（不主动下发指令），只是被动等待轴状态变为 Idle。
// 验收标准：
//   1. 编排器状态 === WaitingForIdle（保持等待）
//   2. driver 历史队列为空（未下发新指令）
//
TEST_F(JogOrchestratorTest, ShouldStayInWaitingForIdleIfAxisIsNotYetIdle)
{
    runToWaitingForIdleState(Direction::Forward);
    // 模拟轴仍在 Jogging（尚未停稳）
    axis().applyFeedback({.state = AxisState::Jogging, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::WaitingForIdle);
    EXPECT_TRUE(driver.history.empty());
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
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::EnsuringDisabled);
}

// ============================================================================
// [测试场景 17] 掉电指令下发：EnsuringDisabled 阶段
// ============================================================================
// 业务规则：
//   进入 EnsuringDisabled 后，编排器通过 EnableUseCase（无状态）
//   下发 EnableCommand(false) 给驱动层，通知硬件执行掉电操作。
// 验收标准：
//   1. 下发了 EnableCommand，active=false（掉电指令）
//
TEST_F(JogOrchestratorTest, ShouldIssueDisableCommandAndWait)
{
    runToWaitingForIdleState(Direction::Forward);
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();  // WaitingForIdle → EnsuringDisabled（并下发掉电指令）
    orchestrator->tick();

    ASSERT_TRUE(driver.has<EnableCommand>());
    // 找到最后一条掉电指令
    bool foundDisable = false;
    for (auto it = driver.history.rbegin(); it != driver.history.rend(); ++it) {
        if (std::holds_alternative<EnableCommand>(it->cmd)) {
            if (!std::get<EnableCommand>(it->cmd).active) {
                foundDisable = true;
            }
            break;
        }
    }
    EXPECT_TRUE(foundDisable);
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
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Done);
}

// ============================================================================
// [测试场景 19] Done 终态沉默：不再下发任何指令
// ============================================================================
// 业务规则：
//   编排器进入 Done 终态后，对后续所有 tick 调用均不做响应，
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
    orchestrator->tick();  // EnsuringDisabled → Done
    driver.history.clear();

    // 后续多次 tick，Done 状态不应再下发任何指令
    axis().applyFeedback({.state = AxisState::Disabled, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->tick();
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Done);
    EXPECT_TRUE(driver.history.empty());
}

// ============================================================================
// [测试场景 20] 分组不存在 → 立即 Error + GroupNotFound（NEW）
// ============================================================================
// ⭐ 组管理重构后的新测试路径
//
// 业务规则：
//   编排器绑定的是 SystemManager + groupName。如果分组在运行时被删除
//   或初始绑定的分组名称无效，tick 的第一步（SystemManager 解析）就会失败。
//   编排器必须立即进入 Error，并将错误存为 ContextRejection。
// 验收标准：
//   1. 编排器状态 === Error
//   2. lastError 为 ContextRejection::GroupNotFound
//
TEST_F(JogOrchestratorTest, ShouldEnterErrorWhenGroupNotFound)
{
    JogOrchestrator badOrch(manager, "NonExistentGroup");
    badOrch.startJog(AxisId::Y, Direction::Forward);
    badOrch.tick();

    EXPECT_EQ(badOrch.currentStep(), JogOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<ContextRejection>(badOrch.lastError()));
    EXPECT_EQ(std::get<ContextRejection>(badOrch.lastError()), ContextRejection::GroupNotFound);
}

// ============================================================================
// [测试场景 21] 点动被拒时掉电也只下发一次（幂等）—— NEW
// ============================================================================
// 业务规则：
//   Jog 被拒绝时编排器会调用 EnableUseCase 掉电。如果后续 tick 被调用，
//   error 终态不应再下发任何指令。
// 验收标准：
//   1. 编排器状态 === Error
//   2. EnableCommand(active=false) 仅出现一次
//
TEST_F(JogOrchestratorTest, ShouldDisableOnlyOnceWhenJogRejected)
{
    axis().applyFeedback({.state = AxisState::Idle, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = true, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();
    orchestrator->tick();  // Jog 被拒 → 掉电 + Error
    orchestrator->tick();  // Error 终态，不应再下发

    int disableCount = 0;
    for (const auto& r : driver.history) {
        if (auto* cmd = std::get_if<EnableCommand>(&r.cmd)) {
            if (!cmd->active) disableCount++;
        }
    }
    EXPECT_EQ(disableCount, 1);
}

// ============================================================================
// [测试场景 22] Error 终态幂等：多次 tick 保持 Error
// ============================================================================
// 业务规则：
//   进入 Error 终态后，后续所有 tick 调用都必须保持 Error，不得跳变到其他状态。
// 验收标准：
//   1. 编排器状态 === Error（保持不变）
//
TEST_F(JogOrchestratorTest, ShouldRemainInErrorAfterMultipleTicks)
{
    axis().applyFeedback({.state = AxisState::Error, .absPos = 0, .relPos = 0, .relZeroAbsPos = 0, .posLimit = false, .negLimit = false, .posLimitValue = 1000, .negLimitValue = -1000});
    orchestrator->startJog(targetId, Direction::Forward);
    orchestrator->tick();  // → Error
    orchestrator->tick();  // 再次 tick
    orchestrator->tick();  // 再次 tick

    EXPECT_EQ(orchestrator->currentStep(), JogOrchestrator::Step::Error);
}
