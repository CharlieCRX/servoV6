#include <gtest/gtest.h>
#include <functional>
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/AxisSyncService.h"

class AxisViewModelCoreTest : public ::testing::Test {
protected:
    // 1. 物理基础设施层
    FakePLC plc;
    FakeAxisDriver driver{plc};
    AxisSyncService syncService;

    // 2. 领域与应用层
    Axis axis;
    EnableUseCase enableUc{driver};
    JogAxisUseCase jogUc{driver};
    MoveAbsoluteUseCase moveAbsUc{driver};
    StopAxisUseCase stopUc{driver};

    JogOrchestrator jogOrch{enableUc, jogUc};
    AutoAbsMoveOrchestrator absOrch{enableUc, moveAbsUc};

    // 3. 表现层（待测目标）
    std::unique_ptr<AxisViewModelCore> vm;

    const int TICK_MS = 10; // 系统心跳：10ms

    void SetUp() override {
        // 组装 ViewModel
        vm = std::make_unique<AxisViewModelCore>(axis, jogOrch, absOrch, stopUc);
        
        // 物理引擎初始化：断电状态，设定好速度与限位
        plc.forceState(AxisState::Disabled);
        plc.setSimulatedJogVelocity(20.0);
        plc.setSimulatedMoveVelocity(50.0);
        plc.setLimits(1000.0, -1000.0);
        
        // 初始同步一次，确保 Domain 与物理世界对齐
        syncService.sync(axis, plc.getFeedback());
    }

    // 🌟 核心引擎：时间推进器
    // 模拟 main.cpp 中 QTimer 的行为：推进业务 -> 推进物理 -> 同步传感器
    void advanceTime(int totalMs) {
        int elapsed = 0;
        while (elapsed < totalMs) {
            vm->tick();                                // 1. UI 驱动策略层更新
            plc.tick(TICK_MS);                         // 2. 物理世界流逝 10ms
            syncService.sync(axis, plc.getFeedback()); // 3. 传感器读取并同步给 Axis
            elapsed += TICK_MS;
        }
    }

    // 🌟 核心引擎：条件等待器（防止死循环）
    bool waitUntil(std::function<bool()> condition, int timeoutMs = 5000) {
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            if (condition()) return true;
            advanceTime(TICK_MS);
            elapsed += TICK_MS;
        }
        return false;
    }
};

// =========================================================
// 🎯 测试 1：初始状态映射 (Initial State)
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReflectInitialDisabledState) {
    EXPECT_EQ(vm->state(), AxisState::Disabled);
    EXPECT_DOUBLE_EQ(vm->absPos(), 0.0);
}

// =========================================================
// 🎯 测试 2：点动全生命周期 (Jog Lifecycle)
// 验证：自动上电 -> 真实物理位移 -> 状态投射
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldExecuteJogPositiveRealistically) {
    // 记录初始位置
    double startPos = vm->absPos();

    // Act 1: UI 按下正向点动
    vm->jogPositivePressed();

    // Assert 1: 等待系统完成上电延迟 (FakePLC 有 150ms 延迟) 并进入 Jogging 态
    bool started = waitUntil([this]() { return vm->state() == AxisState::Jogging; });
    ASSERT_TRUE(started) << "Failed to enter Jogging state. Stuck at Enabling?";

    // Act 2: 保持按下，让物理引擎跑 500ms
    advanceTime(500);

    // Assert 2: 验证 UI 上的位置是否发生了真实的物理位移
    EXPECT_GT(vm->absPos(), startPos + 1.0) << "Axis position did not increase on UI!";
    
    // Act 3: UI 松开点动
    vm->jogPositiveReleased();

    // Assert 3: 等待系统刹车、自动断电回落到 Disabled 态
    bool stopped = waitUntil([this]() { return vm->state() == AxisState::Disabled; });
    ASSERT_TRUE(stopped) << "Axis did not safely disable after jogging!";

    // Assert 4: 验证物理惯性（停止后不可漂移）
    double finalPos = vm->absPos();
    advanceTime(200); // 再跑 200ms
    EXPECT_DOUBLE_EQ(vm->absPos(), finalPos) << "Axis drifted after releasing jog!";
}

// // =========================================================
// // 🎯 测试 3：绝对定位全生命周期 (MoveAbsolute Lifecycle)
// // 验证：目标到达 -> 物理收敛 -> 自动断电
// // =========================================================
// TEST_F(AxisViewModelCoreTest, ShouldCompleteAbsoluteMoveRealistically) {
//     double target = 100.0;

//     // Act 1: UI 下发定位指令
//     vm->moveAbsolute(target);

//     // Assert 1: 等待进入定位运动状态
//     bool isMoving = waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; });
//     ASSERT_TRUE(isMoving) << "Failed to enter MovingAbsolute state.";

//     // Assert 2: 等待系统自动完成定位，并完成 Orchestrator 的 Auto-Disable 流程
//     bool isDone = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 5000); // 给 5 秒钟跑 100 unit
//     ASSERT_TRUE(isDone) << "Move absolute timed out or failed to disable!";

//     // Assert 3: 验证 UI 读到的位置确实精准到达了目标
//     EXPECT_NEAR(vm->absPos(), target, 0.01) << "Axis failed to reach physical target on UI!";
// }

// // =========================================================
// // 🎯 测试 4：急停打断 (Emergency Stop)
// // =========================================================
// TEST_F(AxisViewModelCoreTest, ShouldHaltImmediatelyWhenStopPressed) {
//     // Act 1: 发起一个非常远的定位任务 (2000 距离，需要跑很久)
//     vm->moveAbsolute(2000.0);
    
//     // 让它跑 500ms
//     waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; });
//     advanceTime(500);
    
//     // 确认它真的在半路上
//     double midPos = vm->absPos();
//     EXPECT_GT(midPos, 0.0);
//     EXPECT_LT(midPos, 2000.0);

//     // Act 2: 💥 UI 突然按下急停
//     vm->stop();

//     // Assert 1: 等待系统强行中止并切断动力
//     bool isStopped = waitUntil([this]() { return vm->state() == AxisState::Disabled; });
//     ASSERT_TRUE(isStopped) << "Failed to halt after stop command!";

//     // Assert 2: 验证物理位置被截断在了半路，没有继续跑到 2000
//     EXPECT_LT(vm->absPos(), 1500.0) << "Axis ignored stop command and kept moving!";
// }