#include <gtest/gtest.h>
#include "application/policy/GantryCoupleOrchestrator.h"

TEST(GantryCoupleOrchestrator,
     should_request_enable_x_when_axis_disabled)
{
    SystemContext ctx;

    ctx.registerAllStandardAxes();

    auto& x = ctx.getAxis(AxisId::X);

    EXPECT_EQ(x.state(), AxisState::Unknown);

    GantryGroup gantry(
        ctx.getAxis(AxisId::X1),
        ctx.getAxis(AxisId::X2)
    );

    GantryCoupleOrchestrator orchestrator(
        ctx,
        gantry
    );

    orchestrator.start();

    orchestrator.tick();

    EXPECT_TRUE(x.hasPendingCommand());
}