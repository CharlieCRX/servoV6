#ifndef FAKE_PLC_H
#define FAKE_PLC_H

#include "../domain/entity/Axis.h"
#include "../domain/entity/AxisId.h"
#include "infrastructure/logger/Logger.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>

class FakePLC {
public:
    FakePLC() {
        // 初始化所有 AxisId 对应的寄存器组
        m_axes[AxisId::Y] = AxisStateInternal{};
        m_axes[AxisId::Z] = AxisStateInternal{};
        m_axes[AxisId::R] = AxisStateInternal{};
        m_axes[AxisId::X] = AxisStateInternal{};
        m_axes[AxisId::X1] = AxisStateInternal{};
        m_axes[AxisId::X2] = AxisStateInternal{};
    }

    // --- 1. 核心对外接口（多轴签名）---

    void onCommand(AxisId id, const AxisCommand& cmd) {
        std::visit([this, id](auto&& arg) {
            this->processCommand(id, arg);
        }, cmd);
    }

    // 由外部时钟驱动的物理引擎心跳（遍历所有轴独立更新）
    void tick(int ms) {

        m_tickCount++;
        // 🌟 2. 判断是否到了该打印的帧（比如每 100 个 tick 打印一次，即每秒打印一次）
        bool shouldLog = (m_tickCount % 100 == 0);

        for (auto& [id, axis] : m_axes) {

            if (axis.stop_requested) {
                axis.feedback.state = AxisState::Idle;
                axis.stop_requested = false;
            }

            updateStateTransitions(axis, ms);
            updateKinematics(axis, ms);
            checkHardwareLimits(axis);

            axis.feedback.relPos = axis.feedback.absPos - axis.feedback.relZeroAbsPos;

            // 🌟 核心规范：高频物理心跳采样，每 50 帧（模拟 500ms）打印一次
            if (shouldLog) {
                LOG_TRACE(LogLayer::HAL, "PLC", 
                    "Tick axis=" + axisIdToString(id) + 
                    " pos=" + std::to_string(axis.feedback.absPos));
            }
        }
    }

    AxisFeedback getFeedback(AxisId id) const {
        return m_axes.at(id).feedback;
    }

    // --- 2. 仿真环境配置接口（多轴签名）---

    void forceState(AxisId id, AxisState s) {
        m_axes.at(id).feedback.state = s;
    }

    void setSimulatedMoveVelocity(AxisId id, double v) {
        m_axes.at(id).move_velocity = std::abs(v);
    }

    void setSimulatedJogVelocity(AxisId id, double v) {
        m_axes.at(id).jog_velocity = std::abs(v);
    }

    void setPosition(AxisId id, double pos) {
        m_axes.at(id).feedback.absPos = pos;
    }

    void setLimits(AxisId id, double pos, double neg) {
        auto& axis = m_axes.at(id);
        axis.feedback.posLimitValue = pos;
        axis.feedback.negLimitValue = neg;
    }

private:
    // 每个轴独立的内部状态
    struct AxisStateInternal {
        AxisFeedback feedback{ AxisState::Disabled, 0.0, 0.0, 0.0, false, false, 1000.0, -1000.0 };

        // 独立速度配置
        double move_velocity = 50.0; // 默认定位速度 50 unit/s
        double jog_velocity = 10.0;  // 默认点动速度 10 unit/s

        // 内部定时器
        int enable_timer_ms = 0;

        // 运动学内部状态
        double target_pos = 0.0;
        Direction jog_dir = Direction::Forward;
        bool stop_requested = false;
    };

    uint64_t m_tickCount = 0; // 🌟 1. 增加一个 PLC 自己专属的滴答计数器

    std::unordered_map<AxisId, AxisStateInternal> m_axes;

    static constexpr int ENABLE_DELAY_MS = 150;

    // --- 3. 命令分发处理（多轴版本）---

    void processCommand(AxisId id, std::monostate) {} // 空指令不处理

    void processCommand(AxisId id, const StopCommand&) {
        auto& axis = m_axes.at(id);
        handleStop(axis);
    }

    void processCommand(AxisId id, const EnableCommand& cmd) {
        auto& axis = m_axes.at(id);
        if (cmd.active && axis.feedback.state == AxisState::Disabled) {
            // 进入"启动中"过渡态
            axis.enable_timer_ms = 0;
            axis.feedback.state = AxisState::Unknown;
        } else if (!cmd.active) {
            // 掉电立即生效
            axis.feedback.state = AxisState::Disabled;
        }
    }

    void processCommand(AxisId id, const JogCommand& cmd) {
        auto& axis = m_axes.at(id);
        if (cmd.active) {
            if (axis.feedback.state == AxisState::Idle) {
                axis.feedback.state = AxisState::Jogging;
                axis.jog_dir = cmd.dir;
            }
        } else {
            if (axis.feedback.state == AxisState::Jogging) {
                axis.stop_requested = true; // 触发制动
            }
        }
    }

    void processCommand(AxisId id, const MoveCommand& cmd) {
        auto& axis = m_axes.at(id);
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

    void handleStop(AxisStateInternal& axis) {
        // Stop 只对运动态生效
        if (axis.feedback.state == AxisState::Jogging ||
            axis.feedback.state == AxisState::MovingAbsolute ||
            axis.feedback.state == AxisState::MovingRelative) {
            axis.stop_requested = true;
        }
    }

    // --- 4. 物理引擎演算（操作指定轴）---

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

            // Move: 使用 move_velocity 进行 P 控制收敛
            double step = (axis.move_velocity * ms) / 1000.0;
            double diff = axis.target_pos - axis.feedback.absPos;

            if (std::abs(diff) <= step) {
                axis.feedback.absPos = axis.target_pos;
                axis.feedback.state = AxisState::Idle; // 闭环收敛
            } else {
                axis.feedback.absPos += (diff > 0) ? step : -step;
            }
        }
        else if (axis.feedback.state == AxisState::Jogging) {

            // Jog: 使用 jog_velocity 进行连续累加
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
