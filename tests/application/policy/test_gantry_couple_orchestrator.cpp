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


TEST_F(GantryCoupleOrchestratorTest, should_enter_waiting_coupled_after_requesting_couple)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); 
    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick(); // -> Coupling

    orchestrator->tick(); // 发送意图 requestCouple() 并推进到 WaitingCoupled

    EXPECT_TRUE(gantry->isCouplingRequested());
    EXPECT_FALSE(gantry->isCoupled());
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::WaitingCoupled);
}

TEST_F(GantryCoupleOrchestratorTest, should_wait_coupled_feedback_before_done)
{
    x().applyFeedback({ .state = AxisState::Disabled });
    orchestrator->start();
    orchestrator->tick(); 
    x().applyFeedback({ .state = AxisState::Idle });
    orchestrator->tick(); // -> Coupling
    orchestrator->tick(); // -> WaitingCoupled

    // 第一帧：PLC 还没有返回，卡在 WaitingCoupled
    orchestrator->tick(); 
    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::WaitingCoupled);

    // 第二帧：模拟外部 PLC (或底层总线通讯) 确认关联完成，调用 applyCoupledFeedback()
    gantry->applyCoupledFeedback(); 
    
    // 再次 tick，Orchestrator 终于检测到 isCoupled() 为 true，推进进入 Done
    orchestrator->tick(); 

    EXPECT_EQ(orchestrator->currentStep(), GantryCoupleOrchestrator::Step::Done);
}