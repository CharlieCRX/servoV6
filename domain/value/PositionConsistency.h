#ifndef POSITION_CONSISTENCY_H
#define POSITION_CONSISTENCY_H
#pragma once

#include <cmath>
#include <string>

/*
 * 龙门位置一致性值对象（纯计算，无状态）
 *
 * 约束依据：
 *   约束9:  X1.pos ≈ -X2.pos（镜像关系）
 *   约束10: X.position = X1.pos（逻辑位置计算）
 *   约束11: |X1.pos + X2.pos| > epsilon → 系统认为"不同步"
 *
 * 所有方法为静态函数，无副作用，仅执行纯数学计算。
 * 不持有任何状态，不依赖任何外部服务。
 *
 * 典型用途：
 *   - 联动建立前检查双轴物理位置是否满足镜像约束
 *   - 实时计算同步偏差用于 SyncGuard 守护
 *   - 从 X1 物理位置推导 X 逻辑位置
 */
class PositionConsistency {
public:
    /*
     * 默认同步偏差容差（单位：mm）
     *
     * 当 |X1.pos + X2.pos| <= kDefaultEpsilon 时，
     * 认为双轴满足位置一致性约束，允许进入联动模式。
     */
    static constexpr double kDefaultEpsilon = 0.01;  // 0.01 mm

    /*
     * 计算 X1 和 X2 之间的同步偏差
     *
     * deviation = |X1.pos + X2.pos|
     *
     * 在理想镜像状态下，X1.pos = -X2.pos，偏差为 0。
     * 偏差越大说明双轴同步性越差。
     *
     * @param x1Pos 物理轴 X1 位置（mm）
     * @param x2Pos 物理轴 X2 位置（mm）
     * @return 双轴同步偏差的绝对值（mm），始终 >= 0
     */
    static constexpr double computeDeviation(double x1Pos, double x2Pos) {
        double sum = x1Pos + x2Pos;
        return (sum >= 0.0) ? sum : -sum;  // fabs 在 constexpr 中不可用于所有编译器
    }

    /*
     * 检查双轴是否满足位置一致性（约束11）
     *
     * |X1.pos + X2.pos| <= epsilon  → true（一致）
     * |X1.pos + X2.pos| >  epsilon  → false（不同步）
     *
     * @param x1Pos  物理轴 X1 位置（mm）
     * @param x2Pos  物理轴 X2 位置（mm）
     * @param epsilon 同步容差，默认 kDefaultEpsilon（0.01 mm）
     * @return true 表示位置一致（满足镜像约束），false 表示不同步
     */
    static constexpr bool isConsistent(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon) {
        return computeDeviation(x1Pos, x2Pos) <= epsilon;
    }

    /*
     * 从 X1 物理位置计算龙门逻辑位置（约束10）
     *
     * X.position = X1.pos
     *
     * 龙门整体逻辑位置直接取自 X1 物理位置。
     *
     * @param x1Pos 物理轴 X1 位置（mm）
     * @return 龙门逻辑位置（mm）
     */
    static constexpr double computeLogicalPosition(double x1Pos) {
        return x1Pos;
    }

    /*
     * 从 X2 物理位置推导期望的 X1 镜像位置
     *
     * expectedX1 = -x2Pos
     *
     * 用于诊断：当位置不一致时，显示 X1 期望位置与实际位置的差异。
     *
     * @param x2Pos 物理轴 X2 位置（mm）
     * @return X2 对应的期望 X1 位置（mm）
     */
    static constexpr double expectedX1FromX2(double x2Pos) {
        return -x2Pos;
    }

    /*
     * 诊断：生成位置一致性失败的原因描述
     *
     * @param x1Pos  当前 X1 位置
     * @param x2Pos  当前 X2 位置
     * @param epsilon 使用的容差值
     * @return 诊断字符串，如 "Position deviation 0.05 mm exceeds epsilon 0.01 mm"
     */
    static std::string describeDeviation(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon) {
        double deviation = computeDeviation(x1Pos, x2Pos);
        return "Position deviation " + std::to_string(deviation) + 
               " mm exceeds epsilon " + std::to_string(epsilon) + " mm";
    }

private:
    // 纯静态方法类，禁止实例化
    PositionConsistency() = delete;
};

#endif // POSITION_CONSISTENCY_H
