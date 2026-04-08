#pragma once
#include "entity/Axis.h"

class IAxisDriver {
public:
    virtual ~IAxisDriver() = default;
    // 核心职责：将 Axis 产生的意图物理化
    virtual void send(const AxisCommand& cmd) = 0;
};