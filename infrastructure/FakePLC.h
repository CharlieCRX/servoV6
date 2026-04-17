#ifndef FAKE_PLC_H
#define FAKE_PLC_H

#include "../domain/entity/Axis.h"
#include <cmath>
#include <algorithm>

class FakePLC {
public:
    FakePLC() = default;

    // --- 1. 核心对外接口 ---
    void onCommand(const AxisCommand& cmd) {

        std::visit([this](auto&& arg) {
            this->processCommand(arg);
        }, cmd);
    }

    // 由外部时钟驱动的物理引擎心跳
    void tick(int ms) {
        // 0. 处理上一周期的刹车请求
        if (m_stop_requested) {
            m_feedback.state = AxisState::Idle;
            m_stop_requested = false;
        }

        // 1. 处理异步状态迁移 (如上电延迟)
        updateStateTransitions(ms);

        // 2. 处理物理坐标的连续更新
        updateKinematics(ms);

        // 3. 处理硬件限位强拦截
        checkHardwareLimits();

        // 4. 更新动态相对坐标
        m_feedback.relPos = m_feedback.absPos - m_feedback.relZeroAbsPos;
    }

    AxisFeedback getFeedback() const { return m_feedback; }

    // --- 2. 仿真环境配置接口 ---
    void forceState(AxisState s) { m_feedback.state = s; }
    
    // 区分 Move 和 Jog 的独立仿真速度
    void setSimulatedMoveVelocity(double v) { m_move_velocity = std::abs(v); }
    void setSimulatedJogVelocity(double v) { m_jog_velocity = std::abs(v); }
    
    void setLimits(double pos, double neg) {
        m_feedback.posLimitValue = pos;
        m_feedback.negLimitValue = neg;
    }

private:
    AxisFeedback m_feedback{ AxisState::Disabled, 0.0, 0.0, 0.0, false, false, 1000.0, -1000.0 };
    
    // 独立速度配置
    double m_move_velocity = 50.0; // 默认定位速度 50 unit/s
    double m_jog_velocity = 10.0;  // 默认点动速度 10 unit/s
    
    // 内部定时器
    int m_enable_timer_ms = 0;
    static constexpr int ENABLE_DELAY_MS = 150; 

    // 运动学内部状态
    double m_target_pos = 0.0;
    Direction m_jog_dir = Direction::Forward;
    bool m_stop_requested = false;

    // --- 3. 命令分发处理 ---
    void processCommand(std::monostate) {} // 空指令不处理

    void processCommand(const StopCommand&) {
        handleStop(); // 核心：将 Stop 指令路由到停止逻辑
    }

    void processCommand(const EnableCommand& cmd) {
        if (cmd.active && m_feedback.state == AxisState::Disabled) {
            // 进入“启动中”过渡态
            m_enable_timer_ms = 0;
            m_feedback.state = AxisState::Unknown; 
        } else if (!cmd.active) {
            // 掉电立即生效
            m_feedback.state = AxisState::Disabled;
        }
    }

    void processCommand(const JogCommand& cmd) {
        if (cmd.active) {
            if (m_feedback.state == AxisState::Idle) {
                m_feedback.state = AxisState::Jogging;
                m_jog_dir = cmd.dir;
            }
        } else {
            if (m_feedback.state == AxisState::Jogging) {
                m_stop_requested = true; // 触发制动
            }
        }
    }

    void processCommand(const MoveCommand& cmd) {
        if (m_feedback.state == AxisState::Idle) {
            m_feedback.state = (cmd.type == MoveType::Absolute) ? 
                               AxisState::MovingAbsolute : AxisState::MovingRelative;
            
            if (cmd.type == MoveType::Absolute) {
                m_target_pos = cmd.target;
            } else {
                m_target_pos = m_feedback.absPos + cmd.target;
            }
        }
    }

    void processCommand(const ZeroAbsoluteCommand&) {
        if (m_feedback.state == AxisState::Idle) m_feedback.absPos = 0.0;
    }

    void processCommand(const SetRelativeZeroCommand&) {
        if (m_feedback.state == AxisState::Idle) m_feedback.relZeroAbsPos = m_feedback.absPos;
    }

    void processCommand(const ClearRelativeZeroCommand&) {
        if (m_feedback.state == AxisState::Idle) m_feedback.relZeroAbsPos = 0.0;
    }

    void processCommand(const SetJogVelocityCommand& cmd) {
        m_jog_velocity = std::abs(cmd.velocity); // 速度必须为正
    }

    void processCommand(const SetMoveVelocityCommand& cmd) {
        m_move_velocity = std::abs(cmd.velocity); // 速度必须为正
    }

    void handleStop() {
        // Stop 只对运动态生效
        if (m_feedback.state == AxisState::Jogging || 
            m_feedback.state == AxisState::MovingAbsolute || 
            m_feedback.state == AxisState::MovingRelative) {
            m_stop_requested = true; 
        }
    }

    // --- 4. 物理引擎演算 ---
    void updateStateTransitions(int ms) {
        if (m_feedback.state == AxisState::Unknown) {
            m_enable_timer_ms += ms;
            if (m_enable_timer_ms >= ENABLE_DELAY_MS) {
                m_feedback.state = AxisState::Idle; 
            }
        }
    }

    void updateKinematics(int ms) {
        if (m_feedback.state == AxisState::MovingAbsolute || 
            m_feedback.state == AxisState::MovingRelative) {
            
            // Move: 使用 m_move_velocity 进行 P 控制收敛
            double step = (m_move_velocity * ms) / 1000.0;
            double diff = m_target_pos - m_feedback.absPos;

            if (std::abs(diff) <= step) {
                m_feedback.absPos = m_target_pos;
                m_feedback.state = AxisState::Idle; // 闭环收敛
            } else {
                m_feedback.absPos += (diff > 0) ? step : -step;
            }
        } 
        else if (m_feedback.state == AxisState::Jogging) {
            
            // Jog: 使用 m_jog_velocity 进行连续累加
            double step = (m_jog_velocity * ms) / 1000.0;
            if (m_jog_dir == Direction::Forward) {
                m_feedback.absPos += step;
            } else {
                m_feedback.absPos -= step;
            }
        }
    }

    void checkHardwareLimits() {
        // 正软限位拦截
        if (m_feedback.absPos >= m_feedback.posLimitValue) {
            m_feedback.posLimit = true;
            m_feedback.absPos = m_feedback.posLimitValue; // 截断坐标
            forceStopIfMoving();
        } else {
            m_feedback.posLimit = false;
        }

        // 负软限位拦截
        if (m_feedback.absPos <= m_feedback.negLimitValue) {
            m_feedback.negLimit = true;
            m_feedback.absPos = m_feedback.negLimitValue; // 截断坐标
            forceStopIfMoving();
        } else {
            m_feedback.negLimit = false;
        }
    }

    void forceStopIfMoving() {
        if (m_feedback.state == AxisState::MovingAbsolute || 
            m_feedback.state == AxisState::MovingRelative || 
            m_feedback.state == AxisState::Jogging) {
            m_feedback.state = AxisState::Idle;
        }
    }
};

#endif // FAKE_PLC_H