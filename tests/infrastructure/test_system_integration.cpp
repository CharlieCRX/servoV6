#include <gtest/gtest.h>
#include "entity/Axis.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/AxisSyncService.h"
#include "application/axis/StopAxisUseCase.h"
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "application/policy/JogOrchestrator.h"

TEST(SystemIntegrationTest, FullMoveAbsoluteLifeCycle) {
    
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    MoveAbsoluteUseCase moveUseCase(driver);
    AxisSyncService syncService;

    plc.forceState(AxisState::Idle); 
    plc.setSimulatedMoveVelocity(50.0);
    syncService.sync(axis, plc.getFeedback());     
    
    EXPECT_EQ(axis.state(), AxisState::Idle);

    RejectionReason reason = moveUseCase.execute(axis, 100.0);
    EXPECT_EQ(reason, RejectionReason::None); 

    plc.tick(1000); 
    syncService.sync(axis, plc.getFeedback()); 

    EXPECT_EQ(axis.state(), AxisState::MovingAbsolute);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.001);

    plc.tick(1000); 
    syncService.sync(axis, plc.getFeedback());

    EXPECT_EQ(axis.state(), AxisState::Idle); 
    EXPECT_NEAR(axis.currentAbsolutePosition(), 100.0, 0.001);
    EXPECT_FALSE(axis.hasPendingCommand()); 
    
}



// 案例 1：Orchestrator 驱动的端到端绝对定位测试
TEST(SystemIntegrationTest, ShouldCompleteAbsoluteMoveEndToEnd) {
    // 1. 基础设施与 Domain 装配 (Infrastructure & Domain Setup)
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    // 2. 动作层装配 (UseCase Setup - Stateless)
    EnableUseCase enableUc(driver);
    MoveAbsoluteUseCase moveUc(driver);

    // 3. 策略层装配 (Orchestrator Setup - Stateful)
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    // 初始化物理世界状态：初始为断电状态
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedMoveVelocity(50.0);
    syncService.sync(axis, plc.getFeedback());

    // 4. 触发系统级意图
    double targetPos = 100.0;
    orchestrator.start(targetPos); // 调用你的 start() 方法

    // 5. 离散时间物理仿真循环 (Tick Loop)
    const int TICK_MS = 10;
    const int MAX_TICKS = 500; // 5秒物理时间，防死循环
    int ticks = 0;
    bool motionObserved = false;
    double lastPos = axis.currentAbsolutePosition();

    // 循环条件：未结束、未出错、未超时
    while (orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Done && 
           orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Error && 
           ticks < MAX_TICKS) {
        
        // ① 策略层心跳：观察当前轴状态，状态机流转，下发相应指令
        orchestrator.update(axis); 
        
        // ② 物理世界时间推进 (执行命令，产生物理变化)
        plc.tick(TICK_MS);   
        
        // ③ 传感器反馈回流 (闭环核心：将物理变化同步回 Axis)
        syncService.sync(axis, plc.getFeedback()); 

        // 观察期：验证是否有真实位移发生 (Observability)
        if (axis.state() == AxisState::MovingAbsolute) {
            if (std::abs(axis.currentAbsolutePosition() - lastPos) > 0.0001) {
                motionObserved = true;
            }
            lastPos = axis.currentAbsolutePosition();
        }

        ticks++;
    }

    // 6. 系统级断言 (System-Level Invariants)
    
    // 验证 ①：流程未超时，且成功流转到了 Done 状态
    EXPECT_LT(ticks, MAX_TICKS) << "System timed out! Orchestrator stuck in step: " << static_cast<int>(orchestrator.currentStep());
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done) << "Orchestrator failed prematurely with Error!";
    
    // 验证 ②：行为正确性 - 发生了真实的物理位移
    EXPECT_TRUE(motionObserved) << "No physical motion was observed during the Move state!";
    
    // 验证 ③：闭环完整性 - 到达目标位置
    EXPECT_NEAR(axis.currentAbsolutePosition(), targetPos, 0.01) << "Axis did not reach target position!";
    
    // 验证 ④：策略正确性 - 根据 Orchestrator 逻辑，到达后会自动下发 Disable
    // 此时物理层收到 Disable 会直接掉电，反馈回来的状态应该是 Disabled
    EXPECT_EQ(axis.state(), AxisState::Disabled) << "AutoDisablePolicy failed: Axis did not disable after move!";
    EXPECT_FALSE(axis.hasPendingCommand()) << "Pending command was not consumed!";
}

