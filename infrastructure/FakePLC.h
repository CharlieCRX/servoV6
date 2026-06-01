#ifndef FAKE_PLC_H
#define FAKE_PLC_H

#include "../domain/command/SystemCommand.h"
#include "../domain/entity/Axis.h"
#include "../domain/entity/AxisId.h"
#include "../domain/gantry/GantryFeedback.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// --- 急停仿真相关 ---

/// @brief 急停生效仿真延迟（对应 PLC 扫描周期 × N）
constexpr int EMERGENCY_STOP_ENGAGE_DELAY_MS = 50;

/// @brief 急停解除仿真延迟
constexpr int EMERGENCY_STOP_RELEASE_DELAY_MS = 50;

/// @brief 龙门联动允许的最大位置差（mm）
static constexpr double GANTRY_MAX_POSITION_DELTA = 0.1;

/// @brief 龙门联动超差报警阈值（联动建立后持续监测，mm）
static constexpr double GANTRY_COUPLED_POSITION_DELTA_ALARM = 0.5;

/// @brief 龙门物理状态快照（每个 tick 从 X1/X2 聚合刷新）
struct GantryPhysicalState {
    bool x1Enabled = false;
    bool x2Enabled = false;
    bool x1Stationary = true;
    bool x2Stationary = true;
    double positionDelta = 0.0;  // |X1.absPos - X2.absPos|
    bool x1HasAlarm = false;
    bool x2HasAlarm = false;
    bool emergencyStopActive = false;
};

/**
 * @brief 虚拟 PLC 仿真器（物理引擎模拟 + 急停硬件仿真 + 龙门硬件仿真）
 *
 * 设计职责：仿真一个独立硬件 PLC 的物理行为（运动学演算、限位检测、使能延迟、
 *            急停命令/状态分离寄存器、龙门命令/反馈寄存器）。
 *
 * ╔══════════════════════════════════════════════════════════════╗
 * ║  分组架构：每个 SystemContext 绑定一个独立的 FakePLC 实例   ║
 * ║                                                              ║
 * ║  GroupA (Machine_A)          GroupB (Machine_B)              ║
 * ║  ┌─────────────────┐        ┌─────────────────┐             ║
 * ║  │ FakePLC_A       │        │ FakePLC_B       │             ║
 * ║  │  Y: 独立物理态   │        │  Y: 独立物理态   │             ║
 * ║  │  Z: 独立物理态   │        │  Z: 独立物理态   │             ║
 * ║  │  ...            │        │  ...            │             ║
 * ║  └─────────────────┘        └─────────────────┘             ║
 * ║        ↑                          ↑                         ║
 * ║  FakeAxisDriver_A          FakeAxisDriver_B                 ║
 * ║        ↑                          ↑                         ║
 * ║  SystemContext_A            SystemContext_B                 ║
 * ╚══════════════════════════════════════════════════════════════╝
 *
 * 每个 FakePLC 内部独立维护 6 个轴的物理状态寄存器。
 * 不同 FakePLC 实例之间完全隔离 —— GroupA 的指令不会影响 GroupB 的轴。
 *
 * --- 急停仿真模型 ---
 *
 * 对应真实 PLC 的两个独立寄存器：
 *   - 命令寄存器（"设备急停"）：Domain 写入，PLC 读取
 *   - 状态寄存器（"设备急停中"）：PLC 写入，Domain 读取
 *
 * 两者之间存在硬件延迟：
 *   - 命令写入激活 -> 经过 EMERGENCY_STOP_ENGAGE_DELAY_MS -> 状态寄存设置为 true
 *   - 命令写入解除 -> 经过 EMERGENCY_STOP_RELEASE_DELAY_MS -> 状态寄存设置为 false
 *
 * 急停生效时 PLC 行为（与真实 PLC 一致）：
 *   1. 所有轴立即掉电（状态强制 Disabled）
 *   2. 所有运动立即停止
 *   3. EnableCommand 在急停激活期间被忽略
 *
 * --- 龙门反馈仿真模型 ---
 *
 * 对应真实 PLC 的龙门相关寄存器：
 *   - GantryPowerCommand{enable}    -> 写入使能命令，延迟后 -> GantryFeedback::enable
 *   - GantryCouplingCommand{couple} -> 写入耦合命令，延迟后 -> GantryFeedback::isCoupled
 *   - GantryFeedback::errorCode     -> 耦合条件不满足时的错误码
 *
 * 仿真延迟：
 *   - GANTRY_POWER_DELAY_MS    (150ms) 电机使能延迟
 *   - GANTRY_COUPLING_DELAY_MS (100ms) 耦合/解耦延迟
 *
 * PLC 在每个 tick 中执行扫描周期：刷新物理状态 -> 条件检查 -> 反馈生成。
 * 联动建立后会持续监测：超差/报警/掉电/急停任一触发即自动解耦。
 *
 * 使用示例：
 *   FakePLC plcA, plcB;  // 两台独立硬件
 *   plcA.onCommand(AxisId::Y, EnableCommand{true});  // 只影响 plcA 的 Y 轴
 *   plcB.tick(10);  // 只推进 plcB 的物理引擎
 */
class FakePLC {
public:
    FakePLC() {
        m_axes[AxisId::Y] = AxisStateInternal{};
        m_axes[AxisId::Z] = AxisStateInternal{};
        m_axes[AxisId::R] = AxisStateInternal{};
        m_axes[AxisId::X] = AxisStateInternal{};
        m_axes[AxisId::X1] = AxisStateInternal{};
        m_axes[AxisId::X2] = AxisStateInternal{};
    }

    // ========== 核心对外接口（多轴签名） ==========

    /**
     * @brief 向指定轴下发指令
     */
    void onCommand(AxisId id, const AxisCommand& cmd) {
        std::visit([this, id](auto&& arg) {
            this->processCommand(id, arg);
        }, cmd);
    }

    /**
     * @brief 下发龙门相关命令（GantryCouplingCommand / GantryPowerCommand）
     *
     * 龙门命令不绑定特定轴，由 GantryOrchestrator 通过 Driver -> PLC 路径下发。
     */
    void onGantryCommand(const GantryCouplingCommand& cmd) {
        m_gantryCouplingCmdPending = true;
        m_gantryCouplingTarget = cmd.enableCoupling;
        m_gantryCouplingTimer = 0;
        m_gantryFeedbackLocked = false;  // 新命令到来，让 PLC 重新接管
    }

    void onGantryCommand(const GantryPowerCommand& cmd) {
        m_gantryPowerCmdPending = true;
        m_gantryPowerTarget = cmd.enable;
        m_gantryPowerTimer = 0;
    }

    /**
     * @brief 强制设置"设备急停"命令寄存器（用于测试注入命令来源）
     */
    void forceEmergencyStopCommand(bool active) {
        m_emergencyStopCmdPending = active;
        m_emergencyStopTimer = 0;
    }

    /**
     * @brief 物理引擎 heartbeat
     *
     * 执行顺序：
     *   1. 急停延迟状态机
     *   2. 各轴状态跃迁 -> 运动学推演 -> 限位检测（先推演轴，让 X1/X2 位置更新）
     *   3. 龙门状态机（依赖最新 X1/X2 物理状态 -> 必须在轴推演之后执行）
     */
    void tick(int ms) {
        // --- 急停延迟状态机 ---
        tickEmergencyStop(ms);

        // --- 各轴独立推演 ---
        for (auto& [id, axis] : m_axes) {
            LOG_TRACE_EVERY_N(50, LogLayer::HAL, "PLC",
                "Tick axis=" + axisIdToString(id) + " pos=" + std::to_string(axis.feedback.absPos));

            if (axis.stop_requested) {
                axis.feedback.state = AxisState::Idle;
                axis.stop_requested = false;
            }

            updateStateTransitions(axis, ms);
            updateKinematics(axis, ms);
            checkHardwareLimits(axis);

            axis.feedback.relPos = axis.feedback.absPos - axis.feedback.relZeroAbsPos;

            LOG_TRACE_EVERY_N(50, LogLayer::HAL, "PLC",
                "relPos update: axis=" + axisIdToString(id)
                + " abs=" + std::to_string(axis.feedback.absPos)
                + " base=" + std::to_string(axis.feedback.relZeroAbsPos)
                + " rel=" + std::to_string(axis.feedback.relPos));
        }

        // --- 龙门状态机（依赖最新的X1/X2物理状态 -> 必须在轴推演之后执行） ---
        tickGantry(ms);
    }

    /**
     * @brief 读取指定轴的当前反馈
     */
    AxisFeedback getFeedback(AxisId id) const {
        return m_axes.at(id).feedback;
    }

    // ========== 龙门仿真接口 ==========

    /**
     * @brief 获取龙门反馈快照（用于 pollFeedback 注入领域实体）
     */
    GantryFeedback getGantryFeedback() const {
        return m_gantryFeedback;
    }

    /**
     * @brief 强制设置龙门反馈（用于测试注入龙门物理状态）
     *
     * bypass 延迟，直接覆盖反馈寄存器。设置后锁定自动刷新。
     */
    void forceGantryFeedback(const GantryFeedback& fb) {
        m_gantryFeedback = fb;
        m_gantryPowerCmdPending = false;
        m_gantryCouplingCmdPending = false;
        m_gantryFeedbackLocked = true;
    }

