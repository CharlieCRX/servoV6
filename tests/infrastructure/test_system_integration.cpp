#include <gtest/gtest.h>
#include "entity/Axis.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/AxisSyncService.h"
#include "application/policy/AutoAbsMoveOrchestrator.h"
#include "application/policy/AutoRelMoveOrchestrator.h"
#include "application/policy/JogOrchestrator.h"
#include "application/axis/StopAxisUseCase.h"

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


// 异常场景 A：事前拦截（预检失败）
TEST(SystemIntegrationTest, ShouldFailToStartWhenTargetExceedsLimit) {
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    EnableUseCase enableUc(driver);
    MoveAbsoluteUseCase moveUc(driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    // 设置初始物理环境
    plc.forceState(AxisState::Disabled);
    plc.setLimits(80.0, -80.0); // 软限位同步为 80
    syncService.sync(axis, plc.getFeedback());

    // 尝试走到 150.0（必然被拦截）
    orchestrator.start(150.0);

    const int TICK_MS = 10;
    int ticks = 0;
    while (orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Done && 
           orchestrator.currentStep() != AutoAbsMoveOrchestrator::Step::Error && 
           ticks < 100) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        ticks++;
    }

    // 断言：
    // 1. 状态机必须是 Error
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Error) << "Orchestrator should fail at IssuingMove step!";
    // 2. 轴的物理状态决不能是 Moving
    EXPECT_NE(axis.state(), AxisState::MovingAbsolute) << "Axis should never start moving!";
    // 3. 安全策略：失败后应该下发了掉电指令 (根据你的 Orchestrator 逻辑)
    EXPECT_EQ(axis.state(), AxisState::Disabled);
}

// 异常场景 B：半路夭折（运行中途物理层突发错误）
TEST(SystemIntegrationTest, ShouldFailWhenMoveInterruptedMidway) {
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    EnableUseCase enableUc(driver);
    MoveAbsoluteUseCase moveUc(driver);
    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    // 初始物理环境：限位非常宽裕
    plc.forceState(AxisState::Disabled);
    plc.setLimits(200.0, -200.0); 
    plc.setSimulatedMoveVelocity(50.0);
    syncService.sync(axis, plc.getFeedback());

    // 发起合法指令
    orchestrator.start(150.0);

    const int TICK_MS = 10;
    int ticks = 0;
    bool startedMoving = false;
    bool errorHandled = false;

    while (ticks < 500) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        // 观测点 1：确保它真的跑起来了
        if (axis.state() == AxisState::MovingAbsolute && !startedMoving) {
            startedMoving = true;
            
            // 🌟 暴击测试：在移动了几个周期后，强行给物理世界注入一个硬件报警！
            plc.forceState(AxisState::Error); 
        }

        // 观测点 2：Orchestrator 是否正确捕获了异常
        if (orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Error) {
            errorHandled = true;
            break;
        }

        ticks++;
    }

    // 断言：
    EXPECT_TRUE(startedMoving) << "Move never started!";
    EXPECT_TRUE(errorHandled) << "Orchestrator did not handle the in-flight hardware error!";
    EXPECT_NE(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done) << "Orchestrator falsely claimed success!";
}


