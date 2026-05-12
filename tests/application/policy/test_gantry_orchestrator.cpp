#include <gtest/gtest.h>
#include "application/policy/GantryOrchestrator.h"
#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryGroup.h"
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

        // 获取组内的对象引用
        Axis* x = nullptr;
        ContextRejection ctxReason;
        ctx->tryGetAxis(AxisId::X, x, ctxReason);
        xAxis = x;

        gantry = &ctx->gantry();

        // 创建编排器
        orchestrator = std::make_unique<GantryOrchestrator>(manager, "TestGroup");
    }

    FakePLC plc;
    FakeAxisDriver driver{plc};
    SystemManager manager;
    Axis* xAxis = nullptr;
    GantryGroup* gantry = nullptr;
    std::unique_ptr<GantryOrchestrator> orchestrator;
};

// ============================================================
// 完整联动流程
// ============================================================

// 1. 完整联动流程：EnsuringEnabled → WaitingEnabled → Coupling → WaitingCoupled → Done
TEST_F(GantryOrchestratorTest, full_coupling_flow)
{
    // Given: X 轴 Disabled
    xAxis->applyFeedback({ .state = AxisState::Disabled });

    orchestrator->startCoupling();

    // Step 1: EnsuringEnabled → 调用 EnableUseCase，推进到 WaitingEnabled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingEnabled);

    // Step 2: X 轴进入 Idle → Coupling
    xAxis->applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Coupling);

    // Step 3: Coupling 下发联动命令 → WaitingCoupled
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingCoupled);
    EXPECT_TRUE(gantry->isCouplingRequested());

    // Step 4: PLC 反馈联动成功 → Done
    gantry->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick();
    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_TRUE(orchestrator->isDone());
}

// 2. X 轴已经 Idle 时，一帧内推进到 Coupling
TEST_F(GantryOrchestratorTest, skip_waiting_when_x_already_idle)
{
    xAxis->applyFeedback({ .state = AxisState::Idle });

    orchestrator->startCoupling();
    orchestrator->tick(); // EnsuringEnabled → EnableUseCase 幂等 → WaitingEnabled
    orchestrator->tick(); // X 已经 Idle → Coupling

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Coupling);
}

// 3. 等待使能中 X 轴进入 Error → Error
TEST_F(GantryOrchestratorTest, error_when_x_enters_error_during_waiting)
{
    xAxis->applyFeedback({ .state = AxisState::Disabled });
    orchestrator->startCoupling();
    orchestrator->tick(); // → WaitingEnabled

    xAxis->applyFeedback({ .state = AxisState::Error });
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(orchestrator->hasError());
}

// 4. 联动时 GantryGroup 拦截（X1/X2 轴状态错误） → Error
//    通过 GantryGroup.applyFeedback 模拟 PLC 在协作检查中直接上报错误
TEST_F(GantryOrchestratorTest, error_when_gantry_rejects_coupling)
{
    xAxis->applyFeedback({ .state = AxisState::Idle });

    orchestrator->startCoupling();
    orchestrator->tick(); // → WaitingEnabled
    orchestrator->tick(); // → Coupling

    // Coupling step 调用 gantry.requestCouple(true) 并下发命令
    orchestrator->tick(); // → WaitingCoupled

    // PLC 反馈协作错误（如 X1 未使能）
    gantry->applyFeedback({ .isCoupled = false, .errorCode = 2 }); // X1NotEnabled
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<GantryRejection>(orchestrator->lastError()));
    EXPECT_EQ(std::get<GantryRejection>(orchestrator->lastError()), GantryRejection::X1NotEnabled);
}

// 5. 等待联动中 PLC 返回位置超差错误 → Error
TEST_F(GantryOrchestratorTest, error_when_plc_reports_position_error)
{
    xAxis->applyFeedback({ .state = AxisState::Idle });
    orchestrator->startCoupling();
    orchestrator->tick(); // → WaitingEnabled
    orchestrator->tick(); // → Coupling
    orchestrator->tick(); // → WaitingCoupled

    gantry->applyFeedback({ .isCoupled = false, .errorCode = 1 }); // PositionToleranceExceeded
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
    EXPECT_TRUE(std::holds_alternative<GantryRejection>(orchestrator->lastError()));
    EXPECT_EQ(std::get<GantryRejection>(orchestrator->lastError()), GantryRejection::PositionToleranceExceeded);
}

// ============================================================
// 完整解耦流程
// ============================================================

// 6. 完整解耦流程：Done → Decoupling → WaitingDecoupled → Done
TEST_F(GantryOrchestratorTest, full_decoupling_flow)
{
    // Given: 已联动成功
    xAxis->applyFeedback({ .state = AxisState::Idle });
    orchestrator->startCoupling();
    orchestrator->tick(); // → WaitingEnabled
    orchestrator->tick(); // → Coupling
    orchestrator->tick(); // → WaitingCoupled
    gantry->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // → Done
    EXPECT_TRUE(orchestrator->isDone());
    EXPECT_TRUE(gantry->isCoupled());

    // When: 启动解耦
    orchestrator->startDecoupling();
    orchestrator->tick(); // Decoupling → WaitingDecoupled

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::WaitingDecoupled);
    EXPECT_TRUE(gantry->isDecouplingRequested());

    // PLC 反馈解耦成功
    gantry->applyFeedback({ .isCoupled = false, .errorCode = 0 });
    orchestrator->tick();

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
    EXPECT_FALSE(gantry->isCoupled());
}

// ============================================================
// 边界 & 错误路径
// ============================================================

// 7. 分组不存在 → 立即 Error + GroupNotFound
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
    xAxis->applyFeedback({ .state = AxisState::Idle });
    orchestrator->startCoupling();
    orchestrator->tick();
    orchestrator->tick();
    orchestrator->tick(); // → WaitingCoupled
    gantry->applyFeedback({ .isCoupled = true, .errorCode = 0 });
    orchestrator->tick(); // → Done

    orchestrator->tick(); // 再次 tick

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Done);
}

// 9. Error 态多次 tick 保持 Error（幂等）
TEST_F(GantryOrchestratorTest, tick_after_error_is_idempotent)
{
    xAxis->applyFeedback({ .state = AxisState::Disabled });
    orchestrator->startCoupling();
    orchestrator->tick(); // → WaitingEnabled

    xAxis->applyFeedback({ .state = AxisState::Error });
    orchestrator->tick(); // → Error

    orchestrator->tick(); // 再次 tick

    EXPECT_EQ(orchestrator->currentStep(), GantryOrchestrator::Step::Error);
}
