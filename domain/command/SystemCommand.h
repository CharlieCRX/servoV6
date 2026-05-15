#pragma once

#include "entity/Axis.h"                              // AxisCommand, AxisId
#include "entity/AxisId.h"
#include "gantry/GantryCouplingController.h"          // GantryCouplingCommand
#include "gantry/GantryPowerController.h"             // GantryPowerCommand
#include <variant>

/**
 * @brief 轴命令 + 目标轴 ID 的包装结构
 * 
 * 用于统一命令总线，使 Driver 接口不再需要独立的 AxisId 参数。
 * 原本 IAxisDriver::send(AxisId, AxisCommand) 的两个参数合并为一个结构体。
 */
struct AxisCommandWithId {
    AxisId id;
    AxisCommand cmd;
};

/**
 * @brief 急停命令（对应 PLC 寄存器"设备急停"）
 *
 * 注意：
 *   - 这是写入 PLC 的"命令"，不是 PLC 反馈的"状态"
 *   - PLC 反馈状态通过独立的"设备急停中"寄存器获取
 *   - 命令与状态的分离是实现四态安全状态机的基础
 */
struct EmergencyStopCommand {
    bool active; // true: 触发急停, false: 解除急停锁定
};

/**
 * @brief 统一系统命令边界
 * 
 * 所有领域层产生的控制意图通过此 variant 统一发送到基础设施层。
 * Driver 接口只需要一个 send(SystemCommand) 方法，不再感知命令来源。
 * 
 * 当前包含：
 *   - AxisCommandWithId      : 单轴控制命令（使能/运动/停止/点动）
 *   - GantryCouplingCommand  : 龙门联动/解耦命令
 *   - GantryPowerCommand     : 龙门电机使能/掉电命令
 *   - EmergencyStopCommand   : 急停触发/解除命令
 */
using SystemCommand = std::variant<
    AxisCommandWithId,
    GantryCouplingCommand,
    GantryPowerCommand,
    EmergencyStopCommand
>;