// 案例 2：Jog 持续型行为与中断的端到端验证
TEST(SystemIntegrationTest, ShouldJogStartAndStopCorrectly) {
    // 1. 基础设施与 Domain 装配
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    // 2. 动作层装配 (Stateless UseCases)
    EnableUseCase enableUc(driver);
    JogAxisUseCase jogUc(driver);

    // 3. 策略层装配 (Stateful Orchestrator)
    JogOrchestrator orchestrator(enableUc, jogUc);

    // 初始化物理世界状态：初始断电
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedJogVelocity(10.0); // 设定点动速度 10 unit/s
    syncService.sync(axis, plc.getFeedback());

    // ==========================================
    // 阶段 A：按下正向点动按钮 (模拟 UI MousePress)
    // ==========================================
    orchestrator.startJog(Direction::Forward);

    const int TICK_MS = 10;
    int ticks = 0;
    const int MAX_TICKS = 500;
    bool startedJogging = false;
    double posBeforeJog = axis.currentAbsolutePosition();

    // 循环 1：等待系统自动上电并切入 Jogging 状态
    while (ticks < MAX_TICKS) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        if (axis.state() == AxisState::Jogging) {
            startedJogging = true;
            // 物理世界持续运行 500ms (50 * 10ms)，积累位移
            for (int i = 0; i < 50; ++i) {
                orchestrator.update(axis);
                plc.tick(TICK_MS);
                syncService.sync(axis, plc.getFeedback());
            }
            break;
        }
        ticks++;
    }

    // 断言 A：必须成功进入点动状态，且物理位置必须发生真实改变
    EXPECT_TRUE(startedJogging) << "System failed to enter Jogging state! Stuck at Enabling?";
    double posDuringJog = axis.currentAbsolutePosition();
    EXPECT_GT(posDuringJog, posBeforeJog) << "Observability Failed: Position did not increase during Forward Jog!";


    // ==========================================
    // 阶段 B：松开点动按钮 (模拟 UI MouseRelease)
    // ==========================================
    // 触发防误杀逻辑的正常停止 (向底层发送 JogCommand {active: false})
    orchestrator.stopJog(Direction::Forward); 

    ticks = 0;
    bool fullyStopped = false;

    // 循环 2：等待系统制动 -> 回落 Idle -> 触发 EnsuringDisabled -> 断电 (Done)
    while (ticks < MAX_TICKS) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        // 根据你的状态机，最终会停在 Done 状态
        if (orchestrator.currentStep() == JogOrchestrator::Step::Done) {
            fullyStopped = true;
            break;
        }
        ticks++;
    }

    // 断言 B：必须成功走到 Done，并且策略层成功执行了掉电逻辑
    EXPECT_TRUE(fullyStopped) << "Orchestrator did not reach Done state after stopping!";
    EXPECT_EQ(axis.state(), AxisState::Disabled) << "Axis did not disable after Jogging completed!";
    EXPECT_FALSE(axis.hasPendingCommand()) << "Pending command was not cleared!";

    // 断言 C：物理边界约束（停止后不可漂移）
    double posAfterStop = axis.currentAbsolutePosition();
    for (int i = 0; i < 20; ++i) {
        plc.tick(TICK_MS); 
    }
    syncService.sync(axis, plc.getFeedback());
    EXPECT_DOUBLE_EQ(axis.currentAbsolutePosition(), posAfterStop) << "Physical Violation: Axis drifted after completely stopping!";
}