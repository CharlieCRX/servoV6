#include <gtest/gtest.h>
#include "entity/Axis.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "application/axis/MoveAbsoluteUseCase.h"
#include "application/axis/AxisSyncService.h"

TEST(SystemIntegrationTest, FullMoveAbsoluteLifeCycle) {
    
    Axis axis;
    FakePLC plc;
    FakeAxisDriver driver(plc);
    MoveAbsoluteUseCase moveUseCase(driver);
    AxisSyncService syncService;

    plc.forceState(AxisState::Idle); 
    plc.setSimulatedMoveVelocity(50.0);
    syncService.sync(axis, plc);     
    
    EXPECT_EQ(axis.state(), AxisState::Idle);

    RejectionReason reason = moveUseCase.execute(axis, 100.0);
    EXPECT_EQ(reason, RejectionReason::None); 

    plc.tick(1000); 
    syncService.sync(axis, plc); 

    EXPECT_EQ(axis.state(), AxisState::MovingAbsolute);
    EXPECT_NEAR(axis.currentAbsolutePosition(), 50.0, 0.001);

    plc.tick(1000); 
    syncService.sync(axis, plc);

    EXPECT_EQ(axis.state(), AxisState::Idle); 
    EXPECT_NEAR(axis.currentAbsolutePosition(), 100.0, 0.001);
    EXPECT_FALSE(axis.hasPendingCommand()); 
    
}