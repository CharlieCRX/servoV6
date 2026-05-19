#ifndef FAKE_AXIS_DRIVER_H
#define FAKE_AXIS_DRIVER_H

#include "../infrastructure/ISystemDriver.h"
#include "FakePLC.h"
#include "domain/entity/SystemContext.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/utils/CommandFormatter.h"
#include <vector>
#include <array>
#include <variant>

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
 * --- 命令通路 ---
 *
 * send() 返回 CommunicationResult:
 *   - 未连接时返回 Disconnected
 *   - 正常时处理命令后返回 Sent
 *
 * --- 反馈通路 ---
 *
 * pollFeedback() 实现:
 *   1. 推进 FakePLC 一个周期 (tick)
 *   2. 遍历所有 6 个轴，读取 FakePLC 反馈并注入 Axis::applyFeedback()
 *   3. 注入急停状态反馈 -> EmergencyStopController::applyFeedback()
 *   4. 注入龙门反馈 -> GantryCouplingController::applyFeedback()
 *                     + GantryPowerController::applyFeedback()
 *
 * --- 测试辅助 ---
 *   disconnect() / connect() -- 模拟网络通断
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

    CommunicationResult send(const SystemCommand& cmd) override {
        if (!m_connected) {
            return CommunicationResult{
                CommunicationResult::Status::Disconnected,
                0,
                "Fake driver not connected"
            };
        }
        std::visit([this](auto&& c) {
            handle(c);
        }, cmd);
        return CommunicationResult{};  // Fake 驱动永远通讯成功
    }

    void pollFeedback(SystemContext& ctx) override {
        // 1. 推进硬件模拟一个周期
        m_plc.tick(10);

        // 2. 注入安全状态反馈 -- 急停
        ctx.emergencyStopController().applyFeedback(m_plc.getEmergencyStopFeedback());

        // 3. 注入龙门反馈 -- 电机使能 + 联动状态
        GantryFeedback gf = m_plc.getGantryFeedback();
        ctx.gantryPowerController().applyFeedback(gf);
        ctx.gantryCouplingController().applyFeedback(gf);

        // 4. 读取所有轴的反馈并注入
        constexpr std::array<AxisId, 6> ALL_AXIS_IDS = {
            AxisId::X, AxisId::X1, AxisId::X2, AxisId::Y, AxisId::Z, AxisId::R
        };
        for (auto axisId : ALL_AXIS_IDS) {
            Axis* axis = nullptr;
            ContextRejection r;
            if (ctx.tryGetAxis(axisId, axis, r) && axis) {
                axis->applyFeedback(m_plc.getFeedback(axisId));
            }
        }
    }

    // ========== 测试辅助 ==========

    /// @brief 模拟网络断开（测试用）
    void disconnect() { m_connected = false; }

    /// @brief 模拟网络恢复（测试用）
    void connect() { m_connected = true; }

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
    bool m_connected = true;  // Fake 驱动默认已连接

    // ========== 命令分发处理 ==========

    void handle(const AxisCommandWithId& c) {
        LOG_TRACE(LogLayer::HAL, "Driver", "Sending to PLC: " + utils::format(c.cmd));

        history.push_back({c.id, c.cmd});
        m_plc.onCommand(c.id, c.cmd);
    }

    void handle(const GantryCouplingCommand& cmd) {
        LOG_TRACE(LogLayer::HAL, "Driver",
            cmd.enableCoupling ? "GantryCouplingCommand: COUPLE" : "GantryCouplingCommand: DECOUPLE");
        m_plc.onGantryCommand(cmd);
    }

    void handle(const GantryPowerCommand& cmd) {
        LOG_TRACE(LogLayer::HAL, "Driver",
            cmd.enable ? "GantryPowerCommand: POWER_ON" : "GantryPowerCommand: POWER_OFF");
        m_plc.onGantryCommand(cmd);
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
