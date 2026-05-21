#include "Axis.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include "infrastructure/utils/CommandFormatter.h"
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

void Axis::setIdentity(AxisId id, const std::string& groupName)
{
    m_id = id;
    m_group = groupName;
}

void Axis::applyFeedback(const AxisFeedback &feedback)
{
    // 为日志系统创建 TraceScope，输出时自动携带 [group][axis] 上下文
    TraceScope scope(m_group, axisIdToString(m_id), "");

    // --- 基线 TRACE（节流: 每50次tick输出1条）---
    LOG_TRACE_EVERY_N(50, LogLayer::DOM, "Axis",
        "applyFeedback: state=" + std::string(axisStateName(feedback.state))
        + " abs=" + std::to_string(feedback.absPos)
        + " rel=" + std::to_string(feedback.relPos)
        + " base=" + std::to_string(feedback.relZeroAbsPos)
        + " posLimit=" + (feedback.posLimit ? "true" : "false")
        + " negLimit=" + (feedback.negLimit ? "true" : "false")
        + " pending=" + utils::format(m_pending_intent));

    // --- 状态镜像 + 速度镜像 ---
    AxisState prevState = m_state;
    m_state = feedback.state;
    m_current_abs_pos = feedback.absPos;
    m_current_rel_pos = feedback.relPos;
    m_rel_zero_abs_pos = feedback.relZeroAbsPos;

    m_pos_limit_active = feedback.posLimit;
    m_neg_limit_active = feedback.negLimit;
    m_pos_limit_value = feedback.posLimitValue;
    m_neg_limit_value = feedback.negLimitValue;

    m_jog_velocity = feedback.getjogVelocity;
    m_move_velocity = feedback.getMoveVelocity;

    // --- 状态变更 DEBUG ---
    if (prevState != m_state) {
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "applyFeedback: state " + std::string(axisStateName(prevState))
            + " -> " + std::string(axisStateName(m_state)));
    }

    // ═══════════════════════════════════════════════
    // 1. 限位运行中熔断逻辑
    // ═══════════════════════════════════════════════
    if (m_pos_limit_active || m_neg_limit_active) {
        if (std::holds_alternative<MoveCommand>(m_pending_intent) || 
            std::holds_alternative<JogCommand>(m_pending_intent)) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: LIMIT FUSE -- clearing motion intent: " + utils::format(m_pending_intent));
            m_pending_intent = std::monostate{};
        }
    }

    // ═══════════════════════════════════════════════
    // 2. 运动类状态：清理运动意图
    // ═══════════════════════════════════════════════
    if (m_state == AxisState::Jogging ||
        m_state == AxisState::MovingAbsolute ||
        m_state == AxisState::MovingRelative)
    {
        if (std::holds_alternative<JogCommand>(m_pending_intent)) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: axis=" + std::string(axisStateName(m_state))
                + " -> clearing Jog intent");
            m_pending_intent = std::monostate{};
        }
    }

    // ═══════════════════════════════════════════════
    // 3. 静止类状态：清理停止意图
    // ═══════════════════════════════════════════════
    if (m_state == AxisState::Idle ||
        m_state == AxisState::Disabled ||
        m_state == AxisState::Error)
    {
        if (std::holds_alternative<StopCommand>(m_pending_intent)) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: state=" + std::string(axisStateName(m_state))
                + " -> clearing Stop intent");
            m_pending_intent = std::monostate{};
        }
    }

    // ═══════════════════════════════════════════════
    // 4. ZeroAbsolute 闭环
    // ═══════════════════════════════════════════════
    if (std::holds_alternative<ZeroAbsoluteCommand>(m_pending_intent)) {
        if (std::abs(m_current_abs_pos) < POSITION_EPSILON) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: ZeroAbsolute CLOSED -- abs=" + std::to_string(m_current_abs_pos)
                + " < eps=" + std::to_string(POSITION_EPSILON));
            m_pending_intent = std::monostate{};
        }
    }

    // ═══════════════════════════════════════════════
    // 5. SetRelativeZero 闭环
    // ═══════════════════════════════════════════════
    if (std::holds_alternative<SetRelativeZeroCommand>(m_pending_intent)) {
        bool isRelPosZero = std::abs(m_current_rel_pos) < POSITION_EPSILON;
        bool isBaseMatched = std::abs(m_rel_zero_abs_pos - m_expected_zero_base) < POSITION_EPSILON;

        if (isRelPosZero && isBaseMatched) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: SetRelativeZero CLOSED -- rel=" + std::to_string(m_current_rel_pos)
                + " base=" + std::to_string(m_rel_zero_abs_pos)
                + " expected=" + std::to_string(m_expected_zero_base));
            m_pending_intent = std::monostate{};
        }
        // 仅状态变更时输出delta DEBUG
        else if (isRelPosZero != (std::abs(m_current_rel_pos - (m_rel_zero_abs_pos - m_expected_zero_base)) < POSITION_EPSILON * 2)) {
            LOG_TRACE_EVERY_N(20, LogLayer::DOM, "Axis",
                "applyFeedback: SetRelativeZero waiting -- rel=" + std::to_string(m_current_rel_pos)
                + " base=" + std::to_string(m_rel_zero_abs_pos)
                + " expected=" + std::to_string(m_expected_zero_base));
        }
    }

    // ═══════════════════════════════════════════════
    // 6. ClearRelativeZero 闭环
    // ═══════════════════════════════════════════════
    if (std::holds_alternative<ClearRelativeZeroCommand>(m_pending_intent)) {
        bool isRelPosRestored = std::abs(m_current_rel_pos - m_current_abs_pos) < POSITION_EPSILON;
        bool isBaseReset = std::abs(m_rel_zero_abs_pos) < POSITION_EPSILON;

        if (isRelPosRestored && isBaseReset) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: ClearRelativeZero CLOSED -- rel=" + std::to_string(m_current_rel_pos)
                + " abs=" + std::to_string(m_current_abs_pos)
                + " base=" + std::to_string(m_rel_zero_abs_pos));
            m_pending_intent = std::monostate{};
        }
    }

    // ═══════════════════════════════════════════════
    // 7. Move 定位指令数值收敛判定
    // ═══════════════════════════════════════════════
    if (auto* moveCmd = std::get_if<MoveCommand>(&m_pending_intent)) {
        if (m_state == AxisState::Idle) {
            double physicalTarget = 0.0;
            
            if (moveCmd->type == MoveType::Absolute) {
                physicalTarget = moveCmd->target;
            } else {
                physicalTarget = moveCmd->startAbs + moveCmd->target;
            }

            if (std::abs(m_current_abs_pos - physicalTarget) < POSITION_EPSILON) {
                LOG_DEBUG(LogLayer::DOM, "Axis",
                    "applyFeedback: Move CLOSED -- abs=" + std::to_string(m_current_abs_pos)
                    + " target=" + std::to_string(physicalTarget)
                    + " diff=" + std::to_string(std::abs(m_current_abs_pos - physicalTarget)));
                m_pending_intent = std::monostate{};
            }
        }
    }

    // ═══════════════════════════════════════════════
    // 8. Enable/Disable 闭环
    // ═══════════════════════════════════════════════
    if (auto* cmd = std::get_if<EnableCommand>(&m_pending_intent)) {
        if (cmd->active) {
            if (m_state != AxisState::Disabled && m_state != AxisState::Unknown) {
                LOG_DEBUG(LogLayer::DOM, "Axis",
                    "applyFeedback: Enable CLOSED -- state=" + std::string(axisStateName(m_state)));
                m_pending_intent = std::monostate{};
            }
        } else {
            if (m_state == AxisState::Disabled) {
                LOG_DEBUG(LogLayer::DOM, "Axis",
                    "applyFeedback: Disable CLOSED -- state=Disabled");
                m_pending_intent = std::monostate{};
            }
        }
    }

    // ═══════════════════════════════════════════════
    // 9. SetVelocity 闭环
    // ═══════════════════════════════════════════════
    if (auto* cmd = std::get_if<SetJogVelocityCommand>(&m_pending_intent)) {
        if (m_jog_velocity == cmd->velocity) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: SetJogVelocity CLOSED -- v=" + std::to_string(m_jog_velocity));
            m_pending_intent = std::monostate{};
        }
    }
    
    if (auto* cmd = std::get_if<SetMoveVelocityCommand>(&m_pending_intent)) {
        if (m_move_velocity == cmd->velocity) {
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "applyFeedback: SetMoveVelocity CLOSED -- v=" + std::to_string(m_move_velocity));
            m_pending_intent = std::monostate{};
        }
    }
}

