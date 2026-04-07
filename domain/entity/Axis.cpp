#include "Axis.h"
#include <cmath>
Axis::Axis() : m_state(AxisState::Unknown)
{
}

AxisState Axis::state() const
{
    return m_state;
}

void Axis::applyFeedback(const AxisFeedback &feedback)
{
    m_state = feedback.state;
    m_current_abs_pos = feedback.absPos;
    m_current_rel_pos = feedback.relPos;
    m_rel_zero_abs_pos = feedback.relZeroAbsPos;

    // 1. 运动类状态：清理运动意图
    if (m_state == AxisState::Jogging ||
        m_state == AxisState::MovingAbsolute ||
        m_state == AxisState::MovingRelative)
    {
        // 只有当前存的是运动命令时才清理，防止误杀 Stop 指令
        if (std::holds_alternative<JogCommand>(m_pending_intent)) {
            m_pending_intent = std::monostate{};
        }
    }

    // 2. 静止类状态：清理停止意图 (含 Idle, Disabled, Error)
    if (m_state == AxisState::Idle ||
        m_state == AxisState::Disabled ||
        m_state == AxisState::Error)
    {
        // 如果当前挂起的是 Stop 命令，则视为已完成
        if (std::holds_alternative<StopCommand>(m_pending_intent)) {
            m_pending_intent = std::monostate{};
        }
    }


    // 3. 绝对位置清零：根据反馈的绝对位置与零点的接近程度自动清理 ZeroAbsoluteCommand 意图
    if (std::holds_alternative<ZeroAbsoluteCommand>(m_pending_intent)) {
        // 判定公式：|CurrentPos - 0.0| < EPSILON
        if (std::abs(m_current_abs_pos) < POSITION_EPSILON) {
            m_pending_intent = std::monostate{}; 
        }
    }

    // 4. 设置相对原点闭环
    if (std::holds_alternative<SetRelativeZeroCommand>(m_pending_intent)) {
        // 条件 A：相对坐标归零
        bool isRelPosZero = std::abs(m_current_rel_pos) < POSITION_EPSILON;
        // 条件 B：PLC 的基准寄存器对齐了我们当初“抓拍”的期望值
        bool isBaseMatched = std::abs(m_rel_zero_abs_pos - m_expected_zero_base) < POSITION_EPSILON;

        if (isRelPosZero && isBaseMatched) {
            m_pending_intent = std::monostate{}; // 双重达标，消费意图
        }
    }


    // 5. 清除相对原点闭环
    if (std::holds_alternative<ClearRelativeZeroCommand>(m_pending_intent)) {
        // 条件 A：相对坐标恢复为绝对坐标
        bool isRelPosRestored = std::abs(m_current_rel_pos - m_current_abs_pos) < POSITION_EPSILON;
        // 条件 B：PLC 的基准寄存器已清零（通常清除后 PLC 会将基准设为 0）
        bool isBaseReset = std::abs(m_rel_zero_abs_pos) < POSITION_EPSILON;

        if (isRelPosRestored && isBaseReset) {
            m_pending_intent = std::monostate{};
        }
    }

    // --- ⭐ 新增 6. Move 定位指令的数值收敛判定 ---
    if (auto* moveCmd = std::get_if<MoveCommand>(&m_pending_intent)) {
        // 约束：必须同时满足 状态为 Idle 且 数值进入容差
        if (m_state == AxisState::Idle) {
            double physicalTarget = 0.0;
            
            if (moveCmd->type == MoveType::Absolute) {
                physicalTarget = moveCmd->target; // 绝对目标
            } else {
                physicalTarget = moveCmd->startAbs + moveCmd->target; // 相对计算
            }

            // 物理闭环判定：|当前绝对位置 - 物理终点| < ε
            if (std::abs(m_current_abs_pos - physicalTarget) < POSITION_EPSILON) {
                m_pending_intent = std::monostate{};
            }
        }
    }

}

bool Axis::jog(Direction dir)
{
    if (m_state != AxisState::Idle) {
        return false;
    }

    m_pending_intent = JogCommand{ dir };
    return true;
}

bool Axis::moveAbsolute(double target)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Absolute, target, m_current_abs_pos };
    return true;
}

bool Axis::moveRelative(double distance)
{
    if (m_state != AxisState::Idle)  {
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Relative, distance, m_current_abs_pos };
    return true;
}
bool Axis::stop()
{
    m_pending_intent = StopCommand{};

    return true;
}

bool Axis::zeroAbsolutePosition()
{
    // 只有在静止态（Idle/Disabled）才允许修改基准
    if (m_state == AxisState::Idle ||
        m_state == AxisState::Disabled)
    {
        m_pending_intent = ZeroAbsoluteCommand{};
        return true;
    }
    return false;
}

bool Axis::setRelativeZero() {
    // 必须为 Idle 状态
    if (m_state != AxisState::Idle) {
        return false;
    }

    // 核心逻辑：发起指令时，记录当前的绝对位置作为“期望基准”
    m_expected_zero_base = m_current_abs_pos;

    m_pending_intent = SetRelativeZeroCommand{};
    return true;
}


bool Axis::clearRelativeZero() {
    // 必须为 Idle 状态
    if (m_state != AxisState::Idle) {
        return false;
    }
    m_pending_intent = ClearRelativeZeroCommand{};
    return true;
}


double Axis::currentAbsolutePosition() const
{
  return m_current_abs_pos;
}

double Axis::currentRelativePosition() const
{
    return m_current_rel_pos;
}

double Axis::relativeZeroAbsolutePosition() const
{
    return m_rel_zero_abs_pos;
}

bool Axis::hasPendingCommand() const
{
    // 语义：只要不是 monostate，就代表有运动类命令挂起
    return !std::holds_alternative<std::monostate>(m_pending_intent);
}


const AxisCommand &Axis::getPendingCommand() const
{
  return m_pending_intent;
}

bool Axis::hasPendingStop() const
{
    return std::holds_alternative<StopCommand>(m_pending_intent);
}
