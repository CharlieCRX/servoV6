// =============================================================================
// TDD 阶段 1: ModbusSystemDriver 寄存器选择器测试
//
// 测试目标:
//   1. 验证 regCmd* 方法 — 每个 AxisId 映射到正确的命令寄存器
//   2. 验证 regFb* 方法 — 每个 AxisId 映射到正确的反馈寄存器
//   3. 验证组级寄存器 — 龙门、急停
//   4. 验证 X1/X2 fallback 到 X 的策略
//   5. 验证 stub send() / pollFeedback() 不崩溃（空实现）
//
// 测试策略 (TDD):
//   - 直接比对 & 返回值与期望的 constexpr RegisterInfo，确保是同一对象
//   - 不依赖任何 mock，纯单元测试
// =============================================================================

#include <gtest/gtest.h>
#include "infrastructure/plc/ModbusSystemDriver.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/SystemContext.h"

using namespace plc;
using namespace plc::protocol;

// ============================================================================
// 测试夹具: 创建一个 ModbusSystemDriver 实例供所有用例使用
// ============================================================================
class ModbusSystemDriverRegisterTest : public ::testing::Test {
protected:
    ModbusSystemDriver driver;
};

// ============================================================================
// 辅助宏: 验证 int16 地址精确匹配 (value semantics)
// ============================================================================
#define EXPECT_REG_EQ(actual, expected)                       \
    do {                                                      \
        EXPECT_EQ((actual).address, (expected).address)       \
            << "address mismatch";                            \
        EXPECT_EQ((actual).area, (expected).area)             \
            << "area mismatch";                               \
        EXPECT_EQ((actual).type, (expected).type)             \
            << "type mismatch";                               \
    } while (0)


// ============================================================================
// 1. 命令寄存器映射测试 (regCmd*)
// ============================================================================

// --- 1.1 regCmdEnable ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdEnableToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::X),  reg::x_axis::command::ENABLE_REQUEST);
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::X1), reg::x_axis::command::ENABLE_REQUEST);
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::X2), reg::x_axis::command::ENABLE_REQUEST);
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::Y),  reg::y_axis::command::ENABLE_REQUEST);
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::Z),  reg::z_axis::command::ENABLE_REQUEST);
    EXPECT_REG_EQ(driver.regCmdEnable(AxisId::R),  reg::r_axis::command::ENABLE_REQUEST);
}

// --- 1.2 regCmdJogFwd ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdJogFwdToCorrectRegisterPerAxis) {
    // X/X1/X2 -> M50 (X1_JOG_FORWARD)
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::X),  reg::x_axis::command::X1_JOG_FORWARD);
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::X1), reg::x_axis::command::X1_JOG_FORWARD);
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::X2), reg::x_axis::command::X1_JOG_FORWARD);
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::Y),  reg::y_axis::command::JOG_FORWARD);
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::Z),  reg::z_axis::command::JOG_FORWARD);
    EXPECT_REG_EQ(driver.regCmdJogFwd(AxisId::R),  reg::r_axis::command::JOG_FORWARD);
}

// --- 1.3 regCmdJogBwd ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdJogBwdToCorrectRegisterPerAxis) {
    // X/X1/X2 -> M51 (X1_JOG_BACKWARD)
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::X),  reg::x_axis::command::X1_JOG_BACKWARD);
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::X1), reg::x_axis::command::X1_JOG_BACKWARD);
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::X2), reg::x_axis::command::X1_JOG_BACKWARD);
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::Y),  reg::y_axis::command::JOG_BACKWARD);
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::Z),  reg::z_axis::command::JOG_BACKWARD);
    EXPECT_REG_EQ(driver.regCmdJogBwd(AxisId::R),  reg::r_axis::command::JOG_BACKWARD);
}

// --- 1.4 regCmdAbsTarget ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdAbsTargetToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::X),  reg::x_axis::command::ABS_TARGET);
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::X1), reg::x_axis::command::ABS_TARGET);
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::X2), reg::x_axis::command::ABS_TARGET);
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::Y),  reg::y_axis::command::ABS_TARGET);
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::Z),  reg::z_axis::command::ABS_TARGET);
    EXPECT_REG_EQ(driver.regCmdAbsTarget(AxisId::R),  reg::r_axis::command::ABS_TARGET);
}

// --- 1.5 regCmdRelTarget ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdRelTargetToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::X),  reg::x_axis::command::REL_TARGET);
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::X1), reg::x_axis::command::REL_TARGET);
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::X2), reg::x_axis::command::REL_TARGET);
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::Y),  reg::y_axis::command::REL_TARGET);
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::Z),  reg::z_axis::command::REL_TARGET);
    EXPECT_REG_EQ(driver.regCmdRelTarget(AxisId::R),  reg::r_axis::command::REL_TARGET);
}

// --- 1.6 regCmdAbsTrigger ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdAbsTriggerToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::X),  reg::x_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::X1), reg::x_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::X2), reg::x_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::Y),  reg::y_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::Z),  reg::z_axis::command::ABS_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdAbsTrigger(AxisId::R),  reg::r_axis::command::ABS_MOVE_TRIGGER);
}

// --- 1.7 regCmdRelTrigger ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdRelTriggerToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::X),  reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::X1), reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::X2), reg::x_axis::command::REL_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::Y),  reg::y_axis::command::REL_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::Z),  reg::z_axis::command::REL_MOVE_TRIGGER);
    EXPECT_REG_EQ(driver.regCmdRelTrigger(AxisId::R),  reg::r_axis::command::REL_MOVE_TRIGGER);
}

// --- 1.8 regCmdSetRelZero ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdSetRelZeroToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::X),  reg::x_axis::command::SET_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::Y),  reg::y_axis::command::SET_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::Z),  reg::z_axis::command::SET_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::R),  reg::r_axis::command::SET_REL_ZERO);
    // X1/X2 fallback to X
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::X1), reg::x_axis::command::SET_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdSetRelZero(AxisId::X2), reg::x_axis::command::SET_REL_ZERO);
}

// --- 1.9 regCmdClearRelZero ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdClearRelZeroToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::X),  reg::x_axis::command::CLEAR_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::Y),  reg::y_axis::command::CLEAR_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::Z),  reg::z_axis::command::CLEAR_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::R),  reg::r_axis::command::CLEAR_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::X1), reg::x_axis::command::CLEAR_REL_ZERO);
    EXPECT_REG_EQ(driver.regCmdClearRelZero(AxisId::X2), reg::x_axis::command::CLEAR_REL_ZERO);
}

// --- 1.10 regCmdClearAbsPos ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdClearAbsPosToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::X),  reg::x_axis::command::CLEAR_ABS_POS);
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::Y),  reg::y_axis::command::CLEAR_ABS_POS);
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::Z),  reg::z_axis::command::CLEAR_ABS_POS);
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::R),  reg::r_axis::command::CLEAR_ABS_POS);
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::X1), reg::x_axis::command::CLEAR_ABS_POS);
    EXPECT_REG_EQ(driver.regCmdClearAbsPos(AxisId::X2), reg::x_axis::command::CLEAR_ABS_POS);
}

// --- 1.11 regCmdJogSpeed ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdJogSpeedToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::X),  reg::x_axis::command::JOG_SPEED);
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::Y),  reg::y_axis::command::JOG_SPEED);
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::Z),  reg::z_axis::command::JOG_SPEED);
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::R),  reg::r_axis::command::JOG_SPEED);
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::X1), reg::x_axis::command::JOG_SPEED);
    EXPECT_REG_EQ(driver.regCmdJogSpeed(AxisId::X2), reg::x_axis::command::JOG_SPEED);
}

// --- 1.12 regCmdMoveSpeed ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapCmdMoveSpeedToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::X),  reg::x_axis::command::MOVE_SPEED);
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::Y),  reg::y_axis::command::MOVE_SPEED);
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::Z),  reg::z_axis::command::MOVE_SPEED);
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::R),  reg::r_axis::command::MOVE_SPEED);
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::X1), reg::x_axis::command::MOVE_SPEED);
    EXPECT_REG_EQ(driver.regCmdMoveSpeed(AxisId::X2), reg::x_axis::command::MOVE_SPEED);
}


// ============================================================================
// 2. 反馈寄存器映射测试 (regFb*)
// ============================================================================

// --- 2.1 regFbAbsPos ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbAbsPosToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::X),  reg::x_axis::feedback::ABS_POSITION);
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::X1), reg::x_axis::feedback::ABS_POSITION);
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::X2), reg::x_axis::feedback::ABS_POSITION);
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::Y),  reg::y_axis::feedback::ABS_POSITION);
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::Z),  reg::z_axis::feedback::ABS_POSITION);
    EXPECT_REG_EQ(driver.regFbAbsPos(AxisId::R),  reg::r_axis::feedback::ABS_POSITION);
}

