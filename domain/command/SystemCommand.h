#pragma once

#include "entity/Axis.h"                              // AxisCommand, AxisId
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
 * @brief 统一系统命令边界
 * 
 * 所有领域层产生的控制意图通过此 variant 统一发送到基础设施层。
 * Driver 接口只需要一个 send(SystemCommand) 方法，不再感知命令来源。
 * 
 * 当前包含：
 *   - AxisCommandWithId   : 单轴控制命令（使能/运动/停止/点动）
 *   - GantryCouplingCommand : 龙门联动/解耦命令
 *   - GantryPowerCommand     : 龙门电机使能/掉电命令
 */
using SystemCommand = std::variant<
    AxisCommandWithId,
    GantryCouplingCommand,
    GantryPowerCommand
>;
