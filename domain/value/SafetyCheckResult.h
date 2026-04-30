#ifndef SAFETY_CHECK_RESULT_H
#define SAFETY_CHECK_RESULT_H
#pragma once

#include <string>
#include "MotionDirection.h"

/*
 * 运动安全校验结果值对象
 *
 * 约束依据：
 *   约束15: 限位优先级最高 — 触发限位后所有运动视为非法
 *   约束16: 限位后仅允许向远离限位方向 Jog
 *   约束17: 报警状态下禁止所有运动
 *
 * 这是一个纯值对象，封装了"当前运动指令是否合法"的判定结果。
 * 它不持有状态，仅作为安全检查的返回值。
 *
 * 典型用途：
 *   - JogUseCase / MoveUseCase 执行前调用 GantrySafety 校验
 *   - 反馈给上层调用方具体被拒绝的原因
 *   - UI 层根据拒绝原因显示提示信息
 */
class SafetyCheckResult {
public:
    /* 检查结论枚举 */
    enum class Verdict {
        Allowed,              // 允许执行该运动
        Rejected_Alarm,       // 拒绝：处于报警状态
        Rejected_Limit,       // 拒绝：触发限位且非远离限位方向
        Rejected_LimitForward, // 拒绝：触发正向限位，不允许正向运动
        Rejected_LimitBackward // 拒绝：触发负向限位，不允许负向运动
    };

    /*
     * 构造一个安全检查结果
     *
     * @param verdict 检查结论
     * @param reason  可选的详细原因描述（用于日志/诊断）
     */
    explicit SafetyCheckResult(Verdict verdict, std::string reason = "")
        : m_verdict(verdict)
        , m_reason(std::move(reason)) {}

    /* 工厂：允许 */
    static SafetyCheckResult allowed() {
        return SafetyCheckResult(Verdict::Allowed, "Allowed");
    }

    /* 工厂：报警拒绝 */
    static SafetyCheckResult rejectedDueToAlarm() {
        return SafetyCheckResult(Verdict::Rejected_Alarm, 
                                 "Motion rejected: alarm is active");
    }

    /* 工厂：正向限位拒绝 */
    static SafetyCheckResult rejectedDueToForwardLimit() {
        return SafetyCheckResult(Verdict::Rejected_LimitForward,
                                 "Motion rejected: forward limit triggered, "
                                 "only backward jog is allowed");
    }

    /* 工厂：负向限位拒绝 */
    static SafetyCheckResult rejectedDueToBackwardLimit() {
        return SafetyCheckResult(Verdict::Rejected_LimitBackward,
                                 "Motion rejected: backward limit triggered, "
                                 "only forward jog is allowed");
    }

    /* 工厂：无法确定方向的限位拒绝（兜底） */
    static SafetyCheckResult rejectedDueToLimit() {
        return SafetyCheckResult(Verdict::Rejected_Limit,
                                 "Motion rejected: limit switch is triggered");
    }

    /* 是否允许运动 */
    bool isAllowed() const { return m_verdict == Verdict::Allowed; }

    /* 是否被拒绝 */
    bool isRejected() const { return m_verdict != Verdict::Allowed; }

    /* 获取判决结论 */
    Verdict verdict() const { return m_verdict; }

    /* 获取拒绝原因描述 */
    const std::string& reason() const { return m_reason; }

    /* 隐式转换为 bool（true = 允许） */
    explicit operator bool() const { return isAllowed(); }

    /* 值相等比较 */
    bool operator==(const SafetyCheckResult& other) const {
        return m_verdict == other.m_verdict && m_reason == other.m_reason;
    }

    bool operator!=(const SafetyCheckResult& other) const {
        return !(*this == other);
    }

private:
    Verdict m_verdict;
    std::string m_reason;
};

/*
 * 运动安全检查入口（纯函数、无状态）
 *
 * 封装约束15/16/17的判定逻辑：
 *   1. 如果处于 Alarm 状态 → 禁止所有运动（约束17）
 *   2. 如果触发正向限位 → 仅允许 Backward Jog（约束16）
 *   3. 如果触发负向限位 → 仅允许 Forward Jog（约束16）
 *   4. 如果限位方向不明确（如两向同时触发）→ 全部禁止
 *   5. 无报警无触发限位 → 允许
 *
 * 注意：此函数仅做安全层面检查，不关心操作对象（X/X1/X2）、
 * 不关心运动类型（Jog/Move）。这些语义由上层 UseCase 控制。
 *
 * @param isAlarm        是否处于报警状态
 * @param fwdLimitActive 正向限位是否激活
 * @param bwdLimitActive 负向限位是否激活
 * @param direction      请求的运动方向
 * @return SafetyCheckResult 判断结果
 */
inline SafetyCheckResult checkMotionSafety(
        bool isAlarm,
        bool fwdLimitActive,
        bool bwdLimitActive,
        MotionDirection direction) {

    // 约束17: 报警优先级最高 → 禁止所有运动
    if (isAlarm) {
        return SafetyCheckResult::rejectedDueToAlarm();
    }

    // 约束15/16: 限位检查
    // 双向限位同时激活（极端故障）→ 所有方向禁止
    if (fwdLimitActive && bwdLimitActive) {
        if (isForward(direction)) {
            return SafetyCheckResult::rejectedDueToForwardLimit();
        }
        if (isBackward(direction)) {
            return SafetyCheckResult::rejectedDueToBackwardLimit();
        }
        return SafetyCheckResult::rejectedDueToLimit();
    }

    // 仅正向限位激活 → 仅允许 Backward 方向运动（远离正向限位）
    if (fwdLimitActive) {
        if (isForward(direction)) {
            return SafetyCheckResult::rejectedDueToForwardLimit();
        }
        // Backward 方向 → 允许（远离限位）
        return SafetyCheckResult::allowed();
    }

    // 仅负向限位激活 → 仅允许 Forward 方向运动（远离负向限位）
    if (bwdLimitActive) {
        if (isBackward(direction)) {
            return SafetyCheckResult::rejectedDueToBackwardLimit();
        }
        // Forward 方向 → 允许（远离限位）
        return SafetyCheckResult::allowed();
    }

    // 无报警、无限位 → 安全，允许所有运动
    return SafetyCheckResult::allowed();
}

#endif // SAFETY_CHECK_RESULT_H