// --- 2.2 regFbRelPos ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbRelPosToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::X),  reg::x_axis::feedback::REL_POSITION);
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::X1), reg::x_axis::feedback::REL_POSITION);
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::X2), reg::x_axis::feedback::REL_POSITION);
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::Y),  reg::y_axis::feedback::REL_POSITION);
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::Z),  reg::z_axis::feedback::REL_POSITION);
    EXPECT_REG_EQ(driver.regFbRelPos(AxisId::R),  reg::r_axis::feedback::REL_POSITION);
}

// --- 2.3 regFbState ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbStateToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbState(AxisId::X),  reg::x_axis::feedback::STATE);
    EXPECT_REG_EQ(driver.regFbState(AxisId::X1), reg::x_axis::feedback::STATE);
    EXPECT_REG_EQ(driver.regFbState(AxisId::X2), reg::x_axis::feedback::STATE);
    EXPECT_REG_EQ(driver.regFbState(AxisId::Y),  reg::y_axis::feedback::STATE);
    EXPECT_REG_EQ(driver.regFbState(AxisId::Z),  reg::z_axis::feedback::STATE);
    EXPECT_REG_EQ(driver.regFbState(AxisId::R),  reg::r_axis::feedback::STATE);
}

// --- 2.4 regFbAlarmCode ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbAlarmCodeToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::X),  reg::x_axis::feedback::ALARM_CODE);
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::X1), reg::x_axis::feedback::ALARM_CODE);
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::X2), reg::x_axis::feedback::ALARM_CODE);
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::Y),  reg::y_axis::feedback::ALARM_CODE);
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::Z),  reg::z_axis::feedback::ALARM_CODE);
    EXPECT_REG_EQ(driver.regFbAlarmCode(AxisId::R),  reg::r_axis::feedback::ALARM_CODE);
}

// --- 2.5 regFbAbsMoving ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbAbsMovingToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::X),  reg::x_axis::feedback::ABS_MOVING);
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::X1), reg::x_axis::feedback::ABS_MOVING);
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::X2), reg::x_axis::feedback::ABS_MOVING);
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::Y),  reg::y_axis::feedback::ABS_MOVING);
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::Z),  reg::z_axis::feedback::ABS_MOVING);
    EXPECT_REG_EQ(driver.regFbAbsMoving(AxisId::R),  reg::r_axis::feedback::ABS_MOVING);
}

// --- 2.6 regFbRelMoving ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbRelMovingToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::X),  reg::x_axis::feedback::REL_MOVING);
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::X1), reg::x_axis::feedback::REL_MOVING);
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::X2), reg::x_axis::feedback::REL_MOVING);
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::Y),  reg::y_axis::feedback::REL_MOVING);
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::Z),  reg::z_axis::feedback::REL_MOVING);
    EXPECT_REG_EQ(driver.regFbRelMoving(AxisId::R),  reg::r_axis::feedback::REL_MOVING);
}

// --- 2.7 regFbJogging ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbJoggingToCorrectRegisterPerAxis) {
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::X),  reg::x_axis::feedback::JOGGING);
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::X1), reg::x_axis::feedback::JOGGING);
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::X2), reg::x_axis::feedback::JOGGING);
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::Y),  reg::y_axis::feedback::JOGGING);
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::Z),  reg::z_axis::feedback::JOGGING);
    EXPECT_REG_EQ(driver.regFbJogging(AxisId::R),  reg::r_axis::feedback::JOGGING);
}


// ============================================================================
// 3. 组级寄存器映射测试 (龙门、急停)
// ============================================================================

// --- 3.1 regGantryCoupling ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapGantryCouplingToLinkageEnable) {
    EXPECT_REG_EQ(driver.regGantryCoupling(), reg::x_axis::command::LINKAGE_ENABLE);
}

// --- 3.2 regEmergencyStopTrigger ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapEmergencyStopTriggerToEstopTrigger) {
    EXPECT_REG_EQ(driver.regEmergencyStopTrigger(), reg::system_global::command::ESTOP_TRIGGER);
}

// --- 3.3 regFbEmergencyStopActive ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbEmergencyStopActiveToEstopActive) {
    EXPECT_REG_EQ(driver.regFbEmergencyStopActive(), reg::system_global::feedback::ESTOP_ACTIVE);
}

// --- 3.4 regFbGantryErrorCode ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbGantryErrorCodeToGantryErrorCode) {
    EXPECT_REG_EQ(driver.regFbGantryErrorCode(), reg::system_global::feedback::GANTRY_ERROR_CODE);
}

// --- 3.5 regFbLinkageState ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldMapFbLinkageStateToLinkageState) {
    EXPECT_REG_EQ(driver.regFbLinkageState(), reg::x_axis::feedback::LINKAGE_STATE);
}


// ============================================================================
// 4. X1/X2 fallback 到 X 的策略验证
//
// 原则:
//   - 龙门模式下，X1/X2 的指令和反馈统一使用 X 轴的寄存器
//   - 本版本不支持 X1/X2 独立控制（独立寄存器如 X1_ABS_TARGET 暂不开放）
// ============================================================================

// --- 4.1 regCmdEnable: X/X1/X2 返回同一寄存器 ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldReturnSameRegisterForXAndX1AndX2_Enable) {
    const auto& rx  = driver.regCmdEnable(AxisId::X);
    const auto& rx1 = driver.regCmdEnable(AxisId::X1);
    const auto& rx2 = driver.regCmdEnable(AxisId::X2);
    EXPECT_EQ(&rx, &rx1);
    EXPECT_EQ(&rx, &rx2);
}

// --- 4.2 regCmdAbsTarget: X/X1/X2 返回同一寄存器 ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldReturnSameRegisterForXAndX1AndX2_AbsTarget) {
    const auto& rx  = driver.regCmdAbsTarget(AxisId::X);
    const auto& rx1 = driver.regCmdAbsTarget(AxisId::X1);
    const auto& rx2 = driver.regCmdAbsTarget(AxisId::X2);
    EXPECT_EQ(&rx, &rx1);
    EXPECT_EQ(&rx, &rx2);
}

// --- 4.3 regFbAbsPos: X/X1/X2 返回同一寄存器 ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldReturnSameRegisterForXAndX1AndX2_AbsPos) {
    const auto& rx  = driver.regFbAbsPos(AxisId::X);
    const auto& rx1 = driver.regFbAbsPos(AxisId::X1);
    const auto& rx2 = driver.regFbAbsPos(AxisId::X2);
    EXPECT_EQ(&rx, &rx1);
    EXPECT_EQ(&rx, &rx2);
}

// --- 4.4 regFbState: X/X1/X2 返回同一寄存器 ---
TEST_F(ModbusSystemDriverRegisterTest, ShouldReturnSameRegisterForXAndX1AndX2_State) {
    const auto& rx  = driver.regFbState(AxisId::X);
    const auto& rx1 = driver.regFbState(AxisId::X1);
    const auto& rx2 = driver.regFbState(AxisId::X2);
    EXPECT_EQ(&rx, &rx1);
    EXPECT_EQ(&rx, &rx2);
}


// ============================================================================
// 5. 关键地址精确验证 (pessimistic test — 硬编码地址二次校验)
//
// 防止 RegisterAddressAll.h 被意外修改后选择器仍编译通过但指向错误地址
// ============================================================================

TEST_F(ModbusSystemDriverRegisterTest, ShouldMapExactAddressesForYAxisCommands) {
    // Y 轴 ENABLE_REQUEST -> Coil 1
    EXPECT_EQ(driver.regCmdEnable(AxisId::Y).address, 1u);
    EXPECT_EQ(driver.regCmdEnable(AxisId::Y).area, RegisterArea::Coil);

    // Y 轴 JOG_FORWARD -> Coil 54
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Y).address, 54u);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Y).area, RegisterArea::Coil);

    // Y 轴 ABS_TARGET -> HoldingReg 24
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Y).address, 24u);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Y).area, RegisterArea::HoldingReg);

    // Y 轴 ABS_MOVE_TRIGGER -> Coil 42
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::Y).address, 42u);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::Y).area, RegisterArea::Coil);

    // Y 轴 JOG_SPEED -> HoldingReg 1004
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::Y).address, 1004u);
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::Y).area, RegisterArea::HoldingReg);

    // Y 轴 MOVE_SPEED -> HoldingReg 1006
    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::Y).address, 1006u);
    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::Y).area, RegisterArea::HoldingReg);
}

TEST_F(ModbusSystemDriverRegisterTest, ShouldMapExactAddressesForYAxisFeedback) {
    // Y 轴 ABS_POSITION -> HoldingReg 124
    EXPECT_EQ(driver.regFbAbsPos(AxisId::Y).address, 124u);
    EXPECT_EQ(driver.regFbAbsPos(AxisId::Y).area, RegisterArea::HoldingReg);

    // Y 轴 STATE -> HoldingReg 101
    EXPECT_EQ(driver.regFbState(AxisId::Y).address, 101u);
    EXPECT_EQ(driver.regFbState(AxisId::Y).area, RegisterArea::HoldingReg);

    // Y 轴 ALARM_CODE -> HoldingReg 111
    EXPECT_EQ(driver.regFbAlarmCode(AxisId::Y).address, 111u);
    EXPECT_EQ(driver.regFbAlarmCode(AxisId::Y).area, RegisterArea::HoldingReg);

    // Y 轴 ABS_MOVING -> Coil 113
    EXPECT_EQ(driver.regFbAbsMoving(AxisId::Y).address, 113u);
    EXPECT_EQ(driver.regFbAbsMoving(AxisId::Y).area, RegisterArea::Coil);

    // Y 轴 REL_MOVING -> Coil 114
    EXPECT_EQ(driver.regFbRelMoving(AxisId::Y).address, 114u);
    EXPECT_EQ(driver.regFbRelMoving(AxisId::Y).area, RegisterArea::Coil);

    // Y 轴 JOGGING -> Coil 115
    EXPECT_EQ(driver.regFbJogging(AxisId::Y).address, 115u);
    EXPECT_EQ(driver.regFbJogging(AxisId::Y).area, RegisterArea::Coil);
}

TEST_F(ModbusSystemDriverRegisterTest, ShouldMapExactAddressesForXAchsisFallback) {
    // X/X1/X2 的 ENABLE_REQUEST 都指向 Coil 0
    EXPECT_EQ(driver.regCmdEnable(AxisId::X).address, 0u);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X1).address, 0u);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X2).address, 0u);

    // X/X1/X2 的 JOG_FORWARD 都指向 Coil 50
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X).address, 50u);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X1).address, 50u);

    // X/X1/X2 的 ABS_MOVE_TRIGGER 都指向 Coil 40
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X).address, 40u);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X1).address, 40u);

    // X/X1/X2 的 ABS_POSITION 都指向 HoldingReg 120
    EXPECT_EQ(driver.regFbAbsPos(AxisId::X).address, 120u);
    EXPECT_EQ(driver.regFbAbsPos(AxisId::X1).address, 120u);
}

TEST_F(ModbusSystemDriverRegisterTest, ShouldMapExactAddressesForGlobalRegisters) {
    // ESTOP_TRIGGER -> Coil 80
    EXPECT_EQ(driver.regEmergencyStopTrigger().address, 80u);
    EXPECT_EQ(driver.regEmergencyStopTrigger().area, RegisterArea::Coil);

    // ESTOP_ACTIVE -> Coil 130
    EXPECT_EQ(driver.regFbEmergencyStopActive().address, 130u);
    EXPECT_EQ(driver.regFbEmergencyStopActive().area, RegisterArea::Coil);

    // GANTRY_ERROR_CODE -> HoldingReg 180
    EXPECT_EQ(driver.regFbGantryErrorCode().address, 180u);
    EXPECT_EQ(driver.regFbGantryErrorCode().area, RegisterArea::HoldingReg);

    // LINKAGE_STATE -> Coil 125
    EXPECT_EQ(driver.regFbLinkageState().address, 125u);
    EXPECT_EQ(driver.regFbLinkageState().area, RegisterArea::Coil);

    // LINKAGE_ENABLE -> Coil 4
    EXPECT_EQ(driver.regGantryCoupling().address, 4u);
    EXPECT_EQ(driver.regGantryCoupling().area, RegisterArea::Coil);
}


// ============================================================================
// 6. Stub 方法冒烟测试：确保 send()/pollFeedback() 不崩溃
// ============================================================================

TEST_F(ModbusSystemDriverRegisterTest, ShouldNotCrashWhenCallingStubSend) {
    SystemCommand dummy;
    CommunicationResult result;
    // stub send() 应该是空操作，不抛异常，不崩溃
    EXPECT_NO_THROW(result = driver.send(dummy));
    // stub 返回的 CommunicationResult 默认 status 为 Sent
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
}

TEST_F(ModbusSystemDriverRegisterTest, ShouldNotCrashWhenCallingStubPollFeedback) {
    SystemContext ctx;
    // stub pollFeedback() 应该是空操作，不抛异常，不崩溃
    EXPECT_NO_THROW(driver.pollFeedback(ctx));
}
