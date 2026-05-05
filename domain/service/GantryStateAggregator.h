#pragma once

#include "../entity/GantrySystem.h"
#include "../entity/Axis.h"
#include "../value/GantryMode.h"
#include "../value/GantryPosition.h"
#include "../value/SafetyCheckResult.h"

/**
 * @file GantryStateAggregator.h
 * @brief 龙门状态聚合领域服务
 *
 * 职责：
 *   - 封装聚合状态的刷新流程（约束20）
 *   - 联动模式下自动同步位置 + 偏差检测
 *   - 提供 GantryPosition 计算（X = (X1.pos - X2.pos) / 2）
 *
 * 所有操作委托给聚合根 GantrySystem。
 *
 * 覆盖约束：
 *   约束9  — 物理轴位置镜像
 *   约束10 — 逻辑轴位置公式
 *   约束14 — 联动维持
 *   约束20 — X 状态聚合为 X1/X2 的"最坏情形"
 */

class GantryStateAggregator {
public:
    /**
     * @brief 执行一帧状态聚合
     *
     * 委托给 GantrySystem::aggregateState()：
     *   - 聚合 X1/X2 的报警/限位/运动状态到 X
     *   - 联动模式下自动执行联动维持检查（约束14）
     *   - 更新逻辑轴 X 的位置镜像
     *   - 检测并发布限位/报警边沿事件
     *
     * @param system 龙门系统聚合根
     */
    void aggregate(GantrySystem& system) {
        system.aggregateState();
    }

    /**
     * @brief 计算龙门逻辑位置值对象
     *
     * 约束10：X = (X1.pos - X2.pos) / 2
     *
     * 从两个物理轴位置计算龙门中心位置。
     *
     * @return GantryPosition — X 逻辑位置
     */
    GantryPosition computePosition(const GantrySystem& system) const {
        double x1Pos = system.x1().position();
        double x2Pos = system.x2().position();
        // 约束10: X = (X1.pos - X2.pos) / 2
        return GantryPosition((x1Pos - x2Pos) / 2.0);
    }

    /**
     * @brief 在联动模式下计算逻辑轴 X 的位置
     *
     * 约束9：X 位置镜像 = (X1.pos - X2.pos) / 2
     *
     * 纯计算函数，不修改系统状态。
     */
    double computeLogicalPosition(const GantrySystem& system) const {
        double x1Pos = system.x1().position();
        double x2Pos = system.x2().position();
        return (x1Pos - x2Pos) / 2.0;
    }

    /**
     * @brief 获取当前聚合状态的快照描述
     *
     * 纯查询，无副作用。
     */
    AxisState queryAggregatedState(const GantrySystem& system) const {
        return system.logical().aggregatedState();
    }

    /**
     * @brief 获取 X 是否处于运动状态
     */
    bool isXMotionActive(const GantrySystem& system) const {
        return system.logical().isMoving();
    }

    /**
     * @brief 获取命令槽状态
     */
    bool canAcceptCommand(const GantrySystem& system) const {
        return system.logical().canAcceptCommand();
    }
};