bool Axis::enable(bool active)
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "enable(active=" + std::string(active ? "true" : "false") + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " pending=" + utils::format(m_pending_intent));

    // 约束 1：安全屏障 - 故障状态下严禁上电
    if (active && m_state == AxisState::Error) {
        m_last_rejection = RejectionReason::InvalidState;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "enable: REJECT reason=InvalidState, state=Error");
        return false;
    }

    // 约束 2：安全屏障 - 运动中严禁直接切断动力
    if (!active && (m_state == AxisState::Jogging || 
                    m_state == AxisState::MovingAbsolute || 
                    m_state == AxisState::MovingRelative)) {
        m_last_rejection = RejectionReason::AlreadyMoving;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "enable: REJECT reason=AlreadyMoving, state=" + std::string(axisStateName(m_state)));
        return false;
    }

    // 约束 3：幂等性处理 - 如果状态已经达标，不产生新指令
    if (active && (m_state != AxisState::Disabled && m_state != AxisState::Unknown)) {
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "enable: IDEMPOTENT -- already enabled, state=" + std::string(axisStateName(m_state)));
        return true; 
    }
    if (!active && m_state == AxisState::Disabled) {
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "enable: IDEMPOTENT -- already disabled");
        return true;
    }

    // 4. 生成意图
    m_pending_intent = EnableCommand{ active };
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "enable: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}

