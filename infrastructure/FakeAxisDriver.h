#ifndef FAKE_AXIS_DRIVER_H
#define FAKE_AXIS_DRIVER_H

#include "../infrastructure/ISystemDriver.h"
#include "FakePLC.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/utils/CommandFormatter.h"
#include <vector>

class FakeAxisDriver : public ISystemDriver {
public:
    struct Record {
        AxisId id;
        AxisCommand cmd;
    };

    explicit FakeAxisDriver(FakePLC& plc) : m_plc(plc) {}

    void send(AxisId id, const AxisCommand& cmd) override {
        LOG_TRACE(LogLayer::HAL, "Driver", "Sending to PLC: " + utils::format(cmd));

        history.push_back({id, cmd});  // ← 写入公开成员 history
        m_plc.onCommand(id, cmd);
    }

    void sendGantry(const GantryCommand& /*cmd*/) override {
        // no-op for unit tests
    }

    // ========== 测试辅助 ==========

    std::vector<Record> history;  // 公开，测试可直接读写/迭代

    template<typename T>
    bool has() const {
        for (const auto& r : history) {
            if (std::holds_alternative<T>(r.cmd)) return true;
        }
        return false;
    }

    template<typename T>
    T lastForAxis(AxisId targetId) const {
        for (auto it = history.rbegin(); it != history.rend(); ++it) {
            if (it->id == targetId) {
                if (auto* cmd = std::get_if<T>(&it->cmd)) {
                    return *cmd;
                }
            }
        }
        return T{};
    }

private:
    FakePLC& m_plc;
};

#endif // FAKE_AXIS_DRIVER_H
