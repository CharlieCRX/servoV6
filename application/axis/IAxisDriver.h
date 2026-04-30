#pragma once
#include "entity/Axis.h"
#include "entity/AxisId.h"

class IAxisDriver {
public:
    virtual ~IAxisDriver() = default;
    // 核心职责：将 Axis 产生的意图物理化
    virtual void send(AxisId id, const AxisCommand& cmd) = 0;
};