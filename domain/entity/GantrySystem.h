#pragma once

#include "PhysicalAxis.h"
#include "LogicalAxis.h"
#include "../value/GantryMode.h"
#include "../value/CouplingCondition.h"
#include "../value/PositionConsistency.h"
#include "../value/SafetyCheckResult.h"
#include "../event/GantryEvents.h"
#include <vector>
#include <string>

/**
 * @file GantrySystem.h
 * @brief 龙门系统聚合根
 *
 * 职责：
 *   - 管理 GantryMode (Coupled/Decoupled) 状态机
 *   - 持有 2 个 PhysicalAxis (X1, X2) 和 1 个 LogicalAxis (X)
 *   - 执行联动建立条件校验 (约束 13) 和联动维持校验 (约束 14)
 *   - 在每次操作时执行操作目标互斥 (约束 18)
 *   - 在每次操作时执行运动互斥 (约束 19): 检查命令槽状态
 *   - 每周期执行状态聚合 (约束 20): 将 X1/X2 状态合并为 X 的聚合状态
 *   - 发布领域事件 (GantryEvents) 通知外部模式变更、同步偏差等
 *
 * 模式状态机：
 *   Decoupled ──(requestCoupling, checkAll 通过)──▶ Coupled
 *   Coupled   ──(requestDecoupling)──▶ Decoupled
 *   Coupled   ──(checkPositionOnly 失败)──▶ DeviationFault → Decoupled
 */

/// 操作可操作性检查结果
enum class Operability {
    Allowed,
    Rejected_Mode,    ///< 当前模式下不可操作该目标
    Rejected_Alarm,   ///< 报警状态
    Rejected_Limit,   ///< 限位状态 + 方向禁止
    Rejected_Busy     ///< 命令槽已被占用
};

/// 命令下发结果
struct CommandResult {
    bool accepted = false;
    std::string rejectReason;
    GantryEvents::Event event;
};

class GantrySystem {
public:
    /**
     * @brief 构造龙门系统
     * @param x1 物理轴 X1
     * @param x2 物理轴 X2
     */
    GantrySystem(PhysicalAxis x1, PhysicalAxis x2)
        : m_x1(x1), m_x2(x2), m_mode(GantryMode::Decoupled)
    {}

    // ═══════════════════════════════════
    // 模式管理
    // ═══════════════════════════════════

    GantryMode mode() const { return m_mode; }

    /**
     * @brief 联动建立申请 (约束 12-13)
     *
     * 检查所有联动条件：
     *   1. X1/X2 均已使能
     *   2. 无任何报警
     *   3. 无任何限位触发
     *   4. 位置一致性 (|X1.pos + X2.pos| ≤ epsilon)
     *
     * @return 联动条件检查结果
     */
    CouplingCondition::Result requestCoupling() {
        // 发布 CouplingRequested 事件
        m_events.push_back(GantryEvents::Event::couplingRequested());

        auto result = CouplingCondition::checkAll(
            m_x1.isEnabled(), m_x2.isEnabled(),
            m_x1.isAlarmed() || m_x2.isAlarmed(),
            m_x1.isAnyLimitActive() || m_x2.isAnyLimitActive(),
            m_x1.position(), m_x2.position()
        );

        if (result.allowed) {
            m_mode = GantryMode::Coupled;
            m_events.push_back(GantryEvents::Event::coupled());
        }

        return result;
    }

    /**
     * @brief 分动申请 (约束 4)
     *
     * 主动退出联动模式。Stop 由调用者在退出前处理。
     */
    void requestDecoupling(const std::string& reason = "") {
        if (m_mode == GantryMode::Coupled) {
            m_mode = GantryMode::Decoupled;
            m_events.push_back(GantryEvents::Event::decoupled(reason));
        }
    }

    // ═══════════════════════════════════
    // 操作目标互斥检查 (约束 18)
    // ═══════════════════════════════════

    /**
     * @brief 检查指定目标在当前模式下是否可操作
     *
     * Coupled 模式: 只允许 AxisId::X
     * Decoupled 模式: 只允许 AxisId::X1 或 AxisId::X2
     *
     * @param target 操作目标轴标识
     * @return true = 当前模式下该目标可操作
     */
    bool isTargetOperable(AxisId target) const {
        if (isCoupled(m_mode)) {
            return target == AxisId::X;
        } else {
            return target == AxisId::X1 || target == AxisId::X2;
        }
    }

    /**
     * @brief 综合可操作性检查
     *
     * 检查顺序：
     *   1. 模式检查 (约束 18)
     *   2. 报警检查 (约束 17)
     *   3. 限位检查 (约束 15-16)
     *   4. 命令槽检查 (约束 19)
     *
     * @param target 操作目标
     * @param direction 运动方向 (限位检查需要)
     * @return 可操作性枚举
     */
    Operability checkOperability(AxisId target,
                                  MotionDirection direction = MotionDirection::Forward) const {
        // Step 1: 模式检查
        if (!isTargetOperable(target)) {
            return Operability::Rejected_Mode;
        }

        // Step 2: 报警检查
        if (m_x1.isAlarmed() || m_x2.isAlarmed()) {
            return Operability::Rejected_Alarm;
        }

        // Step 3: 限位检查 (仅对物理轴有效)
        if (target == AxisId::X1 || target == AxisId::X2) {
            const PhysicalAxis& axis = (target == AxisId::X1) ? m_x1 : m_x2;
            auto safety = checkMotionSafety(
                axis.isAlarmed(),
                axis.isPosLimitActive(),
                axis.isNegLimitActive(),
                direction
            );
            if (!safety.isAllowed()) {
                return Operability::Rejected_Limit;
            }
        } else if (target == AxisId::X) {
            // 逻辑轴 X：检查两个物理轴的限位
            auto safetyX1 = checkMotionSafety(
                m_x1.isAlarmed(),
                m_x1.isPosLimitActive(),
                m_x1.isNegLimitActive(),
                direction
            );
            auto safetyX2 = checkMotionSafety(
                m_x2.isAlarmed(),
                m_x2.isPosLimitActive(),
                m_x2.isNegLimitActive(),
                direction
            );
            if (!safetyX1.isAllowed() || !safetyX2.isAllowed()) {
                return Operability::Rejected_Limit;
            }
        }

        // Step 4: 命令槽检查 (仅对逻辑轴 X)
        if (target == AxisId::X || isCoupled(m_mode)) {
            if (!m_logical.canAcceptCommand()) {
                return Operability::Rejected_Busy;
            }
        }

        return Operability::Allowed;
    }

    // ═══════════════════════════════════
    // 运动命令编排 (约束 19：命令槽互斥)
    // ═══════════════════════════════════

    /**
     * @brief Jog 命令
     *
     * Coupled 模式: 命令写入 LogicalAxis X 的命令槽
     * Decoupled 模式: 命令写入对应 PhysicalAxis 的活跃标记
     */
    CommandResult jog(AxisId target, MotionDirection dir) {
        // Jog 可以覆盖正在执行的 Jog (约束 19: TC-6.6)
        // 先清除命令槽中的 Jog，再走正常的操作检查流程
        if (isCoupled(m_mode) || target == AxisId::X) {
            if (m_logical.pendingCommand().type == LogicalAxis::CommandType::Jog) {
                m_logical.clearCommand();
            }
        }

        auto op = checkOperability(target, dir);
        if (op != Operability::Allowed) {
            auto event = GantryEvents::Event::commandRejected(operabilityToString(op));
            m_events.push_back(event);
            return {false, operabilityToString(op), event};
        }

        // 写入命令槽
        if (isCoupled(m_mode)) {
            std::string err = m_logical.tryAcceptJog(dir);
            if (!err.empty()) {
                auto event = GantryEvents::Event::commandRejected(err);
                m_events.push_back(event);
                return {false, err, event};
            }
        }
        // Decoupled 模式下直接将命令下发给物理轴（此处仅做记录）
        // 实际命令拆分由 DomainService 负责

        return {true, "", GantryEvents::Event{}};
    }

    /**
     * @brief MoveAbsolute 命令
     */
    CommandResult moveAbsolute(AxisId target, double pos) {
        auto op = checkOperability(target);
        if (op != Operability::Allowed) {
            auto event = GantryEvents::Event::commandRejected(operabilityToString(op));
            m_events.push_back(event);
            return {false, operabilityToString(op), event};
        }

        if (isCoupled(m_mode)) {
            std::string err = m_logical.tryAcceptMoveAbsolute(pos);
            if (!err.empty()) {
                auto event = GantryEvents::Event::commandRejected(err);
                m_events.push_back(event);
                return {false, err, event};
            }
        }

        return {true, "", GantryEvents::Event{}};
    }

    /**
     * @brief MoveRelative 命令
     */
    CommandResult moveRelative(AxisId target, double delta) {
        auto op = checkOperability(target);
        if (op != Operability::Allowed) {
            auto event = GantryEvents::Event::commandRejected(operabilityToString(op));
            m_events.push_back(event);
            return {false, operabilityToString(op), event};
        }

        if (isCoupled(m_mode)) {
            std::string err = m_logical.tryAcceptMoveRelative(delta);
            if (!err.empty()) {
                auto event = GantryEvents::Event::commandRejected(err);
                m_events.push_back(event);
                return {false, err, event};
            }
        }

        return {true, "", GantryEvents::Event{}};
    }

    /**
     * @brief Stop 命令 (始终可接受)
     */
    CommandResult stop(AxisId target) {
        // Stop 不做模式/限位/报警检查，始终可接受
        m_logical.tryAcceptStop();
        return {true, "", GantryEvents::Event{}};
    }

    // ═══════════════════════════════════
    // 状态聚合 (约束 20)
    // ═══════════════════════════════════

    /**
     * @brief 将 X1/X2 状态聚合为 X 的聚合状态
     *
     * 聚合规则 (优先级从高到低)：
     *   1. Alarm  > Limit > Moving > Idle
     *   2. 任一轴报警 → X = Error
     *   3. 任一轴限位 → X = 限位阻断 (Idle)
     *   4. 任一轴运动中 → X = 对应运动状态
     *   5. 双轴 Idle → X = Idle
     *
     * 应在每个 PLC 扫描周期调用。
     * Coupled 模式下自动执行 checkSyncMaintenance()。
     */
    void aggregateState() {
        bool anyAlarm = m_x1.isAlarmed() || m_x2.isAlarmed();
        bool anyLimit = m_x1.isAnyLimitActive() || m_x2.isAnyLimitActive();

        // 聚合状态 (优先级: Alarm > Limit > Moving > Idle)
        AxisState aggState;
        LogicalAxis::AggregatedMotion aggMotion = LogicalAxis::AggregatedMotion::Idle;

        if (anyAlarm) {
            aggState = AxisState::Error;
        } else if (anyLimit) {
            aggState = AxisState::Idle;  // 限位阻断运动
        } else if (m_x1AggregatedMotion != LogicalAxis::AggregatedMotion::Idle) {
            aggState = physicalStateToAxisState(m_x1AggregatedMotion);
            aggMotion = m_x1AggregatedMotion;
        } else if (m_x2AggregatedMotion != LogicalAxis::AggregatedMotion::Idle) {
            aggState = physicalStateToAxisState(m_x2AggregatedMotion);
            aggMotion = m_x2AggregatedMotion;
        } else {
            aggState = AxisState::Idle;
        }

        // 逻辑位置: X.position = X1.pos (约束 10)
        GantryPosition logicalPos{m_x1.position()};

        m_logical.applyAggregatedState(aggState, aggMotion, logicalPos, anyLimit);

        // 检测限位事件
        if (m_x1.isAnyLimitActive() && !m_prevX1Limit) {
            m_events.push_back(GantryEvents::Event::limitTriggered("X1"));
        }
        if (m_x2.isAnyLimitActive() && !m_prevX2Limit) {
            m_events.push_back(GantryEvents::Event::limitTriggered("X2"));
        }
        if (m_x1.isAlarmed() && !m_prevX1Alarm) {
            m_events.push_back(GantryEvents::Event::alarmRaised("X1"));
        }
        if (m_x2.isAlarmed() && !m_prevX2Alarm) {
            m_events.push_back(GantryEvents::Event::alarmRaised("X2"));
        }

        // 联动维持检查 (约束 14)
        if (isCoupled(m_mode)) {
            checkSyncMaintenance();
        }

        // 更新前一周期状态
        m_prevX1Limit = m_x1.isAnyLimitActive();
        m_prevX2Limit = m_x2.isAnyLimitActive();
        m_prevX1Alarm = m_x1.isAlarmed();
        m_prevX2Alarm = m_x2.isAlarmed();
    }

    // ═══════════════════════════════════
    // 联动维持检查 (约束 14)
    // ═══════════════════════════════════

    /**
     * @brief 检查联动维持条件
     *
     * 在 aggregateState() 中自动调用。
     * 若位置偏差超出阈值，触发 DeviationFault 事件 + 自动退出 Coupled。
     *
     * @return true = 同步正常, false = 偏差超限
     */
    bool checkSyncMaintenance() {
        if (!isCoupled(m_mode)) return true;

        auto result = CouplingCondition::checkPositionOnly(
            m_x1.position(), m_x2.position()
        );
        if (!result.allowed) {
            double deviation = PositionConsistency::computeDeviation(
                m_x1.position(), m_x2.position()
            );
            m_events.push_back(GantryEvents::Event::deviationFault(
                m_x1.position(), m_x2.position(), deviation
            ));
            requestDecoupling("Deviation fault");
            return false;
        }
        return true;
    }

    // ═══════════════════════════════════
    // 查询 (零副作用)
    // ═══════════════════════════════════

    PhysicalAxis& x1() { return m_x1; }
    PhysicalAxis& x2() { return m_x2; }
    const LogicalAxis& logical() const { return m_logical; }
    const PhysicalAxis& x1() const { return m_x1; }
    const PhysicalAxis& x2() const { return m_x2; }

    // ═══════════════════════════════════
    // 事件管理
    // ═══════════════════════════════════

    /// 获取并清空事件列表
    std::vector<GantryEvents::Event> drainEvents() {
        auto e = std::move(m_events);
        m_events.clear();
        return e;
    }

    /// 查看事件列表 (不清空)
    const std::vector<GantryEvents::Event>& events() const {
        return m_events;
    }

    // ═══════════════════════════════════
    // 用于测试：设置物理轴聚合运动类型
    // ═══════════════════════════════════
    void setX1Motion(LogicalAxis::AggregatedMotion m) { m_x1AggregatedMotion = m; }
    void setX2Motion(LogicalAxis::AggregatedMotion m) { m_x2AggregatedMotion = m; }

private:
    static AxisState physicalStateToAxisState(LogicalAxis::AggregatedMotion m) {
        switch (m) {
            case LogicalAxis::AggregatedMotion::Jogging:        return AxisState::Jogging;
            case LogicalAxis::AggregatedMotion::MovingAbsolute: return AxisState::MovingAbsolute;
            case LogicalAxis::AggregatedMotion::MovingRelative: return AxisState::MovingRelative;
            default:                                            return AxisState::Idle;
        }
    }

    static std::string operabilityToString(Operability op) {
        switch (op) {
            case Operability::Rejected_Mode:  return "Mode: target not operable in current mode";
            case Operability::Rejected_Alarm: return "Safety: alarm is active";
            case Operability::Rejected_Limit: return "Safety: limit triggered";
            case Operability::Rejected_Busy:  return "Slot: command slot is busy";
            default:                          return "";
        }
    }

    PhysicalAxis m_x1;
    PhysicalAxis m_x2;
    LogicalAxis m_logical;
    GantryMode m_mode;
    std::vector<GantryEvents::Event> m_events;

    // 用于状态聚合的物理轴运动类型（由外部设置，模拟反馈）
    LogicalAxis::AggregatedMotion m_x1AggregatedMotion = LogicalAxis::AggregatedMotion::Idle;
    LogicalAxis::AggregatedMotion m_x2AggregatedMotion = LogicalAxis::AggregatedMotion::Idle;

    // 用于检测边沿触发事件
    bool m_prevX1Limit = false;
    bool m_prevX2Limit = false;
    bool m_prevX1Alarm = false;
    bool m_prevX2Alarm = false;
};