    /**
     * @brief 注入龙门耦合错误码（用于模拟 PLC 拒绝耦合）
     *
     * 仅在 GantryCouplingCommand 处理过程中生效，
     * 模拟 PLC 检测到 X1/X2 未使能/未静止/超差等条件时写入的错误码。
     */
    void forceGantryCouplingError(int errorCode) {
        m_gantryFeedback.errorCode = errorCode;
        m_gantryFeedbackLocked = true;
    }

    // ========== 急停仿真接口 ==========

    /**
     * @brief 获取 PLC 的"设备急停中"状态寄存器值
     *
     * 对应真实 PLC 的：设备急停中
     * 用于 EmergencyStopController::applyFeedback() 的反馈源
     */
    bool getEmergencyStopFeedback() const {
        return m_emergencyStoppedReg;
    }

    /**
     * @brief 强制设置"设备急停中"状态（用于测试注入物理急停按钮）
     *
     * bypass 延迟，直接置位状态寄存器
     */
    void forceEmergencyStopFeedback(bool stopped) {
        m_emergencyStoppedReg = stopped;
    }

    // ========== 仿真环境配置接口 ==========

    void forceState(AxisId id, AxisState s) {
        m_axes.at(id).feedback.state = s;
    }

    void setSimulatedMoveVelocity(AxisId id, double v) {
        m_axes.at(id).move_velocity = std::abs(v);
        m_axes.at(id).feedback.getMoveVelocity = std::abs(v);
    }

    void setSimulatedJogVelocity(AxisId id, double v) {
        m_axes.at(id).jog_velocity = std::abs(v);
        m_axes.at(id).feedback.getjogVelocity = std::abs(v);
    }

    void setLimits(AxisId id, double pos, double neg) {
        auto& axis = m_axes.at(id);
        axis.feedback.posLimitValue = pos;
        axis.feedback.negLimitValue = neg;
    }

    void setAbsolutePosition(AxisId id, double pos) {
        m_axes.at(id).feedback.absPos = pos;
    }

    /**
     * @brief 重置所有轴 + 急停状态 + 龙门状态到初始值
     */
    void resetAll() {
        for (auto& [id, axis] : m_axes) {
            axis = AxisStateInternal{};
        }
        m_emergencyStopCmdPending = false;
        m_emergencyStopTimer = 0;
        m_emergencyStoppedReg = false;
        m_gantryFeedback = GantryFeedback{false, false, 0};
        m_gantryPowerCmdPending = false;
        m_gantryPowerTimer = 0;
        m_gantryCouplingCmdPending = false;
        m_gantryCouplingTimer = 0;
        m_gantryPhysical = GantryPhysicalState{};
        m_gantryFeedbackLocked = false;
    }

private:
    struct AxisStateInternal {
        AxisFeedback feedback{ AxisState::Disabled, 0.0, 0.0, 0.0, false, false, 1000.0, -1000.0 };
        double move_velocity = 50.0;
        double jog_velocity = 10.0;
        int enable_timer_ms = 0;
        double target_pos = 0.0;
        Direction jog_dir = Direction::Forward;
        bool stop_requested = false;
    };

    std::unordered_map<AxisId, AxisStateInternal> m_axes;

    // ========== 急停寄存器（命令/状态分离） ==========

    /// @brief "设备急停"命令寄存器（对应 PLC 输入，Driver 写入）
    bool m_emergencyStopCmdPending = false;

    /// @brief 延迟定时器（从命令变更到状态变更的过渡时间）
    int m_emergencyStopTimer = 0;

    /// @brief "设备急停中"状态寄存器（对应 PLC 输出，Driver 读取反馈）
    bool m_emergencyStoppedReg = false;

    // ========== 龙门寄存器（命令/反馈分离） ==========

    /// @brief 龙门反馈寄存器
    GantryFeedback m_gantryFeedback{false, false, 0};

    /// @brief 龙门物理状态快照（每个 tick 从 X1/X2 聚合刷新）
    GantryPhysicalState m_gantryPhysical{};

    /// @brief 测试注入锁定标志（true 时跳过 tickGantry 自动刷新）
    bool m_gantryFeedbackLocked = false;

    /// @brief 龙门电机使能命令延迟仿真
    bool m_gantryPowerCmdPending = false;
    bool m_gantryPowerTarget = false;
    int m_gantryPowerTimer = 0;
    static constexpr int GANTRY_POWER_DELAY_MS = 150;