bool Axis::jog(Direction dir)
{
    std::string dirStr = (dir == Direction::Forward) ? "Forward" : "Backward";
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "jog(dir=" + dirStr + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " posLimit=" + (m_pos_limit_active ? "true" : "false")
        + " negLimit=" + (m_neg_limit_active ? "true" : "false")
        + " pending=" + utils::format(m_pending_intent));

    // 1. 状态准入细化检查
    if (m_state != AxisState::Idle) {
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "jog: REJECT reason=AlreadyMoving, state=" + std::string(axisStateName(m_state)));
        } 
        else {
            m_last_rejection = RejectionReason::InvalidState;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "jog: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state)));
        }
        return false;
    }

    // 2. 硬件限位 Bit 拦截
    if (dir == Direction::Forward && m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "jog: REJECT reason=AtPositiveLimit, abs=" + std::to_string(m_current_abs_pos));
        return false;
    }
    if (dir == Direction::Backward && m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "jog: REJECT reason=AtNegativeLimit, abs=" + std::to_string(m_current_abs_pos));
        return false;
    }

    // 3. 软件限位数值预检
    if (dir == Direction::Forward && m_current_abs_pos >= m_pos_limit_value) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "jog: REJECT reason=AtPositiveLimit (soft), abs=" + std::to_string(m_current_abs_pos)
            + " >= limit=" + std::to_string(m_pos_limit_value));
        return false;
    }
    if (dir == Direction::Backward && m_current_abs_pos <= m_neg_limit_value) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "jog: REJECT reason=AtNegativeLimit (soft), abs=" + std::to_string(m_current_abs_pos)
            + " <= limit=" + std::to_string(m_neg_limit_value));
        return false;
    }

    // 4. 准入通过：生成点动意图
    m_pending_intent = JogCommand{ dir, true };
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "jog: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}


