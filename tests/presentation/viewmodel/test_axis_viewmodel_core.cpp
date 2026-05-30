#include <gtest/gtest.h>
#include <functional>
#include <variant>
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/ViewModelError.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/SystemManager.h"
#include "application/policy/AbsMovePolicy.h"
#include "application/policy/RelMovePolicy.h"
#include "domain/entity/SystemContext.h"

// ============================================================================
// AxisViewModelCore 单元测试（重构后，基于 SystemManager + 分组路由）
//
// ╔══════════════════════════════════════════════════════════════╗
// ║ 架构变更（相比旧版测试）：                                  ║
// ║   1. ViewModel 构造不再传入 Axis / UseCase / Orchestrator  ║
// ║      而是传入 (SystemManager&, groupName, axisId)          ║
// ║   2. UseCase / Orchestrator 在 ViewModel 内部创建（值语义）║
// ║   3. API 使用方向枚举：jog(Direction) / jogStop(Direction) ║
// ║   4. 错误接口：hasError() / lastError() / clearError()     ║
// ║   5. 测试使用 SystemContext::tryGetAxis() 检查领域状态     ║
// ╚══════════════════════════════════════════════════════════════╝
// ============================================================================

class AxisViewModelCoreTest : public ::testing::Test {
protected:
    static constexpr const char* GROUP_NAME = "Machine_Test";

    // ---- 硬件虚拟化 ----
    FakePLC plc;
    FakeAxisDriver driver{plc};

    // ---- 全局 SystemManager ----
    SystemManager manager;

    // ---- 分组上下文 ----
    SystemContext* ctx = nullptr;

    // ---- 待测 ViewModel ----
    std::unique_ptr<AxisViewModelCore> vm;

    const int TICK_MS = 10;  // 每帧 10ms

    void SetUp() override {
        // 1. 创建分组并绑定驱动
        ContextRejection reason;
        ASSERT_TRUE(manager.createGroup(GROUP_NAME, reason));
        ASSERT_TRUE(manager.tryGetGroup(GROUP_NAME, ctx, reason));
        ASSERT_NE(ctx, nullptr);
        ctx->setDriver(&driver);

        // 2. 初始化硬件默认状态
        plc.forceState(AxisId::Y, AxisState::Disabled);
        plc.setSimulatedMoveVelocity(AxisId::Y, 25.0);
        plc.setSimulatedJogVelocity(AxisId::Y, 20.0);
        plc.setLimits(AxisId::Y, 1000.0, -1000.0);

        // 3. 首次 pollFeedback 将领域层与硬件同步
        driver.pollFeedback(*ctx);

        // 4. 创建 ViewModel（重构后构造签名）
        vm = std::make_unique<AxisViewModelCore>(manager, GROUP_NAME, AxisId::Y);

        // 5. 设置默认点动/运动速度
        vm->setJogVelocity(20.0);
        vm->setMoveVelocity(25.0);
    }

    // ====================================================================
    // 辅助：时间推进器
    //   1. 驱动 ViewModel（编排器状态机）
    //   2. 驱动物理 PLC 引擎
    //   3. 同步硬件反馈到领域层
    // ====================================================================
    void advanceTime(int totalMs) {
        int elapsed = 0;
        while (elapsed < totalMs) {
            vm->tick();                                   // ViewModel 帧驱动
            driver.pollFeedback(*ctx);                    // 硬件反馈 -> 领域层同步
            elapsed += TICK_MS;
        }
    }

    // ====================================================================
    // 辅助：条件等待器（避免死循环）
    // ====================================================================
    bool waitUntil(std::function<bool()> condition, int timeoutMs = 5000) {
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            if (condition()) return true;
            advanceTime(TICK_MS);
            elapsed += TICK_MS;
        }
        return false;
    }

    // ====================================================================
    // 辅助：获取领域层 Axis 指针（用于状态断言）
    // ====================================================================
    Axis* getAxis() {
        Axis* axis = nullptr;
        ContextRejection r;
        ctx->tryGetAxis(AxisId::Y, axis, r);
        return axis;
    }
};

