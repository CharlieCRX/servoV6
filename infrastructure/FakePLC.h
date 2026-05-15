#ifndef FAKE_PLC_H
#define FAKE_PLC_H

#include "../domain/command/SystemCommand.h"
#include "../domain/entity/Axis.h"
#include "../domain/entity/AxisId.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

// --- 急停仿真相关 ---

/// @brief 急停生效仿真延迟（对应 PLC 扫描周期 × N）
constexpr int EMERGENCY_STOP_ENGAGE_DELAY_MS = 50;

/// @brief 急停解除仿真延迟
constexpr int EMERGENCY_STOP_RELEASE_DELAY_MS = 50;

/**
 * @brief 虚拟 PLC 仿真器（物理引擎模拟 + 急停硬件仿真）
 *
 * 设计职责：仿真一个独立硬件 PLC 的物理行为（运动学演算、限位检测、使能延迟、
 *            急停命令/状态分离寄存器）。
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
 *   - 命令写入激活 → 经过 EMERGENCY_STOP_ENGAGE_DELAY_MS → 状态寄存设置为 true
 *   - 命令写入解除 → 经过 EMERGENCY_STOP_RELEASE_DELAY_MS → 状态寄存设置为 false
 *
 * 急停生效时 PLC 行为（与真实 PLC 一致）：
 *   1. 所有轴立即掉电（状态强制 Disabled）
 *   2. 所有运动立即停止
 *   3. EnableCommand 在急停激活期间被忽略
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
     * @brief 物理引擎 heartbeat
     *
     * 执行顺序：
     *   1. 急停延迟状态机
     *   2. 急停生效 → 所有轴强制掉电+停止
     *   3. 各轴状态跃迁 → 运动学推演 → 限位检测
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
        }
    }

    /**
     * @brief 读取指定轴的当前反馈
     */
    AxisFeedback getFeedback(AxisId id) const {
        return m_axes.at(id).feedback;
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

    /**
     * @brief 强制设置"设备急停"命令寄存器（用于测试注入命令来源）
     */
    void forceEmergencyStopCommand(bool active) {
        m_emergencyStopCmdPending = active;
        m_emergencyStopTimer = 0;
    }

    // ========== 仿真环境配置接口 ==========

    void forceState(AxisId id, AxisState s) {
        m_axes.at(id).feedback.state = s;
    }

    void setSimulatedMoveVelocity(AxisId id, double v) {
        m_axes.at(id).move_velocity = std::abs(v);
    }

    void setSimulatedJogVelocity(AxisId id, double v) {
        m_axes.at(id).jog_velocity = std::abs(v);
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
     * @brief 重置所有轴 + 急停状态到初始值
     */
    void resetAll() {
        for (auto& [id, axis] : m_axes) {
            axis = AxisStateInternal{};
        }
        m_emergencyStopCmdPending = false;
        m_emergencyStopTimer = 0;
        m_emergencyStoppedReg = false;
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

        // 急停生效：所有轴强制掉电 + 停止
        if (m_emergencyStoppedReg) {
            for (auto& [id, axis] : m_axes) {
                if (axis.feedback.state != AxisState::Disabled) {
                    axis.feedback.state = AxisState::Disabled;
                }
                axis.enable_timer_ms = 0;
                axis.stop_requested = false;
            }
        }
    }

    // ========== 命令分发 ==========

    void processCommand(AxisId id, std::monostate) {}

    void processCommand(AxisId id, const StopCommand&) {
        auto& axis = m_axes.at(id);
        handleStop(axis);
    }

    void processCommand(AxisId id, const EnableCommand& cmd) {
        // 急停激活时，忽略一切使能命令（模拟真实 PLC AND NOT 设备急停 逻辑）
        if (m_emergencyStoppedReg) {
            return;
        }
        auto& axis = m_axes.at(id);
        if (cmd.active && axis.feedback.state == AxisState::Disabled) {
            axis.enable_timer_ms = 0;
            axis.feedback.state = AxisState::Unknown;
        } else if (!cmd.active) {
            axis.feedback.state = AxisState::Disabled;
        }
    }

    void processCommand(AxisId id, const JogCommand& cmd) {
        auto& axis = m_axes.at(id);
        if (m_emergencyStoppedReg) return;
        if (cmd.active) {
            if (axis.feedback.state == AxisState::Idle) {
                axis.feedback.state = AxisState::Jogging;
                axis.jog_dir = cmd.dir;
            }
        } else {
            if (axis.feedback.state == AxisState::Jogging) {
                axis.stop_requested = true;
            }
        }
    }

    void processCommand(AxisId id, const MoveCommand& cmd) {
        auto& axis = m_axes.at(id);
        if (m_emergencyStoppedReg) return;
        if (axis.feedback.state == AxisState::Idle) {
            axis.feedback.state = (cmd.type == MoveType::Absolute) ?
                                   AxisState::MovingAbsolute : AxisState::MovingRelative;
            if (cmd.type == MoveType::Absolute) {
                axis.target_pos = cmd.target;
            } else {
                axis.target_pos = axis.feedback.absPos + cmd.target;
            }
        }
    }

    void processCommand(AxisId id, const ZeroAbsoluteCommand&) {
        auto& axis = m_axes.at(id);
        if (axis.feedback.state == AxisState::Idle) axis.feedback.absPos = 0.0;
    }

    void processCommand(AxisId id, const SetRelativeZeroCommand&) {
        auto& axis = m_axes.at(id);
        if (axis.feedback.state == AxisState::Idle) axis.feedback.relZeroAbsPos = axis.feedback.absPos;
    }

    void processCommand(AxisId id, const ClearRelativeZeroCommand&) {
        auto& axis = m_axes.at(id);
        if (axis.feedback.state == AxisState::Idle) axis.feedback.relZeroAbsPos = 0.0;
    }

    void processCommand(AxisId id, const SetJogVelocityCommand& cmd) {
        m_axes.at(id).jog_velocity = std::abs(cmd.velocity);
    }

    void processCommand(AxisId id, const SetMoveVelocityCommand& cmd) {
        m_axes.at(id).move_velocity = std::abs(cmd.velocity);
    }

    void processCommand(AxisId id, const GantryCouplingCommand& /*cmd*/) {
        (void)id;
    }

    void processCommand(AxisId id, const GantryPowerCommand& /*cmd*/) {
        (void)id;
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
