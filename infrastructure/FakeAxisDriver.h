#ifndef FAKE_AXIS_DRIVER_H
#define FAKE_AXIS_DRIVER_H

#include "../application/axis/IAxisDriver.h"
#include "FakePLC.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/utils/CommandFormatter.h" // 🌟 引入格式化工具

class FakeAxisDriver : public IAxisDriver {
public:
    explicit FakeAxisDriver(FakePLC& plc) : m_plc(plc) {}

    // 实现接口：将 Axis 产生的意图，无情地砸向 FakePLC
    void send(AxisId id, const AxisCommand& cmd) override {
        // 🌟 瘦身成功：一行代码完成日志拼接
        LOG_TRACE(LogLayer::HAL, "Driver", "Sending to PLC: " + utils::format(cmd));
        
        m_plc.onCommand(cmd);
    }

private:
    FakePLC& m_plc;
};

#endif // FAKE_AXIS_DRIVER_H