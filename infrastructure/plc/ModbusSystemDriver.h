#pragma once

#include "infrastructure/ISystemDriver.h"
#include "infrastructure/plc/protocol/RegisterAddressAll.h"
#include "infrastructure/plc/protocol/PlcDevice.h"
#include "infrastructure/plc/protocol/IModbusClient.h"
#include "infrastructure/plc/protocol/PlcPoller.h"
#include "infrastructure/utils/IClock.h"
#include "infrastructure/utils/overloaded.h"
#include "infrastructure/plc/AxisStateDeriver.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/ContextRejection.h"
#include "domain/entity/SystemContext.h"
#include "domain/gantry/GantryFeedback.h"
#include <cstdint>
#include <deque>
#include <algorithm>
#include <memory>
#include <chrono>

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
        return CommunicationResult::Disconnected();
    }

    return std::visit(overloaded{
        [this](const AxisCommandWithId& ac) -> CommunicationResult {
            const auto id = ac.id;
            return std::visit(overloaded{
                [](std::monostate) -> CommunicationResult {
                    return CommunicationResult::Sent();
                },
                [this, id](const JogCommand& j) -> CommunicationResult {
                    if (j.dir == Direction::Forward) {
                        return m_device->writeBool(regCmdJogFwd(id), j.active);
                    } else {
                        return m_device->writeBool(regCmdJogBwd(id), j.active);
                    }
                },
                [this, id](const StopCommand&) -> CommunicationResult {
                    auto r1 = m_device->writeBool(regCmdJogFwd(id), false);
                    auto r2 = m_device->writeBool(regCmdJogBwd(id), false);
                    if (!r1.ok()) return r1;
                    if (!r2.ok()) return r2;
                    return CommunicationResult::Sent();
                },
                [this, id](const EnableCommand& e) -> CommunicationResult {
                    return m_device->writeBool(regCmdEnable(id), e.active);
                },
                [this, id](const SetJogVelocityCommand& v) -> CommunicationResult {
                    return m_device->writeFloat(regCmdJogSpeed(id),
                                                static_cast<float>(v.velocity));
                },
                [this, id](const SetMoveVelocityCommand& v) -> CommunicationResult {
                    return m_device->writeFloat(regCmdMoveSpeed(id),
                                                static_cast<float>(v.velocity));
                },
                [this, id](const SetAbsTargetCommand& t) -> CommunicationResult {
                    return m_device->writeFloat(regCmdAbsTarget(id),
                                                static_cast<float>(t.target));
                },
                [this, id](const SetRelTargetCommand& t) -> CommunicationResult {
                    return m_device->writeFloat(regCmdRelTarget(id),
                                                static_cast<float>(t.distance));
                },
                [this, id](const TriggerAbsMoveCommand&) -> CommunicationResult {
                    return sendEdgeTrigger(regCmdAbsTrigger(id));
                },
                [this, id](const TriggerRelMoveCommand&) -> CommunicationResult {
                    return sendEdgeTrigger(regCmdRelTrigger(id));
                },
                [this, id](const ZeroAbsoluteCommand&) -> CommunicationResult {
                    return sendEdgeTrigger(regCmdClearAbsPos(id));
                },
                [this, id](const SetRelativeZeroCommand&) -> CommunicationResult {
                    return sendEdgeTrigger(regCmdSetRelZero(id));
                },
                [this, id](const ClearRelativeZeroCommand&) -> CommunicationResult {
                    return sendEdgeTrigger(regCmdClearRelZero(id));
                },
                [](const MoveCommand&) -> CommunicationResult {
                    return CommunicationResult::Sent();
                }
            }, ac.cmd);
        },
        [this](const GantryCouplingCommand& g) -> CommunicationResult {
            return m_device->writeBool(regGantryCoupling(), g.enableCoupling);
        },
        [this](const GantryPowerCommand& g) -> CommunicationResult {
            return m_device->writeBool(regCmdEnable(AxisId::X), g.enable);
        },
        [this](const EmergencyStopCommand& e) -> CommunicationResult {
            return m_device->writeBool(regEmergencyStopTrigger(), e.active);
        }
    }, cmd);
}

// ---------- ISystemDriver::pollFeedback ----------

inline void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    servicePendingEdgeTriggers();

    if (!m_modbusClient || !m_poller) {
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

        const int16_t stateRaw = m_device->readInt16(regFbState(id));
        const int16_t alarmCode = m_device->readInt16(regFbAlarmCode(id));
        const bool absMoving = m_device->readBool(regFbAbsMoving(id));
        const bool relMoving = m_device->readBool(regFbRelMoving(id));
        const bool jogging = m_device->readBool(regFbJogging(id));
        const float absPos = m_device->readFloat(regFbAbsPos(id));
        const float relPos = m_device->readFloat(regFbRelPos(id));

        const AxisState derivedState = deriveAxisState(
            stateRaw, alarmCode, absMoving, relMoving, jogging);

        axis->applyPlcFeedback(derivedState,
                               static_cast<double>(absPos),
                               static_cast<double>(relPos));
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
