#include "Axis.h"
#include <cmath>
#include <limits>
Axis::Axis(): m_state(AxisState::Unknown),
    m_pos_limit_value(std::numeric_limits<double>::max()),  // 默认正限位无穷大
    m_neg_limit_value(std::numeric_limits<double>::lowest()), // 默认负限位无穷小
    m_last_rejection(RejectionReason::None) // 初始化拒绝原因
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

    m_pos_limit_active = feedback.posLimit;   // 更新限位 Bit
    m_neg_limit_active = feedback.negLimit;
    m_pos_limit_value = feedback.posLimitValue; // 更新限位数值
    m_neg_limit_value = feedback.negLimitValue;

    m_jog_velocity = feedback.getjogVelocity;
    m_move_velocity = feedback.getMoveVelocity;

    // --- 限位运行中熔断逻辑 ---
    // 只要有任何限位触发，且当前有 Move 或 Jog 意图，立即清理
    // 这里采用最严苛策略：一旦限位触发，当前所有运动意图立即失效
    if (m_pos_limit_active || m_neg_limit_active) {
        if (std::holds_alternative<MoveCommand>(m_pending_intent) || 
            std::holds_alternative<JogCommand>(m_pending_intent)) {
            m_pending_intent = std::monostate{};
        }
    }

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


    // --- 7. 使能/掉电指令的生命周期闭环 ---
    if (auto* cmd = std::get_if<EnableCommand>(&m_pending_intent)) {
        if (cmd->active) {
            // 意图是上电：只要状态脱离了 Disabled 和 Unknown，视为上电成功
            if (m_state != AxisState::Disabled && m_state != AxisState::Unknown) {
                m_pending_intent = std::monostate{};
            }
        } else {
            // 意图是掉电：只有状态确认为 Disabled 时才清理意图
            if (m_state == AxisState::Disabled) {
                m_pending_intent = std::monostate{};
            }
        }
    }

    // --- 8. SetVelocity 闭环 ---
    if (auto* cmd = std::get_if<SetJogVelocityCommand>(&m_pending_intent)) {
        // 只要 PLC 返回的速度与命令一致, 就认为完成
        if (m_jog_velocity ==  cmd->velocity) {
            m_pending_intent = std::monostate{};
        }
    }
    
    if (auto* cmd = std::get_if<SetMoveVelocityCommand>(&m_pending_intent)) {
        if (m_move_velocity == cmd->velocity) {
            m_pending_intent = std::monostate{};
        }
    }

}

bool Axis::enable(bool active)
{
    // 约束 1：安全屏障 - 故障状态下严禁上电
    if (active && m_state == AxisState::Error) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    // 约束 2：安全屏障 - 运动中严禁直接切断动力
    if (!active && (m_state == AxisState::Jogging || 
                    m_state == AxisState::MovingAbsolute || 
                    m_state == AxisState::MovingRelative)) {
        m_last_rejection = RejectionReason::AlreadyMoving;
        return false;
    }

    // 约束 3：幂等性处理 - 如果状态已经达标，不产生新指令
    if (active && (m_state != AxisState::Disabled && m_state != AxisState::Unknown)) {
        return true; 
    }
    if (!active && m_state == AxisState::Disabled) {
        return true;
    }

    // 4. 生成意图
    m_pending_intent = EnableCommand{ active };
    m_last_rejection = RejectionReason::None;
    return true;
}

bool Axis::jog(Direction dir)
{
    // 1. 状态准入细化检查
    if (m_state != AxisState::Idle) {
        // 如果轴处于任何运动状态，报错“正忙”
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
        } 
        // 否则（Disabled, Error, Unknown），视为状态非法
        else {
            m_last_rejection = RejectionReason::InvalidState;
        }
        return false;
    }

    // 2. 硬件限位 Bit 拦截
    if (dir == Direction::Forward && m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }
    if (dir == Direction::Backward && m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // 3. 软件限位数值预检
    // 即使 Bit 没触发，如果坐标已超限，也禁止继续向超限方向点动
    if (dir == Direction::Forward && m_current_abs_pos >= m_pos_limit_value) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }
    if (dir == Direction::Backward && m_current_abs_pos <= m_neg_limit_value) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // 4. 准入通过：生成点动意图
    m_pending_intent = JogCommand{ dir, true };
    m_last_jog_dir = dir; // 记录方向，以便 stopJog() 寻址
    m_last_rejection = RejectionReason::None;
    return true;
}


bool Axis::stopJog() {
    // 停止点动是安全操作，无条件允许
    m_pending_intent = JogCommand{ m_last_jog_dir, false }; // 停止
    m_last_rejection = RejectionReason::None;
    return true;
}


bool Axis::moveAbsolute(double target)
{
    if (m_state != AxisState::Idle) {
        // 如果轴处于任何运动状态，报错“正忙”
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
        } 
        // 否则（Disabled, Error, Unknown），视为状态非法
        else {
            m_last_rejection = RejectionReason::InvalidState;
        }
        return false;
    }

    // 约束 1：如果已经在限位状态位触发中，禁止所有定位指令
    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }

    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // 约束 2：数值边界预检
    if (target > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (target < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Absolute, target, m_current_abs_pos };
    m_last_rejection = RejectionReason::None;
    return true;
}

bool Axis::moveRelative(double distance)
{
    if (m_state != AxisState::Idle) {
        // 如果轴处于任何运动状态，报错“正忙”
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
        } 
        // 否则（Disabled, Error, Unknown），视为状态非法
        else {
            m_last_rejection = RejectionReason::InvalidState;
        }
        return false;
    }

    // 约束 1：如果已经在限位状态位触发中，禁止所有定位指令
    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }

    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // 约束 2：计算预期终点并进行数值预检
    double expectedTarget = m_current_abs_pos + distance;
    if (expectedTarget > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (expectedTarget < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Relative, distance, m_current_abs_pos };
    m_last_rejection = RejectionReason::None;
    return true;
}
bool Axis::stop()
{
    m_pending_intent = StopCommand{};
    m_last_rejection = RejectionReason::None;

    return true;
}

bool Axis::zeroAbsolutePosition()
{
    // 只有在静止态 Idle 才允许修改基准
    if (m_state == AxisState::Idle)
    {
        m_pending_intent = ZeroAbsoluteCommand{};
        m_last_rejection = RejectionReason::None;
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    return false;
}

bool Axis::setRelativeZero() {
    // 必须为 Idle 状态
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    // 核心逻辑：发起指令时，记录当前的绝对位置作为“期望基准”
    m_expected_zero_base = m_current_abs_pos;

    m_pending_intent = SetRelativeZeroCommand{};
    m_last_rejection = RejectionReason::None;
    return true;
}


bool Axis::clearRelativeZero() {
    // 必须为 Idle 状态
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }
    m_pending_intent = ClearRelativeZeroCommand{};
    m_last_rejection = RejectionReason::None;
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

bool Axis::isMoveInProgress() const
{
    return std::holds_alternative<MoveCommand>(m_pending_intent);
}

bool Axis::isMoveCompleted() const
{
    return !isMoveInProgress();
}

double Axis::positiveSoftLimit() const
{
  return m_pos_limit_value;
}

double Axis::negativeSoftLimit() const
{
  return m_neg_limit_value;
}

bool Axis::setJogVelocity(double v)
{
    if (v <= 0.0) {
        m_last_rejection = RejectionReason::InvalidArgument;
        return false;
    }
    
    if (m_state == AxisState::Idle || m_state == AxisState::Disabled) {
        m_jog_velocity = v;
        m_pending_intent = SetJogVelocityCommand{ .velocity = v };
        m_last_rejection = RejectionReason::None;
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    return false;
}

bool Axis::setMoveVelocity(double v)
{
    if (v <= 0.0) {
        m_last_rejection = RejectionReason::InvalidArgument;
        return false;
    }

    if (m_state == AxisState::Idle || m_state == AxisState::Disabled) {
        m_move_velocity = v;
        m_pending_intent = SetMoveVelocityCommand{ .velocity = v };
        m_last_rejection = RejectionReason::None;
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    return false;
}

double Axis::getjogVelocity() const
{
    return m_jog_velocity;
}

double Axis::getMoveVelocity() const
{
    return m_move_velocity;
}

bool Axis::hasPendingCommand() const
{
    // 语义：只要不是 monostate，就代表有运动类命令挂起
    return !std::holds_alternative<std::monostate>(m_pending_intent);
}

RejectionReason Axis::lastRejection() const
{
    return m_last_rejection;
}

const AxisCommand &Axis::getPendingCommand() const
{
  return m_pending_intent;
}

bool Axis::hasPendingStop() const
{
    return std::holds_alternative<StopCommand>(m_pending_intent);
}