    /// @brief 龙门耦合/解耦命令延迟仿真
    bool m_gantryCouplingCmdPending = false;
    bool m_gantryCouplingTarget = false;
    int m_gantryCouplingTimer = 0;
    static constexpr int GANTRY_COUPLING_DELAY_MS = 100;

    // ========== 内部方法 ==========

    static std::string axisIdToString(AxisId id) {
        switch (id) {
            case AxisId::Y:  return "Y";
            case AxisId::Z:  return "Z";
            case AxisId::R:  return "R";
            case AxisId::X:  return "X";
            case AxisId::X1: return "X1";
            case AxisId::X2: return "X2";
        }
        return "?";
    }

    static constexpr int ENABLE_DELAY_MS = 150;

    /// @brief 急停延迟状态机（对应用真实 PLC 的扫描周期延迟）
    void tickEmergencyStop(int ms) {
        if (m_emergencyStopCmdPending != m_emergencyStoppedReg) {
            m_emergencyStopTimer += ms;
            int requiredDelay = m_emergencyStopCmdPending
                                ? EMERGENCY_STOP_ENGAGE_DELAY_MS
                                : EMERGENCY_STOP_RELEASE_DELAY_MS;
            if (m_emergencyStopTimer >= requiredDelay) {
                m_emergencyStoppedReg = m_emergencyStopCmdPending;
                m_emergencyStopTimer = 0;
            }
        }

        // 急停生效：所有轴强制掉电 + 停止 + 解除龙门联动
        if (m_emergencyStoppedReg) {
            for (auto& [id, axis] : m_axes) {
                if (axis.feedback.state != AxisState::Disabled) {
                    axis.feedback.state = AxisState::Disabled;
                }
                axis.enable_timer_ms = 0;
                axis.stop_requested = false;
            }

            // 主动解除龙门联动（急停具有最高优先级）
            m_gantryFeedback.isCoupled = false;
            m_gantryFeedback.errorCode = 0;  // 无错误

            // 取消所有待处理的龙门命令
            m_gantryPowerCmdPending = false;
            m_gantryPowerTimer = 0;
            m_gantryCouplingCmdPending = false;
            m_gantryCouplingTimer = 0;

            // 解除测试注入锁定（急停覆盖一切）
            m_gantryFeedbackLocked = false;
        }
    }

    /// @brief 刷新龙门物理状态快照（每个 tick 从 X1/X2 聚合）
    void refreshGantryPhysicalState() {
        const auto& x1 = m_axes.at(AxisId::X1).feedback;
        const auto& x2 = m_axes.at(AxisId::X2).feedback;

        m_gantryPhysical.x1Enabled = (x1.state != AxisState::Disabled
                                   && x1.state != AxisState::Unknown);
        m_gantryPhysical.x2Enabled = (x2.state != AxisState::Disabled
                                   && x2.state != AxisState::Unknown);

        m_gantryPhysical.x1Stationary = (x1.state == AxisState::Idle);
        m_gantryPhysical.x2Stationary = (x2.state == AxisState::Idle);

        m_gantryPhysical.x1HasAlarm = (x1.state == AxisState::Error)
                                   || x1.posLimit || x1.negLimit;
        m_gantryPhysical.x2HasAlarm = (x2.state == AxisState::Error)
                                   || x2.posLimit || x2.negLimit;

        m_gantryPhysical.positionDelta = std::abs(x1.absPos - x2.absPos);

        m_gantryPhysical.emergencyStopActive = m_emergencyStoppedReg;
    }

    /// @brief 龙门联动前置条件检查
    /// @return 0=通过, 1=位置超差, 2=X1未使能, 3=X2未使能, 4=X1未静止, 5=X2未静止, 999=其他错误
    int checkCouplingConditions() const {
        // 1. X1 使能检查
        if (!m_gantryPhysical.x1Enabled) return 2; // X1NotEnabled

        // 2. X2 使能检查
        if (!m_gantryPhysical.x2Enabled) return 3; // X2NotEnabled

        // 3. X1 静止检查
        if (!m_gantryPhysical.x1Stationary) return 4; // X1NotStationary

        // 4. X2 静止检查
        if (!m_gantryPhysical.x2Stationary) return 5; // X2NotStationary

        // 5. 位置差检查
        if (m_gantryPhysical.positionDelta >= GANTRY_MAX_POSITION_DELTA)
            return 1; // PositionToleranceExceeded

        // 6. 报警检查
        if (m_gantryPhysical.x1HasAlarm || m_gantryPhysical.x2HasAlarm)
            return 999; // UnknownError

        // 7. 急停检查
        if (m_gantryPhysical.emergencyStopActive) return 999;

        return 0;
    }

    /// @brief 龙门状态机 -- 每个 tick 周期推进（扫描周期模型）
    void tickGantry(int ms) {
        // 测试注入模式：跳过自动刷新，保护测试注入的数据
        if (m_gantryFeedbackLocked) return;

        // 步骤 1：电机使能状态机（可能修改 X1/X2 状态）
        tickGantryPower(ms);

        // 步骤 2：刷新龙门物理状态快照（必须在 power 之后，确保读取最新 X1/X2 状态）
        refreshGantryPhysicalState();

        // 步骤 3：耦合/解耦状态机
        tickGantryCoupling(ms);

        // 步骤 4：联动建立后持续监测
        tickGantryCoupledMonitoring(ms);

        // 步骤 5：同步刷新 GantryFeedback.enable
        m_gantryFeedback.enable = m_gantryPhysical.x1Enabled
                               && m_gantryPhysical.x2Enabled;
    }

    /// @brief 龙门电机使能状态机
    void tickGantryPower(int ms) {
        if (!m_gantryPowerCmdPending) return;

        m_gantryPowerTimer += ms;
        if (m_gantryPowerTimer >= GANTRY_POWER_DELAY_MS) {
            m_gantryPowerCmdPending = false;
            m_gantryPowerTimer = 0;

            // GANTRY_POWER_DELAY_MS 已提供延迟，直接设置目标状态（无需二次延迟）
            auto& x1 = m_axes.at(AxisId::X1);
            auto& x2 = m_axes.at(AxisId::X2);
            auto& x  = m_axes.at(AxisId::X);
            if (m_gantryPowerTarget) {
                // 使能：急停激活时拒绝
                if (!m_emergencyStoppedReg) {
                    if (x1.feedback.state == AxisState::Disabled) {
                        x1.feedback.state = AxisState::Idle;
                    }
                    if (x2.feedback.state == AxisState::Disabled) {
                        x2.feedback.state = AxisState::Idle;
                    }

                    if (x1.feedback.state == AxisState::Idle && x2.feedback.state == AxisState::Idle) {
                        x.feedback.state = AxisState::Idle; // 逻辑龙门轴与物理轴状态保持一致
                    }
                }
            } else {
                // 掉电：无条件
                x1.feedback.state = AxisState::Disabled;
                x2.feedback.state = AxisState::Disabled;
                x.feedback.state = AxisState::Disabled;
            }
        }
    }

    /// @brief 龙门耦合/解耦状态机（含条件检查与拒绝逻辑）
    void tickGantryCoupling(int ms) {
        if (!m_gantryCouplingCmdPending) return;

        m_gantryCouplingTimer += ms;

        if (m_gantryCouplingTarget) {
            // ========== 联动请求 ==========
            if (m_gantryCouplingTimer < GANTRY_COUPLING_DELAY_MS) return;

            int errorCode = checkCouplingConditions();
            if (errorCode != 0) {
                // 条件不满足 -> 拒绝联动，写入错误码
                m_gantryFeedback.errorCode = errorCode;
                m_gantryFeedback.isCoupled = false;
                m_gantryCouplingCmdPending = false;
                m_gantryCouplingTimer = 0;
                return;
            }

            // 所有条件满足 -> 联动成功
            m_gantryFeedback.isCoupled = true;
            m_gantryFeedback.errorCode = 0;
            m_gantryCouplingCmdPending = false;
            m_gantryCouplingTimer = 0;

        } else {
            // ========== 解耦请求（无条件通过） ==========
            if (m_gantryCouplingTimer >= GANTRY_COUPLING_DELAY_MS) {
                m_gantryFeedback.isCoupled = false;
                m_gantryFeedback.errorCode = 0;
                m_gantryCouplingCmdPending = false;
                m_gantryCouplingTimer = 0;
            }
        }
    }

    /// @brief 联动持续监测（联动建立后每个 tick 检查）
    void tickGantryCoupledMonitoring(int /*ms*/) {
        if (!m_gantryFeedback.isCoupled) return;

        bool shouldDecouple = false;
        int errorCode = 0;

        // 1. 轴报警检查（限位触发/Error状态，优先级最高）
        if (m_gantryPhysical.x1HasAlarm || m_gantryPhysical.x2HasAlarm) {
            shouldDecouple = true;
            errorCode = 999;
        }

        // 2. 超差检查（联动建立后阈值放宽至 0.5mm）
        if (!shouldDecouple && m_gantryPhysical.positionDelta >= GANTRY_COUPLED_POSITION_DELTA_ALARM) {
            shouldDecouple = true;
            errorCode = 1; // PositionToleranceExceeded
        }

        // 3. 掉电检查
        if (!shouldDecouple && (!m_gantryPhysical.x1Enabled || !m_gantryPhysical.x2Enabled)) {
            shouldDecouple = true;
            errorCode = m_gantryPhysical.x1Enabled ? 3 : 2;
        }

        // 4. 急停检查
        if (!shouldDecouple && m_gantryPhysical.emergencyStopActive) {
            shouldDecouple = true;
            errorCode = 999;
        }

        if (shouldDecouple) {
            m_gantryFeedback.isCoupled = false;
            m_gantryFeedback.errorCode = errorCode;
        }
    }

    // ========== 命令分发 ==========

    void processCommand(AxisId id, std::monostate) {}

    void processCommand(AxisId id, const StopCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=Stop"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        if (axis.feedback.state == AxisState::Jogging ||
            axis.feedback.state == AxisState::MovingAbsolute ||
            axis.feedback.state == AxisState::MovingRelative) {
            handleStop(axis);
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Stop ignored: axis=" + axisIdToString(id)
                + " not in motion, state=" + std::to_string(static_cast<int>(axis.feedback.state)));
        }
    }

    void processCommand(AxisId id, const EnableCommand& cmd) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=EnableCommand(active=" + (cmd.active ? "true" : "false") + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        // 急停激活时，忽略一切使能命令（模拟真实 PLC AND NOT 设备急停 逻辑）
        if (m_emergencyStoppedReg) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Enable REJECTED: axis=" + axisIdToString(id)
                + " EMERGENCY STOP ACTIVE -- ignoring Enable");
            return;
        }
        if (cmd.active && axis.feedback.state == AxisState::Disabled) {
            axis.enable_timer_ms = 0;
            // 进入 Unknown 过渡态，由 updateStateTransitions 在 ENABLE_DELAY_MS 后置为 Idle
            axis.feedback.state = AxisState::Unknown;
        } else if (!cmd.active) {
            axis.feedback.state = AxisState::Disabled;
        }
    }

    void processCommand(AxisId id, const JogCommand& cmd) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=JogCommand(dir=" + (cmd.dir == Direction::Forward ? "Forward" : "Backward")
            + ", active=" + (cmd.active ? "true" : "false") + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        // 急停激活时，忽略所有运动命令
        if (m_emergencyStoppedReg) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Jog REJECTED: axis=" + axisIdToString(id)
                + " EMERGENCY STOP ACTIVE");
            return;
        }

        // 联动 ON 时，不允许 X1/X2 独立点动
        if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Jog REJECTED: axis=" + axisIdToString(id)
                + " GANTRY COUPLED");
            return;
        }

        if (cmd.active) {
            if (axis.feedback.state == AxisState::Idle) {
                axis.feedback.state = AxisState::Jogging;
                axis.jog_dir = cmd.dir;
            } else {
                LOG_WARN(LogLayer::HAL, "PLC",
                    "Jog REJECTED: axis=" + axisIdToString(id)
                    + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                    + " (requires Idle)");
            }
        } else {
            if (axis.feedback.state == AxisState::Jogging) {
                axis.stop_requested = true;
            }
        }
    }

    void processCommand(AxisId id, const MoveCommand& cmd) {
        auto& axis = m_axes.at(id);

        std::string typeStr = (cmd.type == MoveType::Absolute) ? "Absolute" : "Relative";
        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=MoveCommand(type=" + typeStr
            + ", target=" + std::to_string(cmd.target) + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        // 急停激活时，忽略所有运动命令
        if (m_emergencyStoppedReg) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Move REJECTED: axis=" + axisIdToString(id)
                + " EMERGENCY STOP ACTIVE");
            return;
        }

        // 联动 ON 时，不允许 X1/X2 独立定位
        if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Move REJECTED: axis=" + axisIdToString(id)
                + " GANTRY COUPLED");
            return;
        }

        if (axis.feedback.state == AxisState::Idle) {
            axis.feedback.state = (cmd.type == MoveType::Absolute) ?
                                   AxisState::MovingAbsolute : AxisState::MovingRelative;
            if (cmd.type == MoveType::Absolute) {
                axis.target_pos = cmd.target;
            } else {
                axis.target_pos = axis.feedback.absPos + cmd.target;
            }
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "Move REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle)");
        }
    }

    void processCommand(AxisId id, const ZeroAbsoluteCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=ZeroAbsolute"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
            + " absPos=" + std::to_string(axis.feedback.absPos));

        if (axis.feedback.state == AxisState::Idle || 
            axis.feedback.state == AxisState::Disabled) {
            double oldAbs = axis.feedback.absPos;
            axis.feedback.absPos = 0.0;
            LOG_DEBUG(LogLayer::HAL, "PLC",
                "ZeroAbsolute executed: axis=" + axisIdToString(id)
                + " abs " + std::to_string(oldAbs) + " -> 0.0");
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "ZeroAbsolute REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle or Disabled)");
        }
    }
    
    void processCommand(AxisId id, const SetRelativeZeroCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=SetRelativeZero"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
            + " absPos=" + std::to_string(axis.feedback.absPos)
            + " currentBase=" + std::to_string(axis.feedback.relZeroAbsPos));

        if (axis.feedback.state == AxisState::Idle ||
            axis.feedback.state == AxisState::Disabled) {
            double oldBase = axis.feedback.relZeroAbsPos;
            axis.feedback.relZeroAbsPos = axis.feedback.absPos;
            LOG_DEBUG(LogLayer::HAL, "PLC",
                "SetRelativeZero executed: axis=" + axisIdToString(id)
                + " base " + std::to_string(oldBase) + " -> " + std::to_string(axis.feedback.relZeroAbsPos));
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "SetRelativeZero REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle or Disabled)");
        }
    }
    
    void processCommand(AxisId id, const ClearRelativeZeroCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=ClearRelativeZero"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
            + " absPos=" + std::to_string(axis.feedback.absPos)
            + " currentBase=" + std::to_string(axis.feedback.relZeroAbsPos));

        if (axis.feedback.state == AxisState::Idle ||
            axis.feedback.state == AxisState::Disabled) {
            double oldBase = axis.feedback.relZeroAbsPos;
            axis.feedback.relZeroAbsPos = 0.0;
            LOG_DEBUG(LogLayer::HAL, "PLC",
                "ClearRelativeZero executed: axis=" + axisIdToString(id)
                + " base " + std::to_string(oldBase) + " -> 0.0");
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "ClearRelativeZero REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle or Disabled)");
        }
    }

    void processCommand(AxisId id, const SetJogVelocityCommand& cmd) {
        auto& axis = m_axes.at(id);
        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=SetJogVelocityCommand(velocity=" + std::to_string(cmd.velocity) + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));
        axis.jog_velocity = std::abs(cmd.velocity);
        axis.feedback.getjogVelocity = std::abs(cmd.velocity);
    }

    void processCommand(AxisId id, const SetMoveVelocityCommand& cmd) {
        auto& axis = m_axes.at(id);
        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=SetMoveVelocityCommand(velocity=" + std::to_string(cmd.velocity) + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));
        axis.move_velocity = std::abs(cmd.velocity);
        axis.feedback.getMoveVelocity = std::abs(cmd.velocity);
    }

    void processCommand(AxisId id, const SetAbsTargetCommand& cmd) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=SetAbsTargetCommand(target=" + std::to_string(cmd.target) + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        // 镜像 PLC ABS_TARGET D 寄存器
        axis.feedback.absMoveTarget = cmd.target;
    }

    void processCommand(AxisId id, const TriggerAbsMoveCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=TriggerAbsMoveCommand"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
            + " target=" + std::to_string(axis.feedback.absMoveTarget));

        // 急停激活时，忽略所有运动命令
        if (m_emergencyStoppedReg) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerAbsMove REJECTED: axis=" + axisIdToString(id)
                + " EMERGENCY STOP ACTIVE");
            return;
        }

        // 联动 ON 时，不允许 X1/X2 独立定位
        if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerAbsMove REJECTED: axis=" + axisIdToString(id)
                + " GANTRY COUPLED");
            return;
        }

        if (axis.feedback.state == AxisState::Idle) {
            axis.feedback.state = AxisState::MovingAbsolute;
            axis.target_pos = axis.feedback.absMoveTarget;
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerAbsMove REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle)");
        }
    }

    void processCommand(AxisId id, const SetRelTargetCommand& cmd) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=SetRelTargetCommand(distance=" + std::to_string(cmd.distance) + ")"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state)));

        // 镜像 PLC REL_TARGET D 寄存器
        axis.feedback.relMoveTarget = cmd.distance;
    }

    void processCommand(AxisId id, const TriggerRelMoveCommand&) {
        auto& axis = m_axes.at(id);

        LOG_DEBUG(LogLayer::HAL, "PLC",
            "process: axis=" + axisIdToString(id)
            + " cmd=TriggerRelMoveCommand"
            + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
            + " relTarget=" + std::to_string(axis.feedback.relMoveTarget)
            + " absPos=" + std::to_string(axis.feedback.absPos));

        // 急停激活时，忽略所有运动命令
        if (m_emergencyStoppedReg) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerRelMove REJECTED: axis=" + axisIdToString(id)
                + " EMERGENCY STOP ACTIVE");
            return;
        }

        // 联动 ON 时，不允许 X1/X2 独立定位
        if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerRelMove REJECTED: axis=" + axisIdToString(id)
                + " GANTRY COUPLED");
            return;
        }

        if (axis.feedback.state == AxisState::Idle) {
            axis.feedback.state = AxisState::MovingRelative;
            axis.target_pos = axis.feedback.absPos + axis.feedback.relMoveTarget;
        } else {
            LOG_WARN(LogLayer::HAL, "PLC",
                "TriggerRelMove REJECTED: axis=" + axisIdToString(id)
                + " state=" + std::to_string(static_cast<int>(axis.feedback.state))
                + " (requires Idle)");
        }
    }

    void processCommand(AxisId id, const GantryCouplingCommand& cmd) {
        (void)id;
        LOG_DEBUG(LogLayer::HAL, "PLC",
            std::string("process: cmd=GantryCouplingCommand(couple=") + (cmd.enableCoupling ? "true" : "false") + ")");
        onGantryCommand(cmd);
    }

    void processCommand(AxisId id, const GantryPowerCommand& cmd) {
        (void)id;
        LOG_DEBUG(LogLayer::HAL, "PLC",
            std::string("process: cmd=GantryPowerCommand(enable=") + (cmd.enable ? "true" : "false") + ")");
        onGantryCommand(cmd);
    }

    // ========== 运动控制辅助 ==========

    void handleStop(AxisStateInternal& axis) {
        if (axis.feedback.state == AxisState::Jogging ||
            axis.feedback.state == AxisState::MovingAbsolute ||
            axis.feedback.state == AxisState::MovingRelative) {
            axis.stop_requested = true;
        }
    }

    void updateStateTransitions(AxisStateInternal& axis, int ms) {
        if (axis.feedback.state == AxisState::Unknown) {
            axis.enable_timer_ms += ms;
            if (axis.enable_timer_ms >= ENABLE_DELAY_MS) {
                axis.feedback.state = AxisState::Idle;
            }
        }
    }

    void updateKinematics(AxisStateInternal& axis, int ms) {
        if (axis.feedback.state == AxisState::MovingAbsolute ||
            axis.feedback.state == AxisState::MovingRelative) {
            double step = (axis.move_velocity * ms) / 1000.0;
            double diff = axis.target_pos - axis.feedback.absPos;
            if (std::abs(diff) <= step) {
                axis.feedback.absPos = axis.target_pos;
                axis.feedback.state = AxisState::Idle;
            } else {
                axis.feedback.absPos += (diff > 0) ? step : -step;
            }
        } else if (axis.feedback.state == AxisState::Jogging) {
            double step = (axis.jog_velocity * ms) / 1000.0;
            if (axis.jog_dir == Direction::Forward) {
                axis.feedback.absPos += step;
            } else {
                axis.feedback.absPos -= step;
            }
        }
    }

    void checkHardwareLimits(AxisStateInternal& axis) {
        if (axis.feedback.absPos >= axis.feedback.posLimitValue) {
            if (!axis.feedback.posLimit) {
                LOG_ERROR(LogLayer::HAL, "PLC", "LIMIT TRIGGERED at Positive Soft Limit: " + std::to_string(axis.feedback.posLimitValue));
            }
            axis.feedback.posLimit = true;
            axis.feedback.absPos = axis.feedback.posLimitValue;
            forceStopIfMoving(axis);
        } else {
            axis.feedback.posLimit = false;
        }
        if (axis.feedback.absPos <= axis.feedback.negLimitValue) {
            if (!axis.feedback.negLimit) {
                LOG_ERROR(LogLayer::HAL, "PLC", "LIMIT TRIGGERED at Negative Soft Limit: " + std::to_string(axis.feedback.negLimitValue));
            }
            axis.feedback.negLimit = true;
            axis.feedback.absPos = axis.feedback.negLimitValue;
            forceStopIfMoving(axis);
        } else {
            axis.feedback.negLimit = false;
        }
    }

    void forceStopIfMoving(AxisStateInternal& axis) {
        if (axis.feedback.state == AxisState::MovingAbsolute ||
            axis.feedback.state == AxisState::MovingRelative ||
            axis.feedback.state == AxisState::Jogging) {
            axis.feedback.state = AxisState::Idle;
        }
    }
};

#endif // FAKE_PLC_H