// TDD 红灯案例 4：连续相对定位的端到端验证
TEST(SystemIntegrationTest, ShouldCompleteRelativeMoveEndToEnd) {
    // 1. 系统装配
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    EnableUseCase enableUc(driver);
    MoveRelativeUseCase moveRelUc(driver);
    AutoRelMoveOrchestrator orchestrator(enableUc, moveRelUc);

    // 初始化物理世界
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedMoveVelocity(50.0); // 速度 50 unit/s
    syncService.sync(axis, plc.getFeedback());

    const int TICK_MS = 10;
    const int MAX_TICKS = 500;

    // ==========================================
    // 阶段 A：第一次相对定位 (+50.0)
    // ==========================================
    orchestrator.start(50.0); // 预期终点: 0 + 50 = 50
    
    int ticks = 0;
    while (orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Done && 
           orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Error && 
           ticks < MAX_TICKS) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        ticks++;
    }

    // 断言 A：第一次移动必须成功，且位置等于 50.0
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done) << "First relative move failed!";
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.01) << "First relative move calculated position incorrectly!";
    EXPECT_EQ(axis.state(), AxisState::Disabled) << "Axis did not auto-disable after first move!";

    // ==========================================
    // 阶段 B：第二次相对定位 (-20.0)
    // ==========================================
    orchestrator.start(-20.0); // 预期终点: 50 - 20 = 30
    
    ticks = 0;
    while (orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Done && 
           orchestrator.currentStep() != AutoRelMoveOrchestrator::Step::Error && 
           ticks < MAX_TICKS) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        ticks++;
    }

    // 断言 B：第二次移动必须成功，且位置等于 30.0
    EXPECT_EQ(orchestrator.currentStep(), AutoRelMoveOrchestrator::Step::Done) << "Second relative move failed!";
    EXPECT_NEAR(axis.currentAbsolutePosition(), 30.0, 0.01) << "Math Error: Second relative move did not stack correctly!";
    EXPECT_EQ(axis.state(), AxisState::Disabled);
}


TEST(SystemIntegrationTest, ShouldInterruptJogAtLimitAndRestrictFurtherMoves) {
    // ==========================================
    // 0. 系统基础设施与防腐层装配
    // ==========================================
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    EnableUseCase enableUc(driver);
    JogAxisUseCase jogUc(driver);
    MoveAbsoluteUseCase moveUc(driver);

    JogOrchestrator jogOrchestrator(enableUc, jogUc);
    AutoAbsMoveOrchestrator moveOrchestrator(enableUc, moveUc);

    // 物理世界埋雷：正限位卡在 50.0
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedJogVelocity(20.0); 
    plc.setLimits(50.0, -1000.0); // 设置正软限位
    syncService.sync(axis, plc.getFeedback());

    const int TICK_MS = 10;
    int ticks = 0;

    // ==========================================
    // 阶段 1：作死测试 -> 一路向正方向点动，直到撞墙
    // ==========================================
    jogOrchestrator.startJog(Direction::Forward);
    
    while (ticks < 500) { // 跑 5 秒物理时间，足够撞上 50.0
        jogOrchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        // 观测点：如果 Orchestrator 完成了中断收尾并回落到终态，跳出
        if (jogOrchestrator.currentStep() == JogOrchestrator::Step::Done || 
            jogOrchestrator.currentStep() == JogOrchestrator::Step::Error) {
            break;
        }
        ticks++;
    }

    // 断言 1：必须精确停在 50.0 限位点，并且触发中断导致掉电
    EXPECT_TRUE(plc.getFeedback().posLimit) << "FakePLC did not trigger the hardware limit bit!";
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.01) << "Axis position broke through the limit!";
    EXPECT_EQ(axis.state(), AxisState::Disabled) << "Axis must be safely disabled after hitting the hardware limit!";


    // ==========================================
    // 阶段 2：安全死锁验证 A -> 企图继续向同向(Forward)点动
    // ==========================================
    jogOrchestrator.startJog(Direction::Forward); // 再次作死按正向
    ticks = 0;
    while (ticks < 100) {
        jogOrchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        if (jogOrchestrator.currentStep() == JogOrchestrator::Step::Error) break;
        ticks++;
    }
    
    // 断言 2：必须被无情拒绝，报错原因必须是 AtPositiveLimit
    EXPECT_EQ(jogOrchestrator.currentStep(), JogOrchestrator::Step::Error) << "Orchestrator failed to block forward jog at limit!";
    EXPECT_EQ(jogOrchestrator.errorReason(), RejectionReason::AtPositiveLimit);


    // ==========================================
    // 阶段 3：安全死锁验证 B -> 企图使用 Move 指令(禁止位置移动)
    // ==========================================
    moveOrchestrator.start(0.0); // 哪怕是企图走回 0.0 安全区，只要在限位上，Move 统统不许用
    ticks = 0;
    while (ticks < 100) {
        moveOrchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        if (moveOrchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Error) break;
        ticks++;
    }

    // 断言 3：Move 指令必须被全局锁死
    EXPECT_EQ(moveOrchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Error) << "Orchestrator failed to block Absolute Move at limit!";
    EXPECT_EQ(moveOrchestrator.errorReason(), RejectionReason::AtPositiveLimit);


    // ==========================================
    // 阶段 4：唯一生路 -> 反向逃逸 (Jog Backward)
    // ==========================================
    jogOrchestrator.startJog(Direction::Backward); // 正确操作：反向点动逃离
    ticks = 0;
    bool escapedLimit = false;

    while (ticks < 300) { // 等待上电 + 反向运动脱离 50.0
        jogOrchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        // 观测点：如果成功进入 Jogging，并且位置从 50.0 降下来导致限位位复位，说明逃逸成功
        if (axis.state() == AxisState::Jogging && !plc.getFeedback().posLimit) {
            escapedLimit = true;
            break;
        }
        ticks++;
    }

    // 断言 4：必须成功进入反向运动，并且物理限制解除
    EXPECT_TRUE(escapedLimit) << "System completely locked up! Failed to allow backward escape jog!";
    EXPECT_LT(axis.currentAbsolutePosition(), 49.9) << "Axis position did not decrease during escape!";

    // 最后别忘了安全停止逃逸动作
    jogOrchestrator.stopJog(Direction::Backward);
}


TEST(SystemIntegrationTest, ShouldHaltCompletelyAndReportErrorWhenEmergencyStopInvoked) {
    // 1. 系统装配
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    AxisSyncService syncService;

    EnableUseCase enableUc(driver);
    MoveAbsoluteUseCase moveUc(driver);
    StopAxisUseCase stopUc(driver); // 专门用于紧急打断

    AutoAbsMoveOrchestrator orchestrator(enableUc, moveUc);

    // 初始物理环境：速度设慢点，目标设远点，拉长运动时间
    plc.forceState(AxisState::Disabled);
    plc.setSimulatedMoveVelocity(20.0); // 20 unit/s
    syncService.sync(axis, plc.getFeedback());

    // 2. 发起一个很长的移动任务 (目标 200.0，需要 10秒 才能跑完)
    orchestrator.start(200.0);

    const int TICK_MS = 10;
    int ticks = 0;
    bool startedMoving = false;

    // 3. 先让它正常跑 2 秒钟 (200 个 tick)
    while (ticks < 200) {
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());
        
        if (axis.state() == AxisState::MovingAbsolute) {
            startedMoving = true;
        }
        ticks++;
    }

    // 确认它真的跑起来了
    EXPECT_TRUE(startedMoving) << "Axis never started moving!";
    
    // 4. 💥 突发急停！用户拍下了 E-Stop 按钮！
    // 绕过 Orchestrator，直接用最高优的 StopUseCase 发送指令
    stopUc.execute(axis);

    // 5. 继续运行系统循环，观察 Orchestrator 的收敛行为
    int stopTicks = 0;
    while (stopTicks < 500) { // 最多再等 5 秒
        orchestrator.update(axis);
        plc.tick(TICK_MS);
        syncService.sync(axis, plc.getFeedback());

        // 等待 Orchestrator 结束它的生命周期
        if (orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Error ||
            orchestrator.currentStep() == AutoAbsMoveOrchestrator::Step::Done) {
            break;
        }
        stopTicks++;
    }

    // 6. 核心语义断言
    
    // 物理层断言：绝对不能跑到 200.0，必须在半路停下
    EXPECT_LT(axis.currentAbsolutePosition(), 150.0) << "Axis failed to stop in time!";

    // 🌟 逻辑层断言（最容易挂的地方）：
    // 任务中途被打断，绝对不能谎报军情说自己 Done 了！
    EXPECT_NE(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Done) 
        << "FATAL LOGIC ERROR: Orchestrator falsely claimed the move was Done after an Emergency Stop!";
        
    // 必须以 Error (或某种 Aborted) 态结束
    EXPECT_EQ(orchestrator.currentStep(), AutoAbsMoveOrchestrator::Step::Error) 
        << "Orchestrator failed to realize it was interrupted!";
}