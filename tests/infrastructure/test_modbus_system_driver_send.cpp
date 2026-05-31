/**
 * @file test_modbus_system_driver_send.cpp
 * @brief 阶段四 TDD: ModbusSystemDriver::send() 命令分派单元测试
 *
 * 测试依据: 《阶段四：命令分派单元测试 —— TDD 详细开发文档》§5
 *
 * 测试目标:
 *   验证 send(SystemCommand) 能正确根据 variant 类型,
 *   将命令分派到正确的 PlcDevice::writeBool / writeFloat 调用,
 *   包括正确选择 AxisId→RegisterInfo 映射。
 *
 * SystemCommand = variant<AxisCommandWithId, GantryCouplingCommand, GantryPowerCommand, EmergencyStopCommand>
 * AxisCommand    = variant<monostate, JogCommand, MoveCommand, StopCommand, ZeroAbsoluteCommand, ...>
 *
 * 测试覆盖:
 *   - Level 型轴命令 (Enable, Jog, Stop)
 *   - D-Register 型轴命令 (SetJogVelocity, SetMoveVelocity, SetAbsTarget, SetRelTarget)
 *   - EdgeTrigger 型轴命令 (TriggerAbsMove, TriggerRelMove, ZeroAbsolute, SetRelativeZero, ClearRelativeZero)
 *   - 组级命令 (GantryCoupling, GantryPower, EmergencyStop)
 *   - 边界条件 (未注入设备, monostate, 写失败传播)
 */

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/ISystemDriver.h"
#include "tests/infrastructure/mock/MockPlcDevice.h"

#include "domain/entity/Axis.h"
#include "domain/entity/AxisId.h"
#include "domain/command/SystemCommand.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"

#include <memory>

using ::testing::_;
using ::testing::Return;
using ::testing::StrictMock;

using namespace plc;

// ═════════════════════════════════════════════════════════════════════
// 测试辅助
// ═════════════════════════════════════════════════════════════════════

namespace {
    CommunicationResult ResultOk() { return CommunicationResult::Sent(); }
    CommunicationResult ResultDisconnected() { return CommunicationResult::Disconnected(); }
    CommunicationResult ResultNetworkError() {
        return {CommunicationResult::Status::NetworkError, 0, "mock error"};
    }
}  // namespace

// ═════════════════════════════════════════════════════════════════════
// 测试夹具
// ═════════════════════════════════════════════════════════════════════

class ModbusSystemDriverSendTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver_.setDevice(&mock_device_);
    }

    StrictMock<MockPlcDevice> mock_device_;
    ModbusSystemDriver driver_;
};


// ═════════════════════════════════════════════════════════════════════
// Part 1: Level 型轴命令 — writeBool
// ═════════════════════════════════════════════════════════════════════

// ---------- EnableCommand ----------

TEST_F(ModbusSystemDriverSendTest, EnableCommandWritesCoil) {
    SystemCommand cmd{AxisCommandWithId{AxisId::X, EnableCommand{true}}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

TEST_F(ModbusSystemDriverSendTest, DisableCommandWritesCoilFalse) {
    SystemCommand cmd{AxisCommandWithId{AxisId::Y, EnableCommand{false}}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::y_axis::command::ENABLE_REQUEST, false))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- JogCommand Forward / Backward ----------

TEST_F(ModbusSystemDriverSendTest, JogForwardCommandWritesCoil) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::X, JogCommand{Direction::Forward, true}
    }};
    // X → regCmdJogFwd returns X1_JOG_FORWARD (M50)
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::X1_JOG_FORWARD, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

TEST_F(ModbusSystemDriverSendTest, JogBackwardCommandWritesCoil) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Y, JogCommand{Direction::Backward, true}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::y_axis::command::JOG_BACKWARD, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- Jog deactivate (方向停，只写对应方向 false) ----------

TEST_F(ModbusSystemDriverSendTest, JogDeactivateForwardOnlyClearsForwardCoil) {
    // JogCommand{Forward, active=false}: 仅写 JOG_FWD=false
    SystemCommand cmd{AxisCommandWithId{
        AxisId::X, JogCommand{Direction::Forward, false}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(ResultOk()));
    // StrictMock: 不应有 JOG_BWD 调用
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- StopCommand (同时清除两个 Jog Coil) ----------

TEST_F(ModbusSystemDriverSendTest, StopCommandWritesBothJogCoilsOff) {
    SystemCommand cmd{AxisCommandWithId{AxisId::X, StopCommand{}}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(ResultOk()));
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::X1_JOG_BACKWARD, false))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- Monostate 空命令 ----------

TEST_F(ModbusSystemDriverSendTest, MonostateEmptyCommandNoWrite) {
    SystemCommand cmd{AxisCommandWithId{AxisId::X, std::monostate{}}};
    // StrictMock: 不应有任何 write 调用
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}


// ═════════════════════════════════════════════════════════════════════
// Part 1: D-Register 型轴命令 — writeFloat
// ═════════════════════════════════════════════════════════════════════

// ---------- SetJogVelocityCommand ----------

TEST_F(ModbusSystemDriverSendTest, SetJogVelocityWritesFloat) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Y, SetJogVelocityCommand{150.0}
    }};
    EXPECT_CALL(mock_device_,
                writeFloat(reg::y_axis::command::JOG_SPEED, 150.0f))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- SetMoveVelocityCommand ----------

TEST_F(ModbusSystemDriverSendTest, SetMoveVelocityWritesFloat) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Z, SetMoveVelocityCommand{200.0}
    }};
    EXPECT_CALL(mock_device_,
                writeFloat(reg::z_axis::command::MOVE_SPEED, 200.0f))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- SetAbsTargetCommand (仅写 D 寄存器，不触发) ----------

