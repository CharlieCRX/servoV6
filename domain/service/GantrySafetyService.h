#pragma once

#include "../entity/GantrySystem.h"
#include "../value/MotionDirection.h"
#include "../value/SafetyCheckResult.h"
#include <string>
#include <sstream>

/**
 * @file GantrySafetyService.h
 * @brief 龙门安全约束领域服务
 *
 * 职责：
 *   - 封装龙门双轴的安全状态查询
 *   - 实现运动方向限位约束（约束16：可Jog远离，不可Move/Jog靠近）
 *   - 实现报警约束（约束17：禁止所有运动，只允许ResetAlarm）
 *   - 提供统一的安全检查入口
 *
 * 所有操作基于 GantrySystem 的只读状态。
 *
 * 覆盖约束：
 *   约束15 — 限位优先级最高
 *   约束16 — 限位后行为限制
 *   约束17 — 报警约束
 */

class GantrySafetyService {
public:
    /**
     * @brief 通用运动安全检查
     *
     * 检查顺序：Alarm > Limit > OK
     * 报警和限位都会导致拒绝，不区分方向（用于 Move 命令）。
     *
     * @return SafetyCheckResult
     */
    SafetyCheckResult isMotionAllowed(const GantrySystem& system,
                                       MotionDirection dir) const {
        // 1. 报警检查（约束17: 优先级最高）
        if (system.x1().isAlarmed() || system.x2().isAlarmed()) {
            return SafetyCheckResult::rejectedDueToAlarm();
        }

        // 2. 限位检查（约束15/16: 任何方向都拒绝 Move 类操作）
        if (system.x1().isAnyLimitActive() || system.x2().isAnyLimitActive()) {
            std::ostringstream oss;
            oss << "Motion blocked: limit active; ";
            if (system.x1().isPosLimitActive()) oss << "X1 posLimit; ";
            if (system.x1().isNegLimitActive()) oss << "X1 negLimit; ";
            if (system.x2().isPosLimitActive()) oss << "X2 posLimit; ";
            if (system.x2().isNegLimitActive()) oss << "X2 negLimit; ";
            return SafetyCheckResult(
                SafetyCheckResult::Verdict::Rejected_Limit,
                oss.str()
            );
        }

        return SafetyCheckResult::allowed();
    }

    /**
     * @brief Move 命令安全检查
     *
     * 与 isMotionAllowed 行为相同：
     * 报警和限位状态下拒绝所有 Move 操作（MoveAbsolute / MoveRelative）。
     *
     * @return SafetyCheckResult
     */
    SafetyCheckResult isMoveAllowed(const GantrySystem& system,
                                     MotionDirection dir) const {
        return isMotionAllowed(system, dir);
    }

    /**
     * @brief Jog 命令安全检查
     *
     * 约束16：限位时允许 Jog 远离限位方向，禁止 Jog 靠近。
     * 约束17：报警时禁止所有 Jog。
     *
     * @return true = Jog 被允许
     */
    bool isJogAllowed(const GantrySystem& system,
                      MotionDirection dir) const {
        // 1. 报警检查（约束17）
        if (system.x1().isAlarmed() || system.x2().isAlarmed()) {
            return false;
        }

        // 2. 限位方向检查（约束16）
        // X1 正向限位 → 禁止 Forward Jog
        if (system.x1().isPosLimitActive() && dir == MotionDirection::Forward) {
            return false;
        }
        // X1 负向限位 → 禁止 Backward Jog
        if (system.x1().isNegLimitActive() && dir == MotionDirection::Backward) {
            return false;
        }
        // X2 正向限位 → 禁止 Forward Jog
        if (system.x2().isPosLimitActive() && dir == MotionDirection::Forward) {
            return false;
        }
        // X2 负向限位 → 禁止 Backward Jog
        if (system.x2().isNegLimitActive() && dir == MotionDirection::Backward) {
            return false;
        }

        return true;
    }

    /**
     * @brief Reset 操作检查
     *
     * 报警状态下允许 ResetAlarm 操作（约束17）。
     */
    bool isResetAllowed(const GantrySystem& system) const {
        // Reset 不检查报警（因为报警时需要 Reset 来清除）
        // 仅检查是否真的存在报警需要清除
        return isAnyAlarm(system);
    }

    /**
     * @brief 是否存在任何报警
     */
    bool isAnyAlarm(const GantrySystem& system) const {
        return system.x1().isAlarmed() || system.x2().isAlarmed();
    }

    /**
     * @brief 是否存在任何限位触发
     */
    bool isAnyLimit(const GantrySystem& system) const {
        return system.x1().isAnyLimitActive() || system.x2().isAnyLimitActive();
    }
};