bool Axis::stopJog(Direction dir) {
    std::string dirStr = (dir == Direction::Forward) ? "Forward" : "Backward";
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "stopJog(dir=" + dirStr + ") entry:"
        + " state=" + std::string(axisStateName(m_state)));

    // 停止点动是安全操作，无条件允许
    m_pending_intent = JogCommand{ dir, false };
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "stopJog: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}


bool Axis::moveAbsolute(double target)
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "moveAbsolute(target=" + std::to_string(target) + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " posLimit=" + (m_pos_limit_active ? "true" : "false")
        + " negLimit=" + (m_neg_limit_active ? "true" : "false")
        + " pending=" + utils::format(m_pending_intent));

    if (m_state != AxisState::Idle) {
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "moveAbsolute: REJECT reason=AlreadyMoving, state=" + std::string(axisStateName(m_state)));
        } 
        else {
            m_last_rejection = RejectionReason::InvalidState;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "moveAbsolute: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state)));
        }
        return false;
    }

    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveAbsolute: REJECT reason=AtPositiveLimit");
        return false;
    }

    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveAbsolute: REJECT reason=AtNegativeLimit");
        return false;
    }

    if (target > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveAbsolute: REJECT reason=TargetOutOfPositiveLimit, target=" + std::to_string(target)
            + " > limit=" + std::to_string(m_pos_limit_value));
        return false;
    }
    if (target < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveAbsolute: REJECT reason=TargetOutOfNegativeLimit, target=" + std::to_string(target)
            + " < limit=" + std::to_string(m_neg_limit_value));
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Absolute, target, m_current_abs_pos };
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "moveAbsolute: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}

bool Axis::moveRelative(double distance)
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "moveRelative(distance=" + std::to_string(distance) + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " posLimit=" + (m_pos_limit_active ? "true" : "false")
        + " negLimit=" + (m_neg_limit_active ? "true" : "false")
        + " pending=" + utils::format(m_pending_intent));

    if (m_state != AxisState::Idle) {
        if (m_state == AxisState::Jogging || 
            m_state == AxisState::MovingAbsolute || 
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "moveRelative: REJECT reason=AlreadyMoving, state=" + std::string(axisStateName(m_state)));
        } 
        else {
            m_last_rejection = RejectionReason::InvalidState;
            LOG_DEBUG(LogLayer::DOM, "Axis",
                "moveRelative: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state)));
        }
        return false;
    }

    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveRelative: REJECT reason=AtPositiveLimit");
        return false;
    }

    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveRelative: REJECT reason=AtNegativeLimit");
        return false;
    }

    double expectedTarget = m_current_abs_pos + distance;
    if (expectedTarget > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveRelative: REJECT reason=TargetOutOfPositiveLimit, expectedTarget=" + std::to_string(expectedTarget)
            + " > limit=" + std::to_string(m_pos_limit_value));
        return false;
    }
    if (expectedTarget < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "moveRelative: REJECT reason=TargetOutOfNegativeLimit, expectedTarget=" + std::to_string(expectedTarget)
            + " < limit=" + std::to_string(m_neg_limit_value));
        return false;
    }

    m_pending_intent = MoveCommand{ MoveType::Relative, distance, m_current_abs_pos };
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "moveRelative: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}

bool Axis::stop()
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        std::string("stop() entry:")
        + " state=" + std::string(axisStateName(m_state))
        + " pending=" + utils::format(m_pending_intent));

    m_pending_intent = StopCommand{};
    m_last_rejection = RejectionReason::None;

    LOG_DEBUG(LogLayer::DOM, "Axis",
        "stop: PASS -> pending=" + utils::format(m_pending_intent));
    return true;
}