TEST_F(ModbusSystemDriverSendTest, SetAbsTargetWritesOnlyDRegister) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::X, SetAbsTargetCommand{123.45}
    }};
    EXPECT_CALL(mock_device_,
                writeFloat(reg::x_axis::command::ABS_TARGET, 123.45f))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);
}

// ---------- SetRelTargetCommand (仅写 D 寄存器，不触发) ----------

TEST_F(ModbusSystemDriverSendTest, SetRelTargetWritesOnlyDRegister) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Y, SetRelTargetCommand{-50.0}
    }};
    EXPECT_CALL(mock_device_,
                writeFloat(reg::y_axis::command::REL_TARGET, -50.0f))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);
}


// ═════════════════════════════════════════════════════════════════════
// Part 2: EdgeTrigger 型轴命令 — sendEdgeTrigger (writeBool true + enqueue)
// ═════════════════════════════════════════════════════════════════════

// ---------- TriggerAbsMoveCommand (仅 EdgeTrigger) ----------

TEST_F(ModbusSystemDriverSendTest, TriggerAbsMoveOnlyEdgeTrigger) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::X, TriggerAbsMoveCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 1u);
}

// ---------- TriggerRelMoveCommand (仅 EdgeTrigger) ----------

TEST_F(ModbusSystemDriverSendTest, TriggerRelMoveOnlyEdgeTrigger) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Z, TriggerRelMoveCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::z_axis::command::REL_MOVE_TRIGGER, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 1u);
}

// ---------- ZeroAbsoluteCommand (CLEAR_ABS_POS) ----------

TEST_F(ModbusSystemDriverSendTest, ZeroAbsoluteCommandEdgeTrigger) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::X, ZeroAbsoluteCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::CLEAR_ABS_POS, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 1u);
}

// ---------- SetRelativeZeroCommand ----------

TEST_F(ModbusSystemDriverSendTest, SetRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Y, SetRelativeZeroCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::y_axis::command::SET_REL_ZERO, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 1u);
}

// ---------- ClearRelativeZeroCommand ----------

TEST_F(ModbusSystemDriverSendTest, ClearRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::R, ClearRelativeZeroCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::r_axis::command::CLEAR_REL_ZERO, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 1u);
}

// ---------- EdgeTrigger 写入失败不入队 ----------

TEST_F(ModbusSystemDriverSendTest, EdgeTriggerWriteFailureDoesNotEnqueue) {
    SystemCommand cmd{AxisCommandWithId{
        AxisId::Y, TriggerAbsMoveCommand{}
    }};
    EXPECT_CALL(mock_device_,
                writeBool(reg::y_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(ResultNetworkError()));
    auto result = driver_.send(cmd);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);
}


// ═════════════════════════════════════════════════════════════════════
// Part 2: 组级命令
// ═════════════════════════════════════════════════════════════════════

// ---------- GantryCouplingCommand 龙门联动 ----------

TEST_F(ModbusSystemDriverSendTest, GantryCouplingCommandLevelType) {
    SystemCommand cmd{GantryCouplingCommand{true}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::LINKAGE_ENABLE, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);  // Level 型，不入队
}

// ---------- GantryCouplingCommand 龙门解耦 ----------

TEST_F(ModbusSystemDriverSendTest, GantryUncouplingCommandLevelType) {
    SystemCommand cmd{GantryCouplingCommand{false}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::LINKAGE_ENABLE, false))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- GantryPowerCommand 龙门上电 (共用 X 轴 M0) ----------

TEST_F(ModbusSystemDriverSendTest, GantryPowerCommandSharesM0WithXEnable) {
    SystemCommand cmd{GantryPowerCommand{true}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);
}

// ---------- GantryPowerCommand 龙门掉电 ----------

TEST_F(ModbusSystemDriverSendTest, GantryPowerOffCommand) {
    SystemCommand cmd{GantryPowerCommand{false}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::ENABLE_REQUEST, false))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}

// ---------- EmergencyStopCommand 急停 (Level 型) ----------

TEST_F(ModbusSystemDriverSendTest, EmergencyStopCommandLevelType) {
    SystemCommand cmd{EmergencyStopCommand{true}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::system_global::command::ESTOP_TRIGGER, true))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_.pendingEdgeCount(), 0u);  // Level 型，不入队
}

// ---------- EmergencyStopCommand 解除急停 (Level 型) ----------

TEST_F(ModbusSystemDriverSendTest, ReleaseEmergencyStopCommandLevelType) {
    SystemCommand cmd{EmergencyStopCommand{false}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::system_global::command::ESTOP_TRIGGER, false))
        .WillOnce(Return(ResultOk()));
    auto result = driver_.send(cmd);
    EXPECT_TRUE(result.ok());
}


// ═════════════════════════════════════════════════════════════════════
// Part 2: 边界条件 & 错误处理
// ═════════════════════════════════════════════════════════════════════

// ---------- 未注入 PlcDevice ----------

TEST_F(ModbusSystemDriverSendTest, SendWithoutDeviceReturnsDisconnected) {
    ModbusSystemDriver driver_no_device;
    SystemCommand cmd{AxisCommandWithId{AxisId::Y, EnableCommand{true}}};
    auto result = driver_no_device.send(cmd);
    EXPECT_TRUE(result.isDisconnected());
}

// ---------- 写失败传播 ----------

TEST_F(ModbusSystemDriverSendTest, WriteFailurePropagatesError) {
    SystemCommand cmd{AxisCommandWithId{AxisId::X, EnableCommand{true}}};
    EXPECT_CALL(mock_device_,
                writeBool(reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(ResultNetworkError()));
    auto result = driver_.send(cmd);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::NetworkError);
}
