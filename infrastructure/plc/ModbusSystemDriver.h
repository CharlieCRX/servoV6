#pragma once

#include "infrastructure/ISystemDriver.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "domain/entity/AxisId.h"
#include <cstdint>

namespace plc {

/**
 * @brief Modbus 系统驱动 —— 统一命令/反馈的门面
 *
 * 职责：
 *   1. regCmd* / regFb* : 寄存器选择器（AxisId → RegisterInfo 映射）
 *   2. send(SystemCommand) : 命令分派到 PlcDevice 写入
 *   3. pollFeedback(SystemContext&) : 反馈轮询及领域实体分发
 *
 * 阶段一仅实现寄存器选择器（regCmd* / regFb*），其余方法为 stub。
 */
class ModbusSystemDriver : public ISystemDriver {
public:
    ModbusSystemDriver() = default;
    ~ModbusSystemDriver() override = default;

    // =========================================================================
    // 阶段一：寄存器选择器 —— 命令类（regCmd*）
    // 每个方法根据 AxisId 返回对应的 RegisterInfo 引用
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regCmdEnable(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogFwd(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogBwd(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdAbsTarget(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdRelTarget(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdAbsTrigger(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdRelTrigger(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdSetRelZero(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdClearRelZero(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdClearAbsPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogSpeed(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdMoveSpeed(AxisId id) const;

    // =========================================================================
    // 阶段一：寄存器选择器 —— 反馈类（regFb*）
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regFbAbsPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbRelPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbState(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbAlarmCode(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbAbsMoving(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbRelMoving(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbJogging(AxisId id) const;

    // =========================================================================
    // 阶段一：寄存器选择器 —— 组级命令/反馈
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regGantryCoupling() const;
    [[nodiscard]] const protocol::RegisterInfo& regEmergencyStopTrigger() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbEmergencyStopActive() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbGantryErrorCode() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbLinkageState() const;

    // =========================================================================
    // ISystemDriver 接口 (阶段一 stub，后续阶段实现)
    // =========================================================================

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;
};

// =============================================================================
// 内联实现：寄存器选择器
//
// 原则：
//   1. X1 / X2 在当前版本下 fallback 到 X（龙门模式）
//   2. 不暴露 HOME_TRIGGER（本版本不开放回原点）
//   3. 使用 RegisterAddressAll.h 中已定义的 constexpr RegisterInfo
// =============================================================================

// ---------- 命令类 ----------

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdEnable(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ENABLE_REQUEST;
        case AxisId::Y:  return reg::y_axis::command::ENABLE_REQUEST;
        case AxisId::Z:  return reg::z_axis::command::ENABLE_REQUEST;
        case AxisId::R:  return reg::r_axis::command::ENABLE_REQUEST;
    }
    // unreachable, but avoid compiler warning
    return reg::x_axis::command::ENABLE_REQUEST;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogFwd(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::X1_JOG_FORWARD;  // M50 (X1 正转)
        case AxisId::Y:  return reg::y_axis::command::JOG_FORWARD;     // M54
        case AxisId::Z:  return reg::z_axis::command::JOG_FORWARD;     // M56
        case AxisId::R:  return reg::r_axis::command::JOG_FORWARD;     // M58
    }
    return reg::x_axis::command::X1_JOG_FORWARD;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogBwd(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::X1_JOG_BACKWARD; // M51 (X1 反转)
        case AxisId::Y:  return reg::y_axis::command::JOG_BACKWARD;    // M55
        case AxisId::Z:  return reg::z_axis::command::JOG_BACKWARD;    // M57
        case AxisId::R:  return reg::r_axis::command::JOG_BACKWARD;    // M59
    }
    return reg::x_axis::command::X1_JOG_BACKWARD;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdAbsTarget(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ABS_TARGET;      // D20
        case AxisId::Y:  return reg::y_axis::command::ABS_TARGET;      // D24
        case AxisId::Z:  return reg::z_axis::command::ABS_TARGET;      // D28
        case AxisId::R:  return reg::r_axis::command::ABS_TARGET;      // D32
    }
    return reg::x_axis::command::ABS_TARGET;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdRelTarget(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::REL_TARGET;      // D22
        case AxisId::Y:  return reg::y_axis::command::REL_TARGET;      // D26
        case AxisId::Z:  return reg::z_axis::command::REL_TARGET;      // D30
        case AxisId::R:  return reg::r_axis::command::REL_TARGET;      // D34
    }
    return reg::x_axis::command::REL_TARGET;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdAbsTrigger(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ABS_MOVE_TRIGGER; // M40
        case AxisId::Y:  return reg::y_axis::command::ABS_MOVE_TRIGGER; // M42
        case AxisId::Z:  return reg::z_axis::command::ABS_MOVE_TRIGGER; // M44
        case AxisId::R:  return reg::r_axis::command::ABS_MOVE_TRIGGER; // M46
    }
    return reg::x_axis::command::ABS_MOVE_TRIGGER;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdRelTrigger(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::REL_MOVE_TRIGGER; // M41
        case AxisId::Y:  return reg::y_axis::command::REL_MOVE_TRIGGER; // M43
        case AxisId::Z:  return reg::z_axis::command::REL_MOVE_TRIGGER; // M45
        case AxisId::R:  return reg::r_axis::command::REL_MOVE_TRIGGER; // M47
    }
    return reg::x_axis::command::REL_MOVE_TRIGGER;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdSetRelZero(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::SET_REL_ZERO;    // M14
        case AxisId::Y:  return reg::y_axis::command::SET_REL_ZERO;    // M15
        case AxisId::Z:  return reg::z_axis::command::SET_REL_ZERO;    // M16
        case AxisId::R:  return reg::r_axis::command::SET_REL_ZERO;    // M17
        // X1/X2: 龙门模式下相对零点由 X 统一管理，fallback 到 X
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::SET_REL_ZERO;
    }
    return reg::x_axis::command::SET_REL_ZERO;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdClearRelZero(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::CLEAR_REL_ZERO;  // M18
        case AxisId::Y:  return reg::y_axis::command::CLEAR_REL_ZERO;  // M19
        case AxisId::Z:  return reg::z_axis::command::CLEAR_REL_ZERO;  // M20
        case AxisId::R:  return reg::r_axis::command::CLEAR_REL_ZERO;  // M21
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::CLEAR_REL_ZERO;
    }
    return reg::x_axis::command::CLEAR_REL_ZERO;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdClearAbsPos(AxisId id) const {
    // v4.0: ZeroAbsoluteCommand 直接映射到 CLEAR_ABS_POS，不再经 HOME_TRIGGER
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::CLEAR_ABS_POS;   // M30
        case AxisId::Y:  return reg::y_axis::command::CLEAR_ABS_POS;   // M31
        case AxisId::Z:  return reg::z_axis::command::CLEAR_ABS_POS;   // M32
        case AxisId::R:  return reg::r_axis::command::CLEAR_ABS_POS;   // M33
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::CLEAR_ABS_POS;
    }
    return reg::x_axis::command::CLEAR_ABS_POS;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogSpeed(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::JOG_SPEED;       // D1000
        case AxisId::Y:  return reg::y_axis::command::JOG_SPEED;       // D1004
        case AxisId::Z:  return reg::z_axis::command::JOG_SPEED;       // D1008
        case AxisId::R:  return reg::r_axis::command::JOG_SPEED;       // D1012
        // X1/X2: 龙门模式下速度由 X 统一设置，fallback 到 X
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::JOG_SPEED;
    }
    return reg::x_axis::command::JOG_SPEED;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdMoveSpeed(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::MOVE_SPEED;      // D1002
        case AxisId::Y:  return reg::y_axis::command::MOVE_SPEED;      // D1006
        case AxisId::Z:  return reg::z_axis::command::MOVE_SPEED;      // D1010
        case AxisId::R:  return reg::r_axis::command::MOVE_SPEED;      // D1014
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::MOVE_SPEED;
    }
    return reg::x_axis::command::MOVE_SPEED;
}

// ---------- 反馈类 ----------

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAbsPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ABS_POSITION;   // D120
        case AxisId::Y:  return reg::y_axis::feedback::ABS_POSITION;   // D124
        case AxisId::Z:  return reg::z_axis::feedback::ABS_POSITION;   // D128
        case AxisId::R:  return reg::r_axis::feedback::ABS_POSITION;   // D132
    }
    return reg::x_axis::feedback::ABS_POSITION;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbRelPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::REL_POSITION;   // D122
        case AxisId::Y:  return reg::y_axis::feedback::REL_POSITION;   // D126
        case AxisId::Z:  return reg::z_axis::feedback::REL_POSITION;   // D130
        case AxisId::R:  return reg::r_axis::feedback::REL_POSITION;   // D134
    }
    return reg::x_axis::feedback::REL_POSITION;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbState(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::STATE;          // D100
        case AxisId::Y:  return reg::y_axis::feedback::STATE;          // D101
        case AxisId::Z:  return reg::z_axis::feedback::STATE;          // D102
        case AxisId::R:  return reg::r_axis::feedback::STATE;          // D103
        // 龙门 X 轴反馈以 X1 为准（物理寄存器同一套）
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::STATE;
    }
    return reg::x_axis::feedback::STATE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAlarmCode(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::ALARM_CODE;     // D110
        case AxisId::Y:  return reg::y_axis::feedback::ALARM_CODE;     // D111
        case AxisId::Z:  return reg::z_axis::feedback::ALARM_CODE;     // D112
        case AxisId::R:  return reg::r_axis::feedback::ALARM_CODE;     // D113
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ALARM_CODE;
    }
    return reg::x_axis::feedback::ALARM_CODE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAbsMoving(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::ABS_MOVING;     // M110
        case AxisId::Y:  return reg::y_axis::feedback::ABS_MOVING;     // M113
        case AxisId::Z:  return reg::z_axis::feedback::ABS_MOVING;     // M116
        case AxisId::R:  return reg::r_axis::feedback::ABS_MOVING;     // M119
        // 龙门 X 轴反馈以 X1 为准
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ABS_MOVING;
    }
    return reg::x_axis::feedback::ABS_MOVING;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbRelMoving(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::REL_MOVING;     // M111
        case AxisId::Y:  return reg::y_axis::feedback::REL_MOVING;     // M114
        case AxisId::Z:  return reg::z_axis::feedback::REL_MOVING;     // M117
        case AxisId::R:  return reg::r_axis::feedback::REL_MOVING;     // M120
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::REL_MOVING;
    }
    return reg::x_axis::feedback::REL_MOVING;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbJogging(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::JOGGING;        // M112
        case AxisId::Y:  return reg::y_axis::feedback::JOGGING;        // M115
        case AxisId::Z:  return reg::z_axis::feedback::JOGGING;        // M118
        case AxisId::R:  return reg::r_axis::feedback::JOGGING;        // M121
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::JOGGING;
    }
    return reg::x_axis::feedback::JOGGING;
}

// ---------- 组级命令/反馈 ----------

inline const protocol::RegisterInfo& ModbusSystemDriver::regGantryCoupling() const {
    return reg::x_axis::command::LINKAGE_ENABLE;                        // M4
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regEmergencyStopTrigger() const {
    return reg::system_global::command::ESTOP_TRIGGER;                  // M80
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbEmergencyStopActive() const {
    return reg::system_global::feedback::ESTOP_ACTIVE;                  // M130
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbGantryErrorCode() const {
    return reg::system_global::feedback::GANTRY_ERROR_CODE;             // D180
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbLinkageState() const {
    return reg::x_axis::feedback::LINKAGE_STATE;                        // M125
}

// ---------- ISystemDriver 接口 stub (阶段一不实现) ----------

inline CommunicationResult ModbusSystemDriver::send(const SystemCommand& /*cmd*/) {
    return CommunicationResult{};
}

inline void ModbusSystemDriver::pollFeedback(SystemContext& /*ctx*/) {
    // 阶段一不实现
}

} // namespace plc
