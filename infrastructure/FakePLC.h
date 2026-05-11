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

            //模拟 PLC 逻辑：处理《轴X使能》
            // 如果使能请求为真，且轴当前是 Disabled 状态，则切入 Idle
             // 如果使能请求为假，则强制切入 Disabled
            updateEnableLogic();

            // 模拟 PLC 逻辑：处理《轴X联动状态》
            updateCouplingStatus();

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

    // 双轴龙门特例：设置耦合状态（仅影响 X1/X2 的反馈寄存器）
    // ── 模拟 PLC 内部寄存器 ──
    bool setXEnableRequest(bool on) { m_xEnableRequest = on; return true; }
    bool setXCouplingRequest(bool on) { m_xCouplingRequest = on; return true; }
    
    // 获取反馈寄存器
    bool getXCouplingStatus() const { return m_xCouplingStatus; }

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

    bool m_xEnableRequest = false;      // 对应控制寄存器《轴X使能》
    bool m_xCouplingRequest = false;    // 对应控制寄存器《轴X联动使能》
    bool m_xCouplingStatus = false;     // 对应状态寄存器《轴X联动状态》

    void updateEnableLogic() {
        auto& x1 = m_axes[AxisId::X1];
        auto& x2 = m_axes[AxisId::X2];

        if (m_xEnableRequest) {
            if (x1.feedback.state == AxisState::Disabled) x1.feedback.state = AxisState::Idle;
            if (x2.feedback.state == AxisState::Disabled) x2.feedback.state = AxisState::Idle;
        } else {
            x1.feedback.state = AxisState::Disabled;
            x2.feedback.state = AxisState::Disabled;
        }
    }

    void updateCouplingStatus() {
        auto& x1 = m_axes[AxisId::X1];
        auto& x2 = m_axes[AxisId::X2];

        // 条件 A: 收到联动指令
        bool condRequest = m_xCouplingRequest;
        
        // 条件 B: 速度为 0 (简单起见，判断 state 是否为 Idle/Error)
        bool condSpeedZero = (x1.feedback.state == AxisState::Idle || x1.feedback.state == AxisState::Error) &&
                             (x2.feedback.state == AxisState::Idle || x2.feedback.state == AxisState::Error);
        
        // 条件 C: 伺服已使能
        bool condEnabled = (x1.feedback.state != AxisState::Disabled) && 
                           (x2.feedback.state != AxisState::Disabled);

        // 条件 D: 位置偏差检查 (ABS(X1 + X2) < 0.1)
        bool condPosMatch = std::abs(x1.feedback.absPos + x2.feedback.absPos) < 0.1;

        // 开启逻辑
        if (condRequest && condSpeedZero && condEnabled && condPosMatch) {
            m_xCouplingStatus = true;
        } 
        
        // 关闭逻辑：指令撤销即关闭
        if (!m_xCouplingRequest) {
            m_xCouplingStatus = false;
        }
    }
};
};

#endif // FAKE_PLC_H