bool Axis::zeroAbsolutePosition()
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        std::string("zeroAbsolutePosition() entry:")
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " pending=" + utils::format(m_pending_intent));

    if (m_state == AxisState::Idle || m_state == AxisState::Disabled) {
        m_pending_intent = ZeroAbsoluteCommand{};
        m_last_rejection = RejectionReason::None;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "zeroAbsolutePosition: PASS -> pending=" + utils::format(m_pending_intent));
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "zeroAbsolutePosition: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state))
        + " (requires Idle or Disabled)");
    return false;
}

bool Axis::setRelativeZero() {
    LOG_DEBUG(LogLayer::DOM, "Axis",
        std::string("setRelativeZero() entry:")
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " pending=" + utils::format(m_pending_intent));

    if (m_state != AxisState::Idle && m_state != AxisState::Disabled) {
        m_last_rejection = RejectionReason::InvalidState;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "setRelativeZero: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state))
            + " (requires Idle or Disabled)");
        return false;
    }

    m_expected_zero_base = m_current_abs_pos;

    m_pending_intent = SetRelativeZeroCommand{};
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "setRelativeZero: PASS -> pending=" + utils::format(m_pending_intent)
        + " expectedBase=" + std::to_string(m_expected_zero_base));
    return true;
}


bool Axis::clearRelativeZero() {
    LOG_DEBUG(LogLayer::DOM, "Axis",
        std::string("clearRelativeZero() entry:")
        + " state=" + std::string(axisStateName(m_state))
        + " abs=" + std::to_string(m_current_abs_pos)
        + " base=" + std::to_string(m_rel_zero_abs_pos)
        + " pending=" + utils::format(m_pending_intent));

    if (m_state != AxisState::Idle && m_state != AxisState::Disabled) {
        m_last_rejection = RejectionReason::InvalidState;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "clearRelativeZero: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state))
            + " (requires Idle or Disabled)");
        return false;
    }

    m_pending_intent = ClearRelativeZeroCommand{};
    m_last_rejection = RejectionReason::None;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "clearRelativeZero: PASS -> pending=" + utils::format(m_pending_intent));
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
    return std::holds_alternative<std::monostate>(m_pending_intent);
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
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "setJogVelocity(v=" + std::to_string(v) + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " pending=" + utils::format(m_pending_intent));

    if (v <= 0.0) {
        m_last_rejection = RejectionReason::InvalidArgument;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "setJogVelocity: REJECT reason=InvalidArgument, v=" + std::to_string(v));
        return false;
    }
    
    if (m_state == AxisState::Idle || m_state == AxisState::Disabled) {
        m_jog_velocity = v;
        m_pending_intent = SetJogVelocityCommand{ .velocity = v };
        m_last_rejection = RejectionReason::None;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "setJogVelocity: PASS -> pending=" + utils::format(m_pending_intent));
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "setJogVelocity: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state)));
    return false;
}

bool Axis::setMoveVelocity(double v)
{
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "setMoveVelocity(v=" + std::to_string(v) + ") entry:"
        + " state=" + std::string(axisStateName(m_state))
        + " pending=" + utils::format(m_pending_intent));

    if (v <= 0.0) {
        m_last_rejection = RejectionReason::InvalidArgument;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "setMoveVelocity: REJECT reason=InvalidArgument, v=" + std::to_string(v));
        return false;
    }

    if (m_state == AxisState::Idle || m_state == AxisState::Disabled) {
        m_move_velocity = v;
        m_pending_intent = SetMoveVelocityCommand{ .velocity = v };
        m_last_rejection = RejectionReason::None;
        LOG_DEBUG(LogLayer::DOM, "Axis",
            "setMoveVelocity: PASS -> pending=" + utils::format(m_pending_intent));
        return true;
    }
    m_last_rejection = RejectionReason::InvalidState;
    LOG_DEBUG(LogLayer::DOM, "Axis",
        "setMoveVelocity: REJECT reason=InvalidState, state=" + std::string(axisStateName(m_state)));
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
