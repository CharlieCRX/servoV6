/**
 * T3.2 FakeGantryCommandPort 测试
 *
 * 测试范围：
 *   - TS3.2.1: 单轴 Jog 命令已委托到正确的 FakePLC 轴
 *   - TS3.2.2: 单轴 MoveAbsolute 命令已委托
 *   - TS3.2.3: 单轴 MoveRelative 命令已委托
 *   - TS3.2.4: 单轴 Stop 命令已委托
 *   - TS3.2.5: Gantry Jog — X1/X2 方向镜像
 *   - TS3.2.6: Gantry MoveAbsolute — X1/X2 位置镜像
 *   - TS3.2.7: Gantry MoveRelative — X1/X2 位移镜像
 *   - TS3.2.8: Gantry Stop — 同时停止双轴
 *   - TS3.2.9: isAxisSlotFree — 空闲判定
 *   - TS3.2.10: 非法轴号被拒绝
 */

#include <gtest/gtest.h>
#include "infrastructure/FakeGantryCommandPort.h"
#include "domain/value/MotionDirection.h"

class FakeGantryCommandPortTest : public ::testing::Test {
protected:
    FakePLC m_plc;
    FakeGantryCommandPort m_port{m_plc};

    void SetUp() override {
        // 初始给 X1/X2 上电使轴进入 Idle
        m_plc.onCommand(AxisId::X1, EnableCommand{true});
        m_plc.tick(200); // 等待 Enable 完成
        m_plc.onCommand(AxisId::X2, EnableCommand{true});
        m_plc.tick(200);
    }
};

// ============================================================
// TS3.2.1: 单轴 Jog 命令已委托到正确的 FakePLC 轴
// ============================================================
TEST_F(FakeGantryCommandPortTest, JogAxis_X1_Forward_DelegatesToX1) {
    EXPECT_TRUE(m_port.jogAxis(1, MotionDirection::Forward));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Jogging);
    // X2 不应受影响
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Idle);
}

TEST_F(FakeGantryCommandPortTest, JogAxis_X2_Backward_DelegatesToX2) {
    EXPECT_TRUE(m_port.jogAxis(2, MotionDirection::Backward));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Jogging);
    // X1 不应受影响
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Idle);
}

// ============================================================
// TS3.2.2: 单轴 MoveAbsolute 命令已委托
// ============================================================
TEST_F(FakeGantryCommandPortTest, MoveAbsoluteAxis_X1_DelegatesToX1) {
    EXPECT_TRUE(m_port.moveAbsoluteAxis(1, 100.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::MovingAbsolute);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Idle);
}

TEST_F(FakeGantryCommandPortTest, MoveAbsoluteAxis_X2_DelegatesToX2) {
    EXPECT_TRUE(m_port.moveAbsoluteAxis(2, -50.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::MovingAbsolute);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Idle);
}

// ============================================================
// TS3.2.3: 单轴 MoveRelative 命令已委托
// ============================================================
TEST_F(FakeGantryCommandPortTest, MoveRelativeAxis_X1_DelegatesToX1) {
    EXPECT_TRUE(m_port.moveRelativeAxis(1, 20.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::MovingRelative);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Idle);
}

TEST_F(FakeGantryCommandPortTest, MoveRelativeAxis_X2_DelegatesToX2) {
    EXPECT_TRUE(m_port.moveRelativeAxis(2, -10.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::MovingRelative);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Idle);
}

// ============================================================
// TS3.2.4: 单轴 Stop 命令已委托
// ============================================================
TEST_F(FakeGantryCommandPortTest, StopAxis_X1_StopsX1Only) {
    // 先让 X1 Jogging
    m_plc.onCommand(AxisId::X1, JogCommand{Direction::Forward, true});
    m_plc.onCommand(AxisId::X2, JogCommand{Direction::Forward, true});

    EXPECT_TRUE(m_port.stopAxis(1));
    // 直接 forceState 不会经过 FakePLC 内部，我们需要通过 tick 来看
    // 但这里 stopAxis 发出 StopCommand，Axis 内部的 stop_requested 会被设置
    // 然后需要 tick 来使其生效
    m_plc.tick(10);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Idle);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Jogging);
}

// ============================================================
// TS3.2.5: Gantry Jog — X1/X2 方向镜像
// ============================================================
TEST_F(FakeGantryCommandPortTest, JogGantry_Forward_X1Forward_X2Backward) {
    EXPECT_TRUE(m_port.jogGantry(MotionDirection::Forward));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Jogging);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Jogging);
}

// ============================================================
// TS3.2.6: Gantry MoveAbsolute — X1/X2 位置镜像
// ============================================================
TEST_F(FakeGantryCommandPortTest, MoveAbsoluteGantry_SendsToBothAxes) {
    EXPECT_TRUE(m_port.moveAbsoluteGantry(100.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::MovingAbsolute);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::MovingAbsolute);
}

// ============================================================
// TS3.2.7: Gantry MoveRelative — X1/X2 位移镜像
// ============================================================
TEST_F(FakeGantryCommandPortTest, MoveRelativeGantry_SendsToBothAxes) {
    EXPECT_TRUE(m_port.moveRelativeGantry(30.0));
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::MovingRelative);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::MovingRelative);
}

// ============================================================
// TS3.2.8: Gantry Stop — 同时停止双轴
// ============================================================
TEST_F(FakeGantryCommandPortTest, StopGantry_StopsBothAxes) {
    // 让两个轴都 Jogging
    m_plc.onCommand(AxisId::X1, JogCommand{Direction::Forward, true});
    m_plc.onCommand(AxisId::X2, JogCommand{Direction::Forward, true});

    EXPECT_TRUE(m_port.stopGantry());
    m_plc.tick(10);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X1).state, AxisState::Idle);
    EXPECT_EQ(m_plc.getFeedback(AxisId::X2).state, AxisState::Idle);
}

// ============================================================
// TS3.2.9: isAxisSlotFree — 空闲判定
// ============================================================
TEST_F(FakeGantryCommandPortTest, SlotFree_WhenAxisIdle) {
    EXPECT_TRUE(m_port.isAxisSlotFree(1));
    EXPECT_TRUE(m_port.isAxisSlotFree(2));
}

TEST_F(FakeGantryCommandPortTest, SlotNotFree_WhenAxisMoving) {
    m_plc.onCommand(AxisId::X1, MoveCommand{MoveType::Absolute, 50.0});
    EXPECT_FALSE(m_port.isAxisSlotFree(1));
    EXPECT_TRUE(m_port.isAxisSlotFree(2)); // X2 仍然空闲
}

// ============================================================
// TS3.2.10: 非法轴号被拒绝
// ============================================================
TEST_F(FakeGantryCommandPortTest, InvalidAxis_ReturnsFalse) {
    EXPECT_FALSE(m_port.jogAxis(0, MotionDirection::Forward));
    EXPECT_FALSE(m_port.jogAxis(3, MotionDirection::Forward));
    EXPECT_FALSE(m_port.moveAbsoluteAxis(0, 10.0));
    EXPECT_FALSE(m_port.moveAbsoluteAxis(3, 10.0));
    EXPECT_FALSE(m_port.moveRelativeAxis(0, 5.0));
    EXPECT_FALSE(m_port.moveRelativeAxis(3, 5.0));
    EXPECT_FALSE(m_port.stopAxis(0));
    EXPECT_FALSE(m_port.stopAxis(3));
    EXPECT_FALSE(m_port.isAxisSlotFree(0));
    EXPECT_FALSE(m_port.isAxisSlotFree(3));
}
