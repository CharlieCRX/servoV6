#pragma once

#include "infrastructure/ISystemDriver.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/IModbusClient.h"
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/utils/IClock.h"
#include "infrastructure/utils/overloaded.h"
#include "infrastructure/plc/AxisStateDeriver.h"
#include "infrastructure/logger/Logger.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/ContextRejection.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryFeedback.h"
#include <cstdint>
#include <deque>
#include <algorithm>
#include <memory>
#include <chrono>
#include <sstream>

namespace plc {

// =============================================================================
// TDD 阶段 2: PendingEdge — 边沿触发协议待执行项
// =============================================================================

struct PendingEdge {
    enum class State {
        Idle,
        WroteOn,
        WroteOff
    };

    const protocol::RegisterInfo* reg = nullptr;
    State state = State::Idle;
    std::chrono::steady_clock::time_point onTime{};
};

class ModbusSystemDriver : public ISystemDriver {
public:
    ModbusSystemDriver()
        : m_clock(std::make_unique<SteadyClock>())
    {}
    ~ModbusSystemDriver() override = default;

    // =========================================================================
    // 寄存器选择器 —— 命令类
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regCmdEnable(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogFwd(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogBwd(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdAbsTarget(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdRelTarget(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdAbsTrigger(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdRelTrigger(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdSetRelZero(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdClearRelZero(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdClearAbsPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdJogSpeed(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regCmdMoveSpeed(AxisId id) const;

    // =========================================================================
    // 寄存器选择器 —— 反馈类
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regFbAbsPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbRelPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbState(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbAlarmCode(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbAbsMoving(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbRelMoving(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbJogging(AxisId id) const;

    // ⭐ 新增：全量反馈寄存器选择器（用于构筑 AxisFeedback）
    [[nodiscard]] const protocol::RegisterInfo& regFbRelZeroRecord(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbSoftLimitPos(AxisId id) const;
    [[nodiscard]] const protocol::RegisterInfo& regFbSoftLimitNeg(AxisId id) const;

    // =========================================================================
    // 寄存器选择器 —— 组级命令/反馈
    // =========================================================================

    [[nodiscard]] const protocol::RegisterInfo& regGantryCoupling() const;
    [[nodiscard]] const protocol::RegisterInfo& regEmergencyStopTrigger() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbEmergencyStopActive() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbGantryErrorCode() const;
    [[nodiscard]] const protocol::RegisterInfo& regFbLinkageState() const;

    // =========================================================================
    // ISystemDriver 接口
    // =========================================================================

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;

    // ===== 连接状态监控与手动重连（★ P1/P2 新增）=====

    /// @brief 查询当前连接状态快照
    ConnectionState getConnectionState() const override;

    /// @brief 触发手动重连（委托给 IModbusClient::requestReconnect）
    void reconnect() override;

    // =========================================================================
    // PendingEdge 队列管理
    // =========================================================================

    void enqueueEdge(const protocol::RegisterInfo* reg);
    PendingEdge dequeueEdge();
    [[nodiscard]] bool isEdgePending(const protocol::RegisterInfo* reg) const;
    [[nodiscard]] size_t pendingEdgeCount() const;
    void clearPendingEdges();

    // =========================================================================
    // 设备与时钟注入
    // =========================================================================

    void setDevice(protocol::PlcDevice* device) { m_device = device; }
    void setClock(std::unique_ptr<IClock> clock) { m_clock = std::move(clock); }
    void setPoller(std::unique_ptr<protocol::PlcPoller> poller) { m_poller = std::move(poller); }
    void setModbusClient(protocol::IModbusClient* client) { m_modbusClient = client; }
    void advanceTime(std::chrono::milliseconds ms);
    CommunicationResult sendEdgeTrigger(const protocol::RegisterInfo& reg);
    void servicePendingEdgeTriggers();

private:
    std::deque<PendingEdge> m_pendingEdges;
    std::deque<PendingEdge>::iterator findEdgeByAddress(const protocol::RegisterInfo* reg);
    std::deque<PendingEdge>::const_iterator findEdgeByAddress(const protocol::RegisterInfo* reg) const;

    static constexpr int EDGE_TRIGGER_PULSE_MS = 150;
    protocol::PlcDevice* m_device = nullptr;
    std::unique_ptr<IClock> m_clock;
    std::unique_ptr<protocol::PlcPoller> m_poller;
    protocol::IModbusClient* m_modbusClient = nullptr;
};

// =============================================================================
// 内联实现：寄存器选择器
// =============================================================================

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdEnable(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ENABLE_REQUEST;
        case AxisId::Y:  return reg::y_axis::command::ENABLE_REQUEST;
        case AxisId::Z:  return reg::z_axis::command::ENABLE_REQUEST;
        case AxisId::R:  return reg::r_axis::command::ENABLE_REQUEST;
    }
    return reg::x_axis::command::ENABLE_REQUEST;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogFwd(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::X1_JOG_FORWARD;
        case AxisId::Y:  return reg::y_axis::command::JOG_FORWARD;
        case AxisId::Z:  return reg::z_axis::command::JOG_FORWARD;
        case AxisId::R:  return reg::r_axis::command::JOG_FORWARD;
    }
    return reg::x_axis::command::X1_JOG_FORWARD;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogBwd(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::X1_JOG_BACKWARD;
        case AxisId::Y:  return reg::y_axis::command::JOG_BACKWARD;
        case AxisId::Z:  return reg::z_axis::command::JOG_BACKWARD;
        case AxisId::R:  return reg::r_axis::command::JOG_BACKWARD;
    }
    return reg::x_axis::command::X1_JOG_BACKWARD;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdAbsTarget(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ABS_TARGET;
        case AxisId::Y:  return reg::y_axis::command::ABS_TARGET;
        case AxisId::Z:  return reg::z_axis::command::ABS_TARGET;
        case AxisId::R:  return reg::r_axis::command::ABS_TARGET;
    }
    return reg::x_axis::command::ABS_TARGET;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdRelTarget(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::REL_TARGET;
        case AxisId::Y:  return reg::y_axis::command::REL_TARGET;
        case AxisId::Z:  return reg::z_axis::command::REL_TARGET;
        case AxisId::R:  return reg::r_axis::command::REL_TARGET;
    }
    return reg::x_axis::command::REL_TARGET;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdAbsTrigger(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::ABS_MOVE_TRIGGER;
        case AxisId::Y:  return reg::y_axis::command::ABS_MOVE_TRIGGER;
        case AxisId::Z:  return reg::z_axis::command::ABS_MOVE_TRIGGER;
        case AxisId::R:  return reg::r_axis::command::ABS_MOVE_TRIGGER;
    }
    return reg::x_axis::command::ABS_MOVE_TRIGGER;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdRelTrigger(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::REL_MOVE_TRIGGER;
        case AxisId::Y:  return reg::y_axis::command::REL_MOVE_TRIGGER;
        case AxisId::Z:  return reg::z_axis::command::REL_MOVE_TRIGGER;
        case AxisId::R:  return reg::r_axis::command::REL_MOVE_TRIGGER;
    }
    return reg::x_axis::command::REL_MOVE_TRIGGER;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdSetRelZero(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::SET_REL_ZERO;
        case AxisId::Y:  return reg::y_axis::command::SET_REL_ZERO;
        case AxisId::Z:  return reg::z_axis::command::SET_REL_ZERO;
        case AxisId::R:  return reg::r_axis::command::SET_REL_ZERO;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::SET_REL_ZERO;
    }
    return reg::x_axis::command::SET_REL_ZERO;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdClearRelZero(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::CLEAR_REL_ZERO;
        case AxisId::Y:  return reg::y_axis::command::CLEAR_REL_ZERO;
        case AxisId::Z:  return reg::z_axis::command::CLEAR_REL_ZERO;
        case AxisId::R:  return reg::r_axis::command::CLEAR_REL_ZERO;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::CLEAR_REL_ZERO;
    }
    return reg::x_axis::command::CLEAR_REL_ZERO;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdClearAbsPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::CLEAR_ABS_POS;
        case AxisId::Y:  return reg::y_axis::command::CLEAR_ABS_POS;
        case AxisId::Z:  return reg::z_axis::command::CLEAR_ABS_POS;
        case AxisId::R:  return reg::r_axis::command::CLEAR_ABS_POS;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::CLEAR_ABS_POS;
    }
    return reg::x_axis::command::CLEAR_ABS_POS;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdJogSpeed(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::JOG_SPEED;
        case AxisId::Y:  return reg::y_axis::command::JOG_SPEED;
        case AxisId::Z:  return reg::z_axis::command::JOG_SPEED;
        case AxisId::R:  return reg::r_axis::command::JOG_SPEED;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::JOG_SPEED;
    }
    return reg::x_axis::command::JOG_SPEED;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regCmdMoveSpeed(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::command::MOVE_SPEED;
        case AxisId::Y:  return reg::y_axis::command::MOVE_SPEED;
        case AxisId::Z:  return reg::z_axis::command::MOVE_SPEED;
        case AxisId::R:  return reg::r_axis::command::MOVE_SPEED;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::command::MOVE_SPEED;
    }
    return reg::x_axis::command::MOVE_SPEED;
}

// ---------- 反馈类 ----------

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAbsPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ABS_POSITION;
        case AxisId::Y:  return reg::y_axis::feedback::ABS_POSITION;
        case AxisId::Z:  return reg::z_axis::feedback::ABS_POSITION;
        case AxisId::R:  return reg::r_axis::feedback::ABS_POSITION;
    }
    return reg::x_axis::feedback::ABS_POSITION;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbRelPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::REL_POSITION;
        case AxisId::Y:  return reg::y_axis::feedback::REL_POSITION;
        case AxisId::Z:  return reg::z_axis::feedback::REL_POSITION;
        case AxisId::R:  return reg::r_axis::feedback::REL_POSITION;
    }
    return reg::x_axis::feedback::REL_POSITION;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbState(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::STATE;
        case AxisId::Y:  return reg::y_axis::feedback::STATE;
        case AxisId::Z:  return reg::z_axis::feedback::STATE;
        case AxisId::R:  return reg::r_axis::feedback::STATE;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::STATE;
    }
    return reg::x_axis::feedback::STATE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAlarmCode(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::ALARM_CODE;
        case AxisId::Y:  return reg::y_axis::feedback::ALARM_CODE;
        case AxisId::Z:  return reg::z_axis::feedback::ALARM_CODE;
        case AxisId::R:  return reg::r_axis::feedback::ALARM_CODE;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ALARM_CODE;
    }
    return reg::x_axis::feedback::ALARM_CODE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbAbsMoving(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::ABS_MOVING;
        case AxisId::Y:  return reg::y_axis::feedback::ABS_MOVING;
        case AxisId::Z:  return reg::z_axis::feedback::ABS_MOVING;
        case AxisId::R:  return reg::r_axis::feedback::ABS_MOVING;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::ABS_MOVING;
    }
    return reg::x_axis::feedback::ABS_MOVING;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbRelMoving(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::REL_MOVING;
        case AxisId::Y:  return reg::y_axis::feedback::REL_MOVING;
        case AxisId::Z:  return reg::z_axis::feedback::REL_MOVING;
        case AxisId::R:  return reg::r_axis::feedback::REL_MOVING;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::REL_MOVING;
    }
    return reg::x_axis::feedback::REL_MOVING;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbJogging(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::JOGGING;
        case AxisId::Y:  return reg::y_axis::feedback::JOGGING;
        case AxisId::Z:  return reg::z_axis::feedback::JOGGING;
        case AxisId::R:  return reg::r_axis::feedback::JOGGING;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::JOGGING;
    }
    return reg::x_axis::feedback::JOGGING;
}

// ⭐ 新增：全量反馈寄存器选择器

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbRelZeroRecord(AxisId id) const {
    switch (id) {
        case AxisId::X:  return reg::x_axis::feedback::REL_ZERO_RECORD;
        case AxisId::Y:  return reg::y_axis::feedback::REL_ZERO_RECORD;
        case AxisId::Z:  return reg::z_axis::feedback::REL_ZERO_RECORD;
        case AxisId::R:  return reg::r_axis::feedback::REL_ZERO_RECORD;
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::REL_ZERO_RECORD;
    }
    return reg::x_axis::feedback::REL_ZERO_RECORD;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbSoftLimitPos(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::X1_SOFT_LIMIT_POS;
        case AxisId::Y:  return reg::y_axis::feedback::SOFT_LIMIT_POS;
        case AxisId::Z:  return reg::z_axis::feedback::SOFT_LIMIT_POS;
        case AxisId::R:  return reg::x_axis::feedback::X1_SOFT_LIMIT_POS; // R has no specific SOFT_LIMIT_POS in z_axis, fallthrough placeholder; per register table, use a reasonable default
    }
    return reg::x_axis::feedback::X1_SOFT_LIMIT_POS;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbSoftLimitNeg(AxisId id) const {
    switch (id) {
        case AxisId::X:  [[fallthrough]];
        case AxisId::X1: [[fallthrough]];
        case AxisId::X2: return reg::x_axis::feedback::X1_SOFT_LIMIT_NEG;
        case AxisId::Y:  return reg::y_axis::feedback::SOFT_LIMIT_NEG;
        case AxisId::Z:  return reg::z_axis::feedback::SOFT_LIMIT_NEG;
        case AxisId::R:  return reg::x_axis::feedback::X1_SOFT_LIMIT_NEG; // R has no specific SOFT_LIMIT_NEG, use a fallthrough placeholder
    }
    return reg::x_axis::feedback::X1_SOFT_LIMIT_NEG;
}

// ---------- 组级命令/反馈 ----------

inline const protocol::RegisterInfo& ModbusSystemDriver::regGantryCoupling() const {
    return reg::x_axis::command::LINKAGE_ENABLE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regEmergencyStopTrigger() const {
    return reg::system_global::command::ESTOP_TRIGGER;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbEmergencyStopActive() const {
    return reg::system_global::feedback::ESTOP_ACTIVE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbGantryErrorCode() const {
    return reg::system_global::feedback::GANTRY_ERROR_CODE;
}

inline const protocol::RegisterInfo& ModbusSystemDriver::regFbLinkageState() const {
    return reg::x_axis::feedback::LINKAGE_STATE;
}

// ---------- ISystemDriver::send ----------

inline CommunicationResult ModbusSystemDriver::send(const SystemCommand& cmd) {
    if (!m_device) {
        LOG_WARN(LogLayer::HAL, "ModbusSystemDriver", "send: No PlcDevice bound (disconnected)");
        return CommunicationResult::Disconnected();
    }

    auto logAndResult = [](const std::string& msg, CommunicationResult result) {
        if (result.ok()) {
            LOG_INFO(LogLayer::HAL, "ModbusSystemDriver", msg + " -> OK");
        } else {
            std::ostringstream oss;
            oss << msg << " -> FAILED: status=" << static_cast<int>(result.status)
                << " diag=" << result.diagnostic;
            LOG_ERROR(LogLayer::HAL, "ModbusSystemDriver", oss.str());
        }
        return result;
    };

    return std::visit(overloaded{
        [this, &logAndResult](const AxisCommandWithId& ac) -> CommunicationResult {
            const auto id = ac.id;
            const char* axisName = axisIdToString(id);

            return std::visit(overloaded{
                [](std::monostate) -> CommunicationResult {
                    LOG_INFO(LogLayer::HAL, "ModbusSystemDriver", "send: monostate (no-op) -> OK");
                    return CommunicationResult::Sent();
                },
                [this, id, axisName, &logAndResult](const JogCommand& j) -> CommunicationResult {
                    const char* dirStr = (j.dir == Direction::Forward) ? "Fwd" : "Bwd";
                    const auto& reg = (j.dir == Direction::Forward)
                        ? regCmdJogFwd(id) : regCmdJogBwd(id);
                    std::ostringstream oss;
                    oss << "send: Jog " << axisName << " " << dirStr
                        << " active=" << (j.active ? "true" : "false")
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeBool(reg, j.active));
                },
                [this, id, axisName, &logAndResult](const StopCommand&) -> CommunicationResult {
                    const auto& regFwd = regCmdJogFwd(id);
                    const auto& regBwd = regCmdJogBwd(id);
                    std::ostringstream oss;
                    oss << "send: Stop " << axisName
                        << " -> reg[" << regFwd.address << "] off, reg[" << regBwd.address << "] off";
                    auto r1 = m_device->writeBool(regFwd, false);
                    if (!r1.ok()) {
                        LOG_ERROR(LogLayer::HAL, "ModbusSystemDriver",
                            oss.str() + " -> FAILED on JogFwd off: status="
                            + std::to_string(static_cast<int>(r1.status)) + " diag=" + r1.diagnostic);
                        return r1;
                    }
                    auto r2 = m_device->writeBool(regBwd, false);
                    if (!r2.ok()) {
                        LOG_ERROR(LogLayer::HAL, "ModbusSystemDriver",
                            oss.str() + " -> FAILED on JogBwd off: status="
                            + std::to_string(static_cast<int>(r2.status)) + " diag=" + r2.diagnostic);
                        return r2;
                    }
                    LOG_INFO(LogLayer::HAL, "ModbusSystemDriver", oss.str() + " -> OK");
                    return CommunicationResult::Sent();
                },
                [this, id, axisName, &logAndResult](const EnableCommand& e) -> CommunicationResult {
                    const auto& reg = regCmdEnable(id);
                    std::ostringstream oss;
                    oss << "send: Enable " << axisName
                        << " active=" << (e.active ? "true" : "false")
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeBool(reg, e.active));
                },
                [this, id, axisName, &logAndResult](const SetJogVelocityCommand& v) -> CommunicationResult {
                    const auto& reg = regCmdJogSpeed(id);
                    std::ostringstream oss;
                    oss << "send: SetJogVelocity " << axisName
                        << " velocity=" << v.velocity
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeFloat(reg, static_cast<float>(v.velocity)));
                },
                [this, id, axisName, &logAndResult](const SetMoveVelocityCommand& v) -> CommunicationResult {
                    const auto& reg = regCmdMoveSpeed(id);
                    std::ostringstream oss;
                    oss << "send: SetMoveVelocity " << axisName
                        << " velocity=" << v.velocity
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeFloat(reg, static_cast<float>(v.velocity)));
                },
                [this, id, axisName, &logAndResult](const SetAbsTargetCommand& t) -> CommunicationResult {
                    const auto& reg = regCmdAbsTarget(id);
                    std::ostringstream oss;
                    oss << "send: SetAbsTarget " << axisName
                        << " target=" << t.target
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeFloat(reg, static_cast<float>(t.target)));
                },
                [this, id, axisName, &logAndResult](const SetRelTargetCommand& t) -> CommunicationResult {
                    const auto& reg = regCmdRelTarget(id);
                    std::ostringstream oss;
                    oss << "send: SetRelTarget " << axisName
                        << " distance=" << t.distance
                        << " -> reg[" << reg.address << "] " << reg.description;
                    return logAndResult(oss.str(),
                        m_device->writeFloat(reg, static_cast<float>(t.distance)));
                },
                [this, id, axisName, &logAndResult](const TriggerAbsMoveCommand&) -> CommunicationResult {
                    const auto& reg = regCmdAbsTrigger(id);
                    std::ostringstream oss;
                    oss << "send: TriggerAbsMove " << axisName
                        << " -> reg[" << reg.address << "] " << reg.description
                        << " (edge trigger)";
                    return logAndResult(oss.str(), sendEdgeTrigger(reg));
                },
                [this, id, axisName, &logAndResult](const TriggerRelMoveCommand&) -> CommunicationResult {
                    const auto& reg = regCmdRelTrigger(id);
                    std::ostringstream oss;
                    oss << "send: TriggerRelMove " << axisName
                        << " -> reg[" << reg.address << "] " << reg.description
                        << " (edge trigger)";
                    return logAndResult(oss.str(), sendEdgeTrigger(reg));
                },
                [this, id, axisName, &logAndResult](const ZeroAbsoluteCommand&) -> CommunicationResult {
                    const auto& reg = regCmdClearAbsPos(id);
                    std::ostringstream oss;
                    oss << "send: ZeroAbsolute " << axisName
                        << " -> reg[" << reg.address << "] " << reg.description
                        << " (edge trigger)";
                    return logAndResult(oss.str(), sendEdgeTrigger(reg));
                },
                [this, id, axisName, &logAndResult](const SetRelativeZeroCommand&) -> CommunicationResult {
                    const auto& reg = regCmdSetRelZero(id);
                    std::ostringstream oss;
                    oss << "send: SetRelativeZero " << axisName
                        << " -> reg[" << reg.address << "] " << reg.description
                        << " (edge trigger)";
                    return logAndResult(oss.str(), sendEdgeTrigger(reg));
                },
                [this, id, axisName, &logAndResult](const ClearRelativeZeroCommand&) -> CommunicationResult {
                    const auto& reg = regCmdClearRelZero(id);
                    std::ostringstream oss;
                    oss << "send: ClearRelativeZero " << axisName
                        << " -> reg[" << reg.address << "] " << reg.description
                        << " (edge trigger)";
                    return logAndResult(oss.str(), sendEdgeTrigger(reg));
                },
                [](const MoveCommand&) -> CommunicationResult {
                    LOG_INFO(LogLayer::HAL, "ModbusSystemDriver",
                        "send: MoveCommand (legacy combined, no-op at HAL) -> OK");
                    return CommunicationResult::Sent();
                }
            }, ac.cmd);
        },
        [this, &logAndResult](const GantryCouplingCommand& g) -> CommunicationResult {
            const auto& reg = regGantryCoupling();
            std::ostringstream oss;
            oss << "send: GantryCoupling"
                << " enableCoupling=" << (g.enableCoupling ? "true" : "false")
                << " -> reg[" << reg.address << "] " << reg.description;
            return logAndResult(oss.str(),
                m_device->writeBool(reg, g.enableCoupling));
        },
        [this, &logAndResult](const GantryPowerCommand& g) -> CommunicationResult {
            const auto& reg = regCmdEnable(AxisId::X);
            std::ostringstream oss;
            oss << "send: GantryPower"
                << " enable=" << (g.enable ? "true" : "false")
                << " -> reg[" << reg.address << "] " << reg.description
                << " (via X axis enable)";
            return logAndResult(oss.str(),
                m_device->writeBool(reg, g.enable));
        },
        [this, &logAndResult](const EmergencyStopCommand& e) -> CommunicationResult {
            const auto& reg = regEmergencyStopTrigger();
            std::ostringstream oss;
            oss << "send: EmergencyStop"
                << " active=" << (e.active ? "true" : "false")
                << " -> reg[" << reg.address << "] " << reg.description;
            return logAndResult(oss.str(),
                m_device->writeBool(reg, e.active));
        }
    }, cmd);
}

// ---------- ISystemDriver 连接状态与重连（★ P1/P2 新增） ----------

inline ConnectionState ModbusSystemDriver::getConnectionState() const {
    ConnectionState state;
    if (m_modbusClient) {
        state.connected = m_modbusClient->isConnected();
    }
    // 诊断信息在 pollFeedback 中更新
    return state;
}

inline void ModbusSystemDriver::reconnect() {
    if (m_modbusClient) {
        LOG_INFO(LogLayer::HAL, "ModbusSystemDriver",
            "reconnect() -- triggering manual reconnect via IModbusClient::requestReconnect()");
        m_modbusClient->requestReconnect();
    } else {
        LOG_WARN(LogLayer::HAL, "ModbusSystemDriver",
            "reconnect() -- no IModbusClient bound, cannot reconnect");
    }
}

// ---------- ISystemDriver::pollFeedback ----------

inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    servicePendingEdgeTriggers();

    if (!m_modbusClient || !m_poller) {
        return;
    }

    // ★ P1 断连优化：断连时跳过无效轮询，避免高频 promise/future 创建和 asio::post 调度
    // 与自动重连竞争 io_context 线程
    if (!m_modbusClient->isConnected()) {
        const auto now = m_clock->now();
        const uint64_t timestamp = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        auto untrusted = protocol::PlcPoller::untrusted(timestamp);
        if (m_device) { m_device->updateSnapshot(std::move(untrusted)); }

        LOG_WARN_EVERY_MS(1000, LogLayer::HAL, "ModbusSystemDriver",
            "pollFeedback skipped -- not connected (waiting for auto/manual reconnect)");
        return;
    }

    const auto req = m_poller->prepare();

    std::vector<std::vector<uint8_t>> coilResponses;
    coilResponses.reserve(req.coilRequests.size());
    bool allCoilsOk = true;

    for (const auto& cr : req.coilRequests) {
        std::vector<uint8_t> payload;
        CommunicationResult result =
            m_modbusClient->readCoils(cr.range.startAddress, cr.range.count, payload);
        if (result.ok()) {
            coilResponses.push_back(std::move(payload));
        } else {
            allCoilsOk = false;
            coilResponses.push_back({});
        }
    }

    std::vector<std::vector<uint16_t>> wordResponses;
    wordResponses.reserve(req.wordRequests.size());
    bool allWordsOk = true;

    for (const auto& wr : req.wordRequests) {
        std::vector<uint16_t> payload;
        CommunicationResult result =
            m_modbusClient->readHoldingRegisters(wr.range.startAddress, wr.range.count, payload);
        if (result.ok()) {
            wordResponses.push_back(std::move(payload));
        } else {
            allWordsOk = false;
            wordResponses.push_back({});
        }
    }

    const auto now = m_clock->now();
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count()
    );

    if (!allCoilsOk || !allWordsOk) {
        auto untrusted = protocol::PlcPoller::untrusted(timestamp);
        if (m_device) { m_device->updateSnapshot(std::move(untrusted)); }
        return;
    }

    auto snapshot = m_poller->assemble(coilResponses, wordResponses, timestamp);
    if (m_device) { m_device->updateSnapshot(std::move(snapshot)); }
    if (m_device && !m_device->isStateTrusted()) { return; }

    static constexpr AxisId kPolledAxisIds[] = {
        AxisId::X, AxisId::X1, AxisId::X2,
        AxisId::Y, AxisId::Z, AxisId::R,
    };

    for (AxisId id : kPolledAxisIds) {
        Axis* axis = nullptr;
        ContextRejection rejection;
        if (!ctx.tryReadAxis(id, axis, rejection)) { continue; }

        // ── 读取 PLC 反馈寄存器 ──
        const int16_t stateRaw = m_device->readInt16(regFbState(id));
        const int16_t alarmCode = m_device->readInt16(regFbAlarmCode(id));
        const bool absMoving = m_device->readBool(regFbAbsMoving(id));
        const bool relMoving = m_device->readBool(regFbRelMoving(id));
        const bool jogging = m_device->readBool(regFbJogging(id));
        const float absPos = m_device->readFloat(regFbAbsPos(id));
        const float relPos = m_device->readFloat(regFbRelPos(id));
        const float relZeroRec = m_device->readFloat(regFbRelZeroRecord(id));
        const float softLimitPos = m_device->readFloat(regFbSoftLimitPos(id));
        const float softLimitNeg = m_device->readFloat(regFbSoftLimitNeg(id));
        const float jogVel = m_device->readFloat(regCmdJogSpeed(id));
        const float moveVel = m_device->readFloat(regCmdMoveSpeed(id));
        const float absTarget = m_device->readFloat(regCmdAbsTarget(id));
        const float relTarget = m_device->readFloat(regCmdRelTarget(id));

        if (alarmCode != 0) {
            std::ostringstream alarmOss;
            alarmOss << "pollFeedback: " << axisIdToString(id)
                     << " alarmCode=" << alarmCode;
            LOG_WARN(LogLayer::HAL, "ModbusSystemDriver", alarmOss.str());
        }

        // ── 推导轴状态 ──
        AxisState derivedState = deriveAxisState(
            stateRaw, alarmCode, absMoving, relMoving, jogging);

        // ── alarmCode == 3 软限位判断 ──
        // alarmCode 3 表示触发了正限位或负限位，需要根据 absPos 与 SOFT_LIMIT 做二次判断
        bool posLimit = false;
        bool negLimit = false;
        if (alarmCode == 3) {
            std::ostringstream limitOss;
            limitOss << "pollFeedback: " << axisIdToString(id)
                     << " soft limit alarm detected"
                     << " | absPos=" << absPos
                     << " | softLimitPos=" << softLimitPos
                     << " | softLimitNeg=" << softLimitNeg;
            // 如果 absPos + 0.1 >= SOFT_LIMIT_POS → 超出正限位
            if (static_cast<double>(absPos) + 0.1 >= static_cast<double>(softLimitPos)) {
                posLimit = true;
            }
            // 如果 absPos - 0.1 <= SOFT_LIMIT_NEG → 超出负限位
            if (static_cast<double>(absPos) - 0.1 <= static_cast<double>(softLimitNeg)) {
                negLimit = true;
            }
            limitOss << " | posLimit=" << (posLimit ? "true" : "false")
                     << " | negLimit=" << (negLimit ? "true" : "false");
            LOG_WARN(LogLayer::HAL, "ModbusSystemDriver", limitOss.str());
        }

        // ── 构筑 AxisFeedback ──
        AxisFeedback fb;
        fb.state = derivedState;
        fb.absPos = static_cast<double>(absPos);
        fb.relPos = static_cast<double>(relPos);
        fb.relZeroAbsPos = static_cast<double>(relZeroRec);
        fb.posLimit = posLimit;
        fb.negLimit = negLimit;
        fb.posLimitValue = static_cast<double>(softLimitPos);
        fb.negLimitValue = static_cast<double>(softLimitNeg);
        fb.getjogVelocity = static_cast<double>(jogVel);
        fb.getMoveVelocity = static_cast<double>(moveVel);
        fb.absMoveTarget = static_cast<double>(absTarget);
        fb.relMoveTarget = static_cast<double>(relTarget);

        axis->applyFeedback(fb);
    }

    // ==================================================================
    // Phase 7 (新增): 急停状态注入
    //
    // 读取 M130 ESTOP_ACTIVE → EmergencyStopController::applyFeedback()
    //
    // 设计依据:
    //   《pollFeedback 龙门状态 & 系统状态反馈 — 详细设计计划》§1 Phase 7
    // ==================================================================
    {
        const bool estopActive = m_device->readBool(regFbEmergencyStopActive());
        ctx.emergencyStopController().applyFeedback(estopActive);
    }

    // ==================================================================
    // Phase 8 (新增): 龙门状态注入
    //
    // 读取 D100 STATE / M125 LINKAGE_STATE / D180 GANTRY_ERROR_CODE
    // → 构造 GantryFeedback → 注入 GantryCouplingController & GantryPowerController
    //
    // 设计依据:
    //   《pollFeedback 龙门状态 & 系统状态反馈 — 详细设计计划》§1 Phase 8
    // ==================================================================
    {
        const bool gantryEnabled  = m_device->readInt16(regFbState(AxisId::X)) != 0;
        const bool gantryCoupled  = m_device->readBool(regFbLinkageState());
        const int  gantryErrCode  = m_device->readInt16(regFbGantryErrorCode());

        const GantryFeedback fb{gantryEnabled, gantryCoupled, gantryErrCode};

        ctx.gantryCouplingController().applyFeedback(fb);
        ctx.gantryPowerController().applyFeedback(fb);
    }
}

// =============================================================================
// PendingEdge 队列管理 — 内联实现
// =============================================================================

inline std::deque<PendingEdge>::iterator ModbusSystemDriver::findEdgeByAddress(const protocol::RegisterInfo* reg) {
    return std::find_if(
        m_pendingEdges.begin(), m_pendingEdges.end(),
        [reg](const PendingEdge& e) { return e.reg && reg && e.reg->address == reg->address; }
    );
}

inline std::deque<PendingEdge>::const_iterator ModbusSystemDriver::findEdgeByAddress(const protocol::RegisterInfo* reg) const {
    return std::find_if(
        m_pendingEdges.begin(), m_pendingEdges.end(),
        [reg](const PendingEdge& e) { return e.reg && reg && e.reg->address == reg->address; }
    );
}

inline void ModbusSystemDriver::enqueueEdge(const protocol::RegisterInfo* reg) {
    if (!reg) return;
    if (findEdgeByAddress(reg) != m_pendingEdges.end()) return;
    PendingEdge edge;
    edge.reg = reg;
    edge.state = PendingEdge::State::Idle;
    m_pendingEdges.push_back(edge);
}

inline PendingEdge ModbusSystemDriver::dequeueEdge() {
    if (m_pendingEdges.empty()) return {};
    PendingEdge edge = m_pendingEdges.front();
    m_pendingEdges.pop_front();
    return edge;
}

inline bool ModbusSystemDriver::isEdgePending(const protocol::RegisterInfo* reg) const {
    if (!reg) return false;
    return findEdgeByAddress(reg) != m_pendingEdges.end();
}

inline size_t ModbusSystemDriver::pendingEdgeCount() const {
    return m_pendingEdges.size();
}

inline void ModbusSystemDriver::clearPendingEdges() {
    m_pendingEdges.clear();
}

inline void ModbusSystemDriver::advanceTime(std::chrono::milliseconds ms) {
    auto* fake = dynamic_cast<FakeClock*>(m_clock.get());
    if (fake) { fake->advance(ms); }
}

inline CommunicationResult ModbusSystemDriver::sendEdgeTrigger(const protocol::RegisterInfo& reg) {
    if (!m_device) {
        return CommunicationResult{
            CommunicationResult::Status::Disconnected, 0,
            "ModbusSystemDriver::sendEdgeTrigger: No PlcDevice bound"
        };
    }
    auto result = m_device->writeBool(reg, true);
    if (!result.ok()) { return result; }
    PendingEdge edge;
    edge.reg = &reg;
    edge.state = PendingEdge::State::WroteOn;
    edge.onTime = m_clock->now();
    m_pendingEdges.push_back(edge);
    return result;
}

inline void ModbusSystemDriver::servicePendingEdgeTriggers() {
    if (!m_device) return;
    if (m_pendingEdges.empty()) return;
    const auto now = m_clock->now();
    for (auto& edge : m_pendingEdges) {
        if (edge.state != PendingEdge::State::WroteOn) continue;
        if (!edge.reg) continue;
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - edge.onTime);
        if (elapsed.count() >= EDGE_TRIGGER_PULSE_MS) {
            m_device->writeBool(*edge.reg, false);
            edge.state = PendingEdge::State::WroteOff;
        }
    }
    m_pendingEdges.erase(
        std::remove_if(m_pendingEdges.begin(), m_pendingEdges.end(),
            [](const PendingEdge& e) { return e.state == PendingEdge::State::WroteOff; }),
        m_pendingEdges.end()
    );
}

} // namespace plc
