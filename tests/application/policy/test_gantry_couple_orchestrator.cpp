#include <gtest/gtest.h>
#include "application/policy/GantryCoupleOrchestrator.h"

class GantryCoupleOrchestratorTest : public ::testing::Test {
protected:
    SystemContext ctx;
    // 使用 std::unique_ptr 或 std::optional 延迟初始化，确保在 SetUp 之后构造
    std::unique_ptr<GantryGroup> gantry;
    std::unique_ptr<GantryCoupleOrchestrator> orchestrator;

    void SetUp() override {
        ctx.registerAllStandardAxes();
        // 确保轴注册后再初始化依赖轴的组件
        gantry = std::make_unique<GantryGroup>(x1(), x2());
        orchestrator = std::make_unique<GantryCoupleOrchestrator>(ctx, *gantry);
    }

    Axis& x()  { return ctx.getAxis(AxisId::X); }
    Axis& x1() { return ctx.getAxis(AxisId::X1); }
    Axis& x2() { return ctx.getAxis(AxisId::X2); }
};

TEST_F(GantryCoupleOrchestratorTest, should_request_enable_x_when_axis_disabled) 
{
    EXPECT_EQ(x().state(), AxisState::Unknown);

    orchestrator->start(); // 注意指针使用 ->
    orchestrator->tick();

    EXPECT_TRUE(x().hasPendingCommand());

    auto cmd = x().getPendingCommand();
    EXPECT_TRUE(std::holds_alternative<EnableCommand>(cmd));
    auto enable_cmd = std::get<EnableCommand>(cmd);
    EXPECT_TRUE(enable_cmd.active);
}

// 当 X 已经 Ready(Idle), GantryCoupleOrchestrator 应该直接 Couple
TEST_F(GantryCoupleOrchestratorTest,
       should_couple_when_axis_is_idle) 
{
    // 1. 先让 X 进入 Idle 状态
    x().applyFeedback({ .state = AxisState::Idle });
    EXPECT_FALSE(gantry->isCoupled());

    // 2. 启动 Orchestrator
    orchestrator->start();
    orchestrator->tick();

    // 3. 状态推进验证
    EXPECT_TRUE(gantry->isCoupled());
}