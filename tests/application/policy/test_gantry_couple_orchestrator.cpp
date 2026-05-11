#include <gtest/gtest.h>
#include "application/policy/GantryCoupleOrchestrator.h"
#include <memory>
#include <variant>

class GantryCoupleOrchestratorTest : public ::testing::Test {
protected:
    SystemContext ctx;
    std::unique_ptr<GantryGroup> gantry;
    std::unique_ptr<GantryCoupleOrchestrator> orchestrator;

    void SetUp() override {
        ctx.registerAllStandardAxes();
        gantry = std::make_unique<GantryGroup>(x1(), x2());
        orchestrator = std::make_unique<GantryCoupleOrchestrator>(ctx, *gantry);
    }

    Axis& x()  { return ctx.getAxis(AxisId::X); }
    Axis& x1() { return ctx.getAxis(AxisId::X1); }
    Axis& x2() { return ctx.getAxis(AxisId::X2); }
};

// 1. 测试：只有明确为 Disabled 时，才发送 Enable 并进入等待
TEST_F(GantryCoupleOrchestratorTest, should_request_enable_x_when_axis_disabled) 
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start(); 
    orchestrator->tick(); 
    EXPECT_TRUE(x().hasPendingCommand());
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::WaitingAxisReady);
}

// 2. 测试：状态非法时，进入 Error
TEST_F(GantryCoupleOrchestratorTest, should_enter_error_when_axis_state_is_invalid_for_enabling) 
{
    EXPECT_EQ(x().state(), AxisState::Unknown);
    orchestrator->start(); 
    orchestrator->tick(); 
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::Error);
}

// 3. 测试：发了 Enable 后应死等物理 Ready
TEST_F(GantryCoupleOrchestratorTest, should_wait_axis_ready_after_request_enable)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); // -> WaitingAxisReady
    orchestrator->tick(); // 死等
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::WaitingAxisReady);
}

// 4. 测试：等待中发现 Idle，进入 Coupling
TEST_F(GantryCoupleOrchestratorTest, should_enter_coupling_when_axis_ready)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); // -> WaitingAxisReady

    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick(); // WaitingAxisReady -> Coupling

    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::Coupling);
}

// ⭐ 5. 新增修改测试：在 Coupling 状态调用 couple() 后，应该进入等待状态（WaitingCoupled）
TEST_F(GantryCoupleOrchestratorTest, should_enter_waiting_coupled_after_issuing_couple_command)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); 
    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick(); // -> Coupling

    orchestrator->tick(); // 发送 couple 指令，并推进到 WaitingCoupled

    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::WaitingCoupled);
}

// ⭐ 6. 新增测试：在 WaitingCoupled 状态下，只有当底层反馈真正 isCoupled 时，才进入 Done
TEST_F(GantryCoupleOrchestratorTest, should_enter_done_only_when_gantry_is_actually_coupled)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); 
    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled

    // 假设此处底层反馈 gantry->isCoupled() 终于为 true 了
    // (如果你的 fake GantryGroup 调用 couple 后立刻就为 true，那么再 tick 一次就会进 Done)
    orchestrator->tick(); // WaitingCoupled -> Done

    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::Done);
}

// 7. 测试：如果开机已经是 Idle，直接跳过 Enable 阶段进入 Coupling
TEST_F(GantryCoupleOrchestratorTest, should_couple_directly_when_axis_is_already_idle) 
{
    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->start();
    orchestrator->tick(); // -> Coupling
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::Coupling);
}