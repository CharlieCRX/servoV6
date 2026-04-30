#ifndef COUPLING_CONDITION_H
#define COUPLING_CONDITION_H
#pragma once

#include <string>
#include "PositionConsistency.h"

/*
 * 龙门联动建立条件值对象（纯计算，无状态）
 *
 * 约束依据：
 *   约束13: 联动建立必须满足 4 个必要条件
 *     1. X1 / X2 均已使能
 *     2. 无报警
 *     3. 未触发限位
 *     4. 满足位置一致性 |X1.pos + X2.pos| <= epsilon
 *
 * 这是一个纯值对象，所有方法为静态函数，不持有状态。
 * 它封装了"能否进入联动模式"的判定逻辑，外部只需调用
 * checkAll() 传入当前状态快照即可获得聚合结果。
 *
 * 典型用途：
 *   - GantryOrchestrator 发起联动建立申请前的前置校验
 *   - UI 层判断 Coupled 按钮是否可用的依据
 *   - 诊断联动建立失败的具体原因（failReason）
 */
class CouplingCondition {
public:
    /*
     * 联动条件检查的聚合结果
     *
     * 包含判定结论和失败原因，便于调用方做诊断和日志。
     */
    struct Result {
        bool allowed;            // 是否允许进入联动模式
        std::string failReason;  // 失败原因描述（allowed=true 时为空）

        /* 隐式转换为 bool，简化调用方断言风格 */
        operator bool() const { return allowed; }
    };

    /*
     * 检查所有联动准入条件（约束13 完整版）
     *
     * 任一条件不满足即返回 {false, reason}。
     * 检查顺序：使能 → 报警 → 限位 → 位置一致性。
     *
     * @param x1Enabled    X1 轴是否已使能
     * @param x2Enabled    X2 轴是否已使能
     * @param anyAlarm     是否存在报警（X1 或 X2 任一报警）
     * @param anyLimit     是否触发了限位（X1 或 X2 任一限位）
     * @param x1Position   X1 轴物理位置（mm）
     * @param x2Position   X2 轴物理位置（mm）
     * @param epsilon      位置一致性容差（mm），默认 0.01
     * @return Result 结构体，allowed=true 表示全部满足
     */
    static Result checkAll(
            bool x1Enabled,
            bool x2Enabled,
            bool anyAlarm,
            bool anyLimit,
            double x1Position,
            double x2Position,
            double epsilon = PositionConsistency::kDefaultEpsilon) {

        // 条件1: X1/X2 均已使能
        if (!x1Enabled) {
            return {false, "X1 is not enabled"};
        }
        if (!x2Enabled) {
            return {false, "X2 is not enabled"};
        }

        // 条件2: 无报警
        if (anyAlarm) {
            return {false, "Alarm is active on one or both axes"};
        }

        // 条件3: 未触发限位
        if (anyLimit) {
            return {false, "Limit switch is triggered on one or both axes"};
        }

        // 条件4: 位置一致性
        if (!PositionConsistency::isConsistent(x1Position, x2Position, epsilon)) {
            return {false, PositionConsistency::describeDeviation(x1Position, x2Position, epsilon)};
        }

        return {true, ""};
    }

    /*
     * 仅检查位置一致性条件（用于联动维持监控）
     *
     * 约束14: 联动运行期间必须持续满足 |X1.pos + X2.pos| <= Threshold。
     * 此函数仅做位置偏差检查，不检查使能/报警/限位（这些因素变动
     * 会通过其他机制触发联动退出）。
     *
     * @param x1Position  X1 轴物理位置（mm）
     * @param x2Position  X2 轴物理位置（mm）
     * @param epsilon     位置一致性容差（mm）
     * @return Result 结构体
     */
    static Result checkPositionOnly(
            double x1Position,
            double x2Position,
            double epsilon = PositionConsistency::kDefaultEpsilon) {

        if (!PositionConsistency::isConsistent(x1Position, x2Position, epsilon)) {
            return {false, PositionConsistency::describeDeviation(x1Position, x2Position, epsilon)};
        }

        return {true, ""};
    }

private:
    // 纯静态方法类，禁止实例化
    CouplingCondition() = delete;
};

#endif // COUPLING_CONDITION_H
