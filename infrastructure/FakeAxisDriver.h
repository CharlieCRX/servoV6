#ifndef FAKE_AXIS_DRIVER_H
#define FAKE_AXIS_DRIVER_H

#include "../application/axis/IAxisDriver.h"
#include "FakePLC.h"
#include "infrastructure/logger/Logger.h" // 🌟 引入日志系统

class FakeAxisDriver : public IAxisDriver {
public:
    explicit FakeAxisDriver(FakePLC& plc) : m_plc(plc) {}

    // 实现接口：将 Axis 产生的意图，无情地砸向 FakePLC
    void send(const AxisCommand& cmd) override {
        LOG_TRACE(LogLayer::HAL, "Driver", "Sending command to PLC");
        m_plc.onCommand(cmd);
    }

private:
    FakePLC& m_plc;
};

#endif // FAKE_AXIS_DRIVER_H