#include <gtest/gtest.h>
#include "application/policy/GantryOrchestrator.h"
#include "application/SystemManager.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include <memory>

class GantryOrchestratorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建系统分组
        ContextRejection reason;
        manager.createGroup("TestGroup", reason);

        // 获取分组并注入驱动
        SystemContext* ctx = nullptr;
        ContextRejection mgrReason;
        manager.tryGetGroup("TestGroup", ctx, mgrReason);
        ctx->setDriver(&driver);

        // 获取龙门控制器引用（替代陈旧的 GantryGroup）
        gantryCoupling = &ctx->gantryCouplingController();
        gantryPower = &ctx->gantryPowerController();

        // 同步电机控制器状态（模拟首帧 PLC 反馈，退出 NotSynchronized）
        gantryPower->applyFeedback({ .enable = false });

        // 同步联动控制器状态（模拟首帧 PLC 反馈，退出 NotSynchronized）
        gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });

        // 创建编排器
        orchestrator = std::make_unique<GantryOrchestrator>(manager, "TestGroup");
    }

    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;
    GantryCouplingController* gantryCoupling = nullptr;
    GantryPowerController* gantryPower = nullptr;
    std::unique_ptr<GantryOrchestrator> orchestrator;
};

// ============================================================
// 完整联动流程
// ============================================================

// 1. 完整联动流程：
//    EnsuringEnabled -> WaitingEnabled -> Coupling -> WaitingCoupled -> Done
TEST_F(GantryOrchestratorTest, full_coupling_flow)
{
    // Given: 电机掉电
    EXPECT_FALSE(gantryPower->isEnabled());

    orchestrator->startCoupling();

    // Step 1: EnsuringEnabled -> 调用 GantryPowerController::requestEnable(true)
    //         下发 GantryPowerCommand -> WaitingEnabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);
    // 验证编排器已消费 pending command 并发送到驱动
    EXPECT_FALSE(gantryPower->hasPendingCommand());

    // Step 2: PLC 反馈电机已使能 -> Coupling
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Coupling);

    // Step 3: Coupling 下发联动命令 -> WaitingCoupled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingCoupled);
    EXPECT_TRUE(gantryCoupling->isCouplingRequested());

    // Step 4: PLC 反馈联动成功 -> Done
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantryCoupling->isCoupled());
}

// 2. 电机已使能时，跳过等待直接进入 Coupling（idempotent）
TEST_F(GantryOrchestratorTest, skip_waiting_when_power_already_enabled)
{
    // Given: 电机已使能
    gantryPower->applyFeedback({ .enable = true });
    EXPECT_TRUE(gantryPower->isEnabled());

    orchestrator->startCoupling();
    orchestrator->tick(); // EnsuringEnabled -> requestEnable(true) 幂等 -> WaitingEnabled
    orchestrator->tick(); // power 已 Enabled -> Coupling

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Coupling);
}

// 3. 联动时 GantryCouplingController 拦截（状态冲突） -> Error
TEST_F(GantryOrchestratorTest, error_when_state_conflict_during_coupling_request)
{
    // Given: 电机已使能，但联动控制器处于解耦请求进行中（状态冲突场景）
    gantryPower->applyFeedback({ .enable = true });
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 }); // 先联动
    gantryCoupling->requestCouple(false); // 启动解耦请求
    EXPECT_TRUE(gantryCoupling->isDecouplingRequested());

    orchestrator->startCoupling();
    orchestrator->tick(); // EnsuringEnabled -> WaitingEnabled
    orchestrator->tick(); // WaitingEnabled -> Coupling (set, not processed)
    orchestrator->tick(); // Coupling -> requestCouple(true) -> StateConflict -> Error

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<GantryRejection>(orchestrator->lastError()));
    EXPECT_EQ(std::get<GantryRejection>(orchestrator->lastError()), GantryRejection::StateConflict);
}

// 4. 等待联动中 PLC 返回位置超差错误 -> Error
TEST_F(GantryOrchestratorTest, error_when_plc_reports_position_error)
{
    // Given: 电机已使能
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled

    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 1 }); // PositionToleranceExceeded
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<GantryRejection>(orchestrator->lastError()));
    EXPECT_EQ(std::get<GantryRejection>(orchestrator->lastError()), GantryRejection::PositionToleranceExceeded);
}

// 5. 完整解耦流程：Done -> Decoupling -> WaitingDecoupled -> Done
TEST_F(GantryOrchestratorTest, full_decoupling_flow)
{
    // Given: 已联动成功
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantryCoupling->isCoupled());

    // When: 启动解耦
    orchestrator->startDecoupling();
    orchestrator->tick(); // Decoupling -> WaitingDecoupled

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);
    EXPECT_TRUE(gantryCoupling->isDecouplingRequested());

    // PLC 反馈解耦成功
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_FALSE(gantryCoupling->isCoupled());
}

// 6. 解耦等待中收到错误码不应进入 Error 状态
//    因为 PLC 解耦操作不返回错误码（始终为 None），
//    解耦流程仅依赖 isDecouplingRequested 变为 false 判断完成。
TEST_F(GantryOrchestratorTest, should_not_error_on_error_code_during_decoupling)
{
    // Given: 已联动成功
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());

    // 启动解耦 -> WaitingDecoupled
    orchestrator->startDecoupling();
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);

    // 解耦过程中 PLC 返回一个异常的错误码（模拟极端情况）
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 3 }); // X2NotEnabled
    orchestrator->tick();

    // 不应因为 errorCode 进入 Error 状态，继续等待解耦完成
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);
    EXPECT_FALSE(orchestrator->hasError());

    // 后续 PLC 反馈解耦成功
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_FALSE(gantryCoupling->isCoupled());
}

// ============================================================
// 边界 & 错误路径
// ============================================================

// 7. 分组不存在 -> 立即 Error + GroupNotFound
TEST_F(GantryOrchestratorTest, error_when_group_not_found)
{
    GantryOrchestrator badOrch(manager, "NonExistentGroup");
    badOrch.startCoupling();
    badOrch.tick();

    EXPECT_EQ(badOrch.currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<ContextRejection>(badOrch.lastError()));
    EXPECT_EQ(std::get<ContextRejection>(badOrch.lastError()), ContextRejection::GroupNotFound);
}

// 8. Done 态多次 tick 保持 Done（幂等）
TEST_F(GantryOrchestratorTest, tick_after_done_is_idempotent)
{
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick();
    orchestrator->tick();
    orchestrator->tick(); // -> WaitingCoupled
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done

    orchestrator->tick(); // 再次 tick

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
}

// 9. Error 态多次 tick 保持 Error（幂等）
TEST_F(GantryOrchestratorTest, tick_after_error_is_idempotent)
{
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled

    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 1 }); // PositionToleranceExceeded
    orchestrator->tick(); // -> Error

    orchestrator->tick(); // 再次 tick

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
}

// ============================================================
// stopCouplingAndDisable 流程
// ============================================================

// 10. 完整 stopCouplingAndDisable 流程：
//     Decoupling → WaitingDecoupled → Disabling → WaitingDisabled → Done
//     从已联动+已使能状态出发，一键解除联动并关闭使能
TEST_F(GantryOrchestratorTest, full_stop_coupling_and_disable_flow)
{
    // Given: 已联动成功（状态 B：使能 + 联动）
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantryCoupling->isCoupled());
    EXPECT_TRUE(gantryPower->isEnabled());

    // When: 启动一键解除联动+去使能
    orchestrator->stopCouplingAndDisable();

    // Step 1: Decoupling -> 下发解耦命令 -> WaitingDecoupled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);
    EXPECT_TRUE(gantryCoupling->isDecouplingRequested());

    // Step 2: PLC 反馈解耦成功 -> 进入 Disabling（因为有 m_disableAfterDecouple 标记）
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Disabling);

    // Step 3: Disabling -> 下发掉电命令 -> WaitingDisabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDisabled);
    EXPECT_FALSE(gantryPower->hasPendingCommand());

    // Step 4: PLC 反馈掉电成功 -> Done
    gantryPower->applyFeedback({ .enable = false });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_FALSE(gantryCoupling->isCoupled());
    EXPECT_FALSE(gantryPower->isEnabled());
}

// 11. stopCouplingAndDisable：电机已掉电时解耦后跳过掉电等待
//     场景：电机已被外部掉电，但联动仍保持，解耦完成后电机已经 Disabled，
//           应直接进入 Done
TEST_F(GantryOrchestratorTest, stop_coupling_and_disable_power_already_off)
{
    // Given: 已联动成功（状态 B：使能 + 联动）
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick(); // -> WaitingEnabled
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantryPower->isEnabled());

    // 外部掉电（模拟急停等情况）
    gantryPower->applyFeedback({ .enable = false });
    EXPECT_FALSE(gantryPower->isEnabled());

    // When: 启动一键解除联动+去使能
    orchestrator->stopCouplingAndDisable();
    orchestrator->tick(); // -> WaitingDecoupled
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);

    // PLC 反馈解耦成功 -> 进入 Disabling
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Disabling);

    // Disabling -> requestEnable(false) 幂等（已 Disabled）-> WaitingDisabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDisabled);

    // 电机已经 Disabled -> Done
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_FALSE(gantryPower->isEnabled());
    EXPECT_FALSE(gantryCoupling->isCoupled());
}

// ============================================================
// enableAndDecouple 流程
// ============================================================

// 12. 完整 enableAndDecouple 流程（从解耦+掉电状态出发）：
//     EnsuringEnabled → WaitingEnabled → Decoupling → WaitingDecoupled → Done
//     从状态 D 出发，已解耦状态下 requestCouple(false) 幂等，不产生 pending command，
//     因此 Decoupling → WaitingDecoupled 一步完成，直接进入 Done。
TEST_F(GantryOrchestratorTest, full_enable_and_decouple_flow_from_disabled)
{
    // Given: 电机掉电，解耦状态（状态 A 或 D）
    EXPECT_FALSE(gantryPower->isEnabled());
    EXPECT_FALSE(gantryCoupling->isCoupled());

    // When: 启动 enableAndDecouple
    orchestrator->enableAndDecouple();

    // Step 1: EnsuringEnabled -> 下发使能命令 -> WaitingEnabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);
    EXPECT_FALSE(gantryPower->hasPendingCommand());

    // Step 2: PLC 反馈使能成功 -> 进入 Decoupling（因为有 m_decoupleAfterEnable 标记）
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Decoupling);
    EXPECT_TRUE(gantryPower->isEnabled());

    // Step 3: Decoupling -> requestCouple(false) 幂等（已解耦），无 pending command
    //         -> WaitingDecoupled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);

    // Step 4: WaitingDecoupled -> isDecouplingRequested() 为 false -> Done
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantryPower->isEnabled());
    EXPECT_FALSE(gantryCoupling->isCoupled());
}

// 13. enableAndDecouple：电机已使能时跳过使能等待直接解耦
//     对应状态 B → C：已联动+使能，执行使能并解耦（幂等使能，直接进入解耦）
TEST_F(GantryOrchestratorTest, enable_and_decouple_skip_waiting_when_power_already_enabled)
{
    // Given: 电机已使能，已联动（状态 B）
    gantryPower->applyFeedback({ .enable = true });
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    EXPECT_TRUE(gantryPower->isEnabled());
    EXPECT_TRUE(gantryCoupling->isCoupled());

    // When: enableAndDecouple
    orchestrator->enableAndDecouple();

    // Step 1: EnsuringEnabled -> requestEnable(true) 幂等 -> WaitingEnabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);

    // Step 2: 电机已 Enabled -> 直接进入 Decoupling
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Decoupling);

    // Step 3: Decoupling -> WaitingDecoupled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);
    EXPECT_TRUE(gantryCoupling->isDecouplingRequested());

    // Step 4: PLC 反馈解耦成功 -> Done
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_TRUE(gantryPower->isEnabled());
    EXPECT_FALSE(gantryCoupling->isCoupled());
}

// 14. enableAndDecouple：电机使能过程中 PLC 拒绝使能（使能未成功）
//     EnsuringEnabled → WaitingEnabled，但 PLC 反馈 enable=false → 持续等待
TEST_F(GantryOrchestratorTest, enable_and_decouple_waits_when_plc_denies_enable)
{
    // Given: 电机掉电
    EXPECT_FALSE(gantryPower->isEnabled());

    // When: enableAndDecouple
    orchestrator->enableAndDecouple();
    orchestrator->tick(); // -> WaitingEnabled
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);

    // PLC 反馈电机未使能（enable=false）
    gantryPower->applyFeedback({ .enable = false });
    orchestrator->tick();

    // 应该继续等待使能，不会进入 Decoupling
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);
    EXPECT_FALSE(orchestrator->isDone());
    EXPECT_FALSE(orchestrator->hasError());
}

// 15. stopCouplingAndDisable：解耦过程中通讯错误 → Error
TEST_F(GantryOrchestratorTest, stop_coupling_and_disable_comm_error_during_decoupling)
{
    // Given: 已联动成功
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick();
    orchestrator->tick();
    orchestrator->tick();
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());

    // 模拟通讯失败：断开驱动连接（保留 driver 引用，但 send 返回 Disconnected）
    driver.disconnect();

    // When: stopCouplingAndDisable
    orchestrator->stopCouplingAndDisable();
    orchestrator->tick(); // Decoupling -> 尝试 send -> Disconnected -> Error

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(orchestrator->hasError());
}

// 16. stopCouplingAndDisable：解耦完成后掉电时通讯错误 → Error
TEST_F(GantryOrchestratorTest, stop_coupling_and_disable_comm_error_during_disabling)
{
    // Given: 已联动成功
    gantryPower->applyFeedback({ .enable = true });
    orchestrator->startCoupling();
    orchestrator->tick();
    orchestrator->tick();
    orchestrator->tick();
    gantryCoupling->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // -> Done
    EXPECT_TRUE(orchestrator->isDone());

    // When: stopCouplingAndDisable
    orchestrator->stopCouplingAndDisable();
    orchestrator->tick(); // -> WaitingDecoupled
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);

    // 解耦成功
    gantryCoupling->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick(); // -> Disabling
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Disabling);

    // 模拟通讯失败：断开驱动连接（保留 driver 引用，但 send 返回 Disconnected）
    driver.disconnect();

    // Disabling -> 尝试 send -> Disconnected -> Error
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(orchestrator->hasError());
}
