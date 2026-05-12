#pragma once
#include "application/axis/IAxisDriver.h"   // 包含 virtual void send(AxisId, const AxisCommand&)
#include "gantry/GantryGroup.h" // 包含 GantryCommand

/**
 * @brief 聚合驱动接口
 * 实现类（如 FakePLC）将同时负责处理单轴和龙门的底层物理指令
 */
class ISystemDriver : public IAxisDriver {
public:
    virtual ~ISystemDriver() = default;
    // 扩展龙门特有的控制接口
    virtual void sendGantry(const GantryCommand& cmd) = 0;
};