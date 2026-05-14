#pragma once

#include "command/SystemCommand.h"  // SystemCommand variant（统一命令边界）

/**
 * @brief 统一系统驱动接口
 * 
 * 基础设施层的唯一命令入口。所有领域层产生的控制意图
 * 通过 SystemCommand variant 统一发送，Driver 不再感知
 * 命令来自 Axis 聚合还是 Gantry 聚合。
 * 
 * 实现类（如 FakeAxisDriver）通过 std::visit 分发到
 * 各自 handle(...) 重载完成 Command → 物理寄存器转换。
 */
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;

    /**
     * @brief 发送统一系统命令
     * @param cmd SystemCommand variant，包含 AxisCommandWithId / GantryCouplingCommand / GantryPowerCommand
     */
    virtual void send(const SystemCommand& cmd) = 0;
};
