#ifndef FAKE_AXIS_DRIVER_H
#define FAKE_AXIS_DRIVER_H

#include "../infrastructure/ISystemDriver.h"
#include "FakePLC.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/utils/CommandFormatter.h"
#include <vector>

/**
 * @brief ISystemDriver 的假实现，用作测试替身
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  分组感知：每个 SystemContext 绑定一个独立的                 ║
 * ║            FakeAxisDriver + FakePLC 对                       ║
 * ║                                                              ║
 * ║  GroupA (Machine_A)          GroupB (Machine_B)              ║
 * ║  ┌───────────────────┐      ┌───────────────────┐           ║
 * ║  │ FakeAxisDriver_A  │      │ FakeAxisDriver_B  │           ║
 * ║  │   (FakePLC_A)     │      │   (FakePLC_B)     │           ║
 * ║  └───────────────────┘      └───────────────────┘           ║
 * ║        ↑                          ↑                         ║
 * ║  SystemContext_A            SystemContext_B                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 构造时注入独立的 FakePLC 引用，不同分组拥有独立的 history 记录。
 *
 * --- 急停命令处理 ---
 *
 * 当 SystemManager 消费 EmergencyStopCommand 并通过 send() 发送时：
 *   - Driver 将 active 写入 FakePLC 的"设备急停"命令寄存器
 *   - FakePLC::tick() 在 delay 后将"设备急停中"状态寄存器同步为 true
 *   - Domain 侧通过 FakePLC::getEmergencyStopFeedback() 获取状态反馈
 *     并调用 EmergencyStopController::applyFeedback() 驱动状态机跃迁
 *
 * 使用示例：
 *   FakePLC plcA, plcB;
 *   FakeAxisDriver driverA(plcA), driverB(plcB);
 *   SystemContext ctxA(driverA);  // 绑定 driverA
 *   SystemContext ctxB(driverB);  // 绑定 driverB
 */
class FakeAxisDriver : public ISystemDriver {
public:
    struct Record {
        AxisId id;
        AxisCommand cmd;
    };

    explicit FakeAxisDriver(FakePLC& plc) : m_plc(plc) {}

    // ========== ISystemDriver 统一入口 ==========

    void send(const SystemCommand& cmd) override {
        std::visit([this](auto&& c) {
            handle(c);
        }, cmd);
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

    // ========== 命令分发处理 ==========

    void handle(const AxisCommandWithId& c) {
        LOG_TRACE(LogLayer::HAL, "Driver", "Sending to PLC: " + utils::format(c.cmd));

        history.push_back({c.id, c.cmd});
        m_plc.onCommand(c.id, c.cmd);
    }

    void handle(const GantryCouplingCommand& /*cmd*/) {
        // no-op for unit tests
    }

    void handle(const GantryPowerCommand& /*cmd*/) {
        // no-op for unit tests
    }

    /**
     * @brief 急停命令处理
     *
     * 将 EmergencyStopCommand 的 active 写入 FakePLC 的命令寄存器。
     * FakePLC::tick() 将在延迟后同步状态寄存器，
     * Domain 侧通过 getEmergencyStopFeedback() 读取并驱动状态机。
     */
    void handle(const EmergencyStopCommand& cmd) {
        LOG_TRACE(LogLayer::HAL, "Driver",
            cmd.active ? "EmergencyStopCommand: ACTIVATE" : "EmergencyStopCommand: RELEASE");

        m_plc.forceEmergencyStopCommand(cmd.active);
    }
};

#endif // FAKE_AXIS_DRIVER_H