// =========================================================
// 测试 1：初始状态映射（Projection）
//   构造后 state() 应反映领域层 Disabled，absPos=0
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReflectInitialDisabledState) {
    EXPECT_EQ(vm->state(), AxisState::Disabled);
    EXPECT_DOUBLE_EQ(vm->absPos(), 0.0);
    EXPECT_DOUBLE_EQ(vm->relPos(), 0.0);
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// 测试 2：使能生命周期（Enable -> Idle）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldEnableAxisAndReflectState) {
    // Act: 使能
    vm->enable(true);

    // Assert: 等待硬件 Enable 延迟（FakePLC 约 150ms）
    bool enabled = waitUntil([this]() { return vm->state() == AxisState::Idle; });
    ASSERT_TRUE(enabled) << "Axis did not reach Idle after enable!";

    // 无错误
    EXPECT_FALSE(vm->hasError());

    // 确认领域层一致
    auto* axis = getAxis();
    ASSERT_NE(axis, nullptr);
    EXPECT_EQ(axis->state(), AxisState::Idle);
}

// =========================================================
// 测试 3：点动全生命周期（Jog Forward -> Jogging -> Stop -> Disabled）
//   验证：使能 -> 点动 -> 物理位移 -> 停止 -> 自动掉电
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldExecuteJogForwardLifecycle) {
    // 前置：使能
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double startPos = vm->absPos();

    // Act 1: 按下正向点动
    vm->jog(Direction::Forward);

    // Assert 1: 等待 JogOrchestrator 进入 Jogging
    bool jogging = waitUntil([this]() { return vm->state() == AxisState::Jogging; });
    ASSERT_TRUE(jogging) << "Failed to enter Jogging state!";

    // Act 2: 保持点动 500ms -> 产生物理位移
    advanceTime(500);

    // Assert 2: 位置应显著增加（速度 20 × 0.5s ≈ 10.0）
    EXPECT_GT(vm->absPos(), startPos + 5.0) << "Position did not increase during jog!";

    // Act 3: 松开点动
    vm->jogStop(Direction::Forward);

    // Assert 3: 等待刹车 + 自动掉电 -> Disabled
    bool stopped = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 5000);
    ASSERT_TRUE(stopped) << "Axis did not disable after jog stop!";

    // Assert 4: 停止后不漂移
    double finalPos = vm->absPos();
    advanceTime(200);
    EXPECT_NEAR(vm->absPos(), finalPos, 0.01) << "Axis drifted after stop!";
}

// =========================================================
// 测试 4：反向点动（Jog Backward）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldExecuteJogBackwardLifecycle) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double startPos = vm->absPos();

    // 正向点动先跑一段距离
    vm->jog(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Jogging; }));
    advanceTime(300);
    vm->jogStop(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));

    double afterForwardPos = vm->absPos();
    EXPECT_GT(afterForwardPos, startPos);

    // 再使能 + 反向点动
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    vm->jog(Direction::Backward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Jogging; }));
    advanceTime(300);
    vm->jogStop(Direction::Backward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));

    // 反向点动后位置应回落
    EXPECT_LT(vm->absPos(), afterForwardPos - 1.0) << "Position did not decrease on backward jog!";
}

// =========================================================
// 测试 5：急停中断运动（两步操作：setAbsTarget + triggerAbsMove）
//   验证：运动中 stop() -> 立即停止 -> 回到 Disabled
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldHaltImmediatelyWhenStopPressed) {
    // 两步操作：设置目标 + 触发移动（Policy 编排）
    vm->setAbsTarget(800.0);
    advanceTime(TICK_MS);  // 让 consumePendingCommands 发送 SetAbsTargetCommand
    vm->triggerAbsMove();
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; }));

    // 让它跑 500ms
    advanceTime(500);

    double midPos = vm->absPos();
    EXPECT_GT(midPos, 0.0);
    EXPECT_LT(midPos, 600.0) << "Should not have reached 800 yet";

    // 💥 按下停止
    vm->stop();

    // 等待中断 -> Disabled
    bool stopped = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 5000);
    ASSERT_TRUE(stopped) << "Failed to stop!";

    // 位置被截断，未到达目标
    EXPECT_LT(vm->absPos(), 700.0) << "Axis kept moving after stop!";

    // 停止后不漂移
    double finalPos = vm->absPos();
    advanceTime(200);
    EXPECT_NEAR(vm->absPos(), finalPos, 0.01) << "Axis drifted after stop!";
}

// =========================================================
// 测试 6：Disabled 状态下两步绝对定位（Policy 自动 Enable + Move + Disable）
//   验证：Disabled 时 setAbsTarget + triggerAbsMove -> 完整生命周期
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldCompleteAbsMoveFromDisabledViaPolicy) {
    // 新架构语义：Disabled 状态下 triggerAbsMove 触发 AbsMovePolicy 编排
    // EnsuringEnabled -> TriggeringMove -> WaitingMotionStart -> WaitingMotionFinish -> Disabling -> Done
    double beforePos = vm->absPos();
    EXPECT_EQ(vm->state(), AxisState::Disabled);

    vm->setAbsTarget(100.0);
    advanceTime(TICK_MS);  // 让 consumePendingCommands 发送 SetAbsTargetCommand
    vm->triggerAbsMove();

    // 先推进一帧确保 Policy 状态机被驱动启动
    advanceTime(TICK_MS);

    // 等待运动完成（Policy 自动 Enable + Move + Disable）
    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done) << "Move should complete normally from Disabled state!";
    EXPECT_NEAR(vm->absPos(), 100.0, 0.01) << "Axis should reach target position!";
    EXPECT_GT(vm->absPos(), beforePos + 1.0) << "Position should have changed!";

    // 无错误（正常完成）
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// 测试 9：错误接口（hasError / lastError / clearError）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportAndClearErrors) {
    // 初始无错误
    EXPECT_FALSE(vm->hasError());
    auto e = vm->lastError();
    EXPECT_FALSE(e.isValid());

    // 使能 -> 正常完成
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));
    EXPECT_FALSE(vm->hasError());

    // clearError 幂等
    vm->clearError();
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// 测试 10：零位操作
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldHandleZeroOperations) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    // 正向点动产生位移
    vm->jog(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Jogging; }));
    advanceTime(500);
    vm->jogStop(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));

    double joggedPos = vm->absPos();
    EXPECT_GT(joggedPos, 5.0) << "Jog should have moved the axis";

    // 零位操作需要在 Idle 状态，先使能
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    // 设置相对零点
    vm->setRelativeZero();
    // setRelativeZero 下发意图后等待闭环：等待 relPos 归零
    bool relZeroed = waitUntil([this]() { return std::abs(vm->relPos()) < 0.01; }, 5000);
    ASSERT_TRUE(relZeroed) << "Relative zero did not take effect!";
    EXPECT_NEAR(vm->relPos(), 0.0, 0.01) << "Relative position should be 0 after setRelativeZero";

    // 绝对位置清零
    vm->zeroAbsolutePosition();
    // zeroAbsolutePosition 需要等待闭环（applyFeedback 中消费 ZeroAbsoluteCommand）
    bool absZeroed = waitUntil([this]() { return std::abs(vm->absPos()) < 0.01; }, 5000);
    ASSERT_TRUE(absZeroed) << "Absolute zero did not take effect!";
    EXPECT_NEAR(vm->absPos(), 0.0, 0.01) << "Absolute position should be 0 after zeroAbsolutePosition";

    // 清除相对零点（需要在 Idle 状态，此时轴仍是 Idle）
    vm->clearRelativeZero();
    // 等待闭环
    bool relCleared = waitUntil([this]() { return std::abs(vm->relPos() - vm->absPos()) < 0.01; }, 5000);
    ASSERT_TRUE(relCleared) << "Clear relative zero did not take effect!";
    // 清除后 relPos 应回到绝对位置
    EXPECT_NEAR(vm->relPos(), vm->absPos(), 0.01) << "relPos should equal absPos after clearRelativeZero";
}

// =========================================================
// 测试 11：限位反射（posLimit / negLimit）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReflectLimits) {
    // 默认限位：±1000
    EXPECT_DOUBLE_EQ(vm->posLimit(), 1000.0);
    EXPECT_DOUBLE_EQ(vm->negLimit(), -1000.0);
}

// =========================================================
// 测试 12：速度设置（jogVelocity / moveVelocity）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldSetAndReflectVelocity) {
    EXPECT_DOUBLE_EQ(vm->jogVelocity(), 20.0);
    EXPECT_DOUBLE_EQ(vm->moveVelocity(), 25.0);

    vm->setJogVelocity(50.0);
    vm->setMoveVelocity(60.0);

    EXPECT_DOUBLE_EQ(vm->jogVelocity(), 50.0);
    EXPECT_DOUBLE_EQ(vm->moveVelocity(), 60.0);
}

// =========================================================
// 测试 13：isEnabled 属性
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportIsEnabledCorrectly) {
    EXPECT_FALSE(vm->isEnabled());

    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));
    EXPECT_TRUE(vm->isEnabled());

    vm->disable();  // 等价于 enable(false)
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));
    EXPECT_FALSE(vm->isEnabled());
}

// =========================================================
// ⭐ 测试 14：错误列表收集模式 -- 追加而非覆盖
//   验证：多次操作产生的错误会累积在列表中
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldAccumulateErrorsWithoutOverwriting) {
    // 初始无错误
    EXPECT_FALSE(vm->hasError());
    EXPECT_EQ(vm->errorCount(), 0u);

    // 设置非法速度（v <= 0.0 => InvalidArgument 拒绝）
    vm->setJogVelocity(0.0);
    ASSERT_TRUE(vm->hasError());
    EXPECT_EQ(vm->errorCount(), 1u);
    auto firstErr = vm->lastError();
    EXPECT_FALSE(firstErr.code.empty());

    // 再次设置非法速度（产生第二个错误，应追加而非覆盖第一个）
    vm->setJogVelocity(0.0);
    EXPECT_GE(vm->errorCount(), 2u);
}

// =========================================================
// ⭐ 测试 15：allErrors() 返回完整快照
// =========================================================
TEST_F(AxisViewModelCoreTest, AllErrorsShouldReturnFullSnapshot) {
    EXPECT_EQ(vm->allErrors().size(), 0u);

    vm->setJogVelocity(0.0);
    ASSERT_GE(vm->errorCount(), 1u);

    auto errors = vm->allErrors();
    EXPECT_EQ(errors.size(), vm->errorCount());
    EXPECT_FALSE(errors[0].code.empty());
}

// =========================================================
// ⭐ 测试 16：acknowledgeError 按索引移除单条错误
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldAcknowledgeErrorByIndex) {
    vm->setJogVelocity(0.0);
    ASSERT_GE(vm->errorCount(), 1u);

    size_t before = vm->errorCount();
    vm->acknowledgeError(0);  // 移除第一条
    EXPECT_EQ(vm->errorCount(), before - 1);

    // 再次确认：索引越界无副作用
    vm->acknowledgeError(999);
    EXPECT_EQ(vm->errorCount(), before - 1);
}

// =========================================================
// ⭐ 测试 17：clearAllErrors 批量清除
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldClearAllErrors) {
    vm->setJogVelocity(0.0);
    vm->setJogVelocity(0.0);
    ASSERT_GE(vm->errorCount(), 2u);

    vm->clearAllErrors();
    EXPECT_EQ(vm->errorCount(), 0u);
    EXPECT_FALSE(vm->hasError());
    EXPECT_FALSE(vm->lastError().isValid());
}

// =========================================================
// ⭐ 测试 18：clearError() 兼容接口等价于 clearAllErrors
// =========================================================
TEST_F(AxisViewModelCoreTest, ClearErrorShouldBeEquivalentToClearAllErrors) {
    vm->setJogVelocity(0.0);
    ASSERT_TRUE(vm->hasError());

    vm->clearError();
    EXPECT_FALSE(vm->hasError());
    EXPECT_EQ(vm->errorCount(), 0u);
}

// =========================================================
// ⭐ 测试 19：enable(false) 不产生错误（disable 是安全操作）
// =========================================================
TEST_F(AxisViewModelCoreTest, DisableShouldNotProduceError) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    vm->disable();
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// ⭐ 测试 20：lastError() 空列表返回无效错误
// =========================================================
TEST_F(AxisViewModelCoreTest, LastErrorShouldReturnInvalidWhenEmpty) {
    EXPECT_FALSE(vm->hasError());
    auto err = vm->lastError();
    EXPECT_FALSE(err.isValid());
    EXPECT_TRUE(err.code.empty());
}

// =========================================================
// ⭐ 测试 21：两步绝对定位全生命周期（setAbsTarget + triggerAbsMove）
//   验证：Idle 状态 -> setAbsTarget -> triggerAbsMove ->
//         MovingAbsolute -> Disabled -> 位置精确
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldCompleteTwoStepAbsMoveEndToEnd) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double target = 150.0;

    // Step 1: 设置绝对目标（仅写 PLC，不触发运动）
    vm->setAbsTarget(target);
    advanceTime(TICK_MS);  // 让 consumePendingCommands 发送 SetAbsTargetCommand

    // 确认尚未运动
    EXPECT_EQ(vm->state(), AxisState::Idle);
    EXPECT_NEAR(vm->absPos(), 0.0, 0.01);

    // Step 2: 触发绝对移动（走 AbsMovePolicy 编排）
    vm->triggerAbsMove();

    // 等待进入 MovingAbsolute
    bool moving = waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; });
    ASSERT_TRUE(moving) << "Failed to enter MovingAbsolute after triggerAbsMove!";

    // 等待完成 -> Disabled
    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done) << "Two-step absolute move timed out!";

    EXPECT_NEAR(vm->absPos(), target, 0.01);
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// ⭐ 测试 22：两步相对定位全生命周期（setRelTarget + triggerRelMove）
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldCompleteTwoStepRelMoveEndToEnd) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double distance = 60.0;

    // Step 1: 设置相对距离
    vm->setRelTarget(distance);
    advanceTime(TICK_MS);

    // 确认尚未运动
    EXPECT_EQ(vm->state(), AxisState::Idle);
    EXPECT_NEAR(vm->absPos(), 0.0, 0.01);

    // Step 2: 触发相对移动
    vm->triggerRelMove();

    // 等待进入 MovingRelative
    bool moving = waitUntil([this]() { return vm->state() == AxisState::MovingRelative; });
    ASSERT_TRUE(moving);

    // 等待完成 -> Disabled
    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done);

    EXPECT_NEAR(vm->absPos(), distance, 0.01);
    EXPECT_FALSE(vm->hasError());

    // 第二次相对定位（从 Disabled 状态，Policy 自动 Enable）
    vm->setRelTarget(30.0);
    advanceTime(TICK_MS);
    vm->triggerRelMove();

    done = waitUntil([this]() { return vm->state() == AxisState::MovingRelative; }, 10000);
    ASSERT_TRUE(done) << "Second move did not start!";

    done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done);
    EXPECT_NEAR(vm->absPos(), 90.0, 0.01);
}

// =========================================================
// ⭐ 测试 23：setAbsTarget 不触发运动（仅写寄存器，状态不变）
// =========================================================
TEST_F(AxisViewModelCoreTest, SetAbsTargetShouldNotTriggerMotion) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double startPos = vm->absPos();
    EXPECT_NEAR(startPos, 0.0, 0.01);

    vm->setAbsTarget(300.0);
    advanceTime(TICK_MS);
    advanceTime(200);  // 再多等几帧确保没有意外运动

    // 状态应保持 Idle，位置不变
    EXPECT_EQ(vm->state(), AxisState::Idle);
    EXPECT_NEAR(vm->absPos(), startPos, 0.01);
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// ⭐ 测试 24：setRelTarget 不触发运动
// =========================================================
TEST_F(AxisViewModelCoreTest, SetRelTargetShouldNotTriggerMotion) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double startPos = vm->absPos();

    vm->setRelTarget(50.0);
    advanceTime(TICK_MS);
    advanceTime(200);

    EXPECT_EQ(vm->state(), AxisState::Idle);
    EXPECT_NEAR(vm->absPos(), startPos, 0.01);
}

// =========================================================
// ⭐ 测试 25：isLoading 反映 Policy 运行状态
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportLoadingDuringPolicyExecution) {
    // 初始：无 Policy 运行
    EXPECT_FALSE(vm->isLoading());
    EXPECT_EQ(vm->moveStep(), "Idle");

    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    vm->setAbsTarget(80.0);
    advanceTime(TICK_MS);
    vm->triggerAbsMove();

    // Policy 启动后应处于 Loading
    advanceTime(TICK_MS);

    bool foundLoading = waitUntil([this]() { return vm->isLoading(); }, 2000);
    ASSERT_TRUE(foundLoading) << "isLoading should be true while Policy is active!";

    // moveStep 应为非 Idle
    EXPECT_NE(vm->moveStep(), "Idle");

    // 等待完成
    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done);

    // 完成后 isLoading 应为 false
    EXPECT_FALSE(vm->isLoading());
    EXPECT_EQ(vm->moveStep(), "Idle");
}

// =========================================================
// ⭐ 测试 26：moveStep 反映编排步骤变化
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldReportMoveStepDuringPolicy) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    EXPECT_EQ(vm->moveStep(), "Idle");

    vm->setAbsTarget(120.0);
    advanceTime(TICK_MS);
    vm->triggerAbsMove();

    // 驱动 Policy tick 至少一帧
    advanceTime(TICK_MS);

    // moveStep 应变为非 Idle（EnsuringEnabled 或后续步骤）
    std::string step = vm->moveStep();
    EXPECT_NE(step, "Idle");

    // 等待完成
    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done);

    EXPECT_EQ(vm->moveStep(), "Idle");
}

// =========================================================
// ⭐ 测试 27：setAbsTarget 在 Disabled 状态下仍能写入
//   验证：目标设置是纯数据写入，不受轴状态限制
// =========================================================
TEST_F(AxisViewModelCoreTest, ShouldSetAbsTargetEvenWhenDisabled) {
    EXPECT_EQ(vm->state(), AxisState::Disabled);

    vm->setAbsTarget(200.0);
    advanceTime(TICK_MS);

    // 虽然轴 Disabled，但 setAbsTarget 仅写寄存器 + 排队 pending command
    // 应无错误（设置操作不应被拒绝）
    EXPECT_FALSE(vm->hasError());
}

// =========================================================
// ⭐ 测试 28：triggerAbsMove 不传 target，仅触发已设置的目标
//   验证：先 setAbsTarget(150) -> 再 triggerAbsMove -> 到达 150
//        （不是 0，也不是其他值）
// =========================================================
TEST_F(AxisViewModelCoreTest, TriggerAbsMoveShouldUsePreviouslySetTarget) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double myTarget = 175.0;
    vm->setAbsTarget(myTarget);
    advanceTime(TICK_MS);

    vm->triggerAbsMove();

    bool done = waitUntil([this]() { return vm->state() == AxisState::Disabled; }, 10000);
    ASSERT_TRUE(done);

    EXPECT_NEAR(vm->absPos(), myTarget, 0.01);
}
