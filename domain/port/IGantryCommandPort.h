#pragma once

#include "../value/MotionDirection.h"
#include "../value/GantryPosition.h"

/**
 * @file IGantryCommandPort.h
 * @brief 入站端口 — 向 HAL 层下发运动指令
 *
 * 领域层通过此端口将运动命令传递给底层驱动。
 * 实现在 Infrastructure/HAL 层。
 *
 * 约束：
 *   - 仅在 GantrySystem 校验通过后调用
 *   - Coupled 模式下，逻辑命令需由端口实现层拆分为 X1/X2 的镜像指令
 *   - Jog 命令可覆盖前一个未执行完的 Jog
 */
class IGantryCommandPort {
public:
    virtual ~IGantryCommandPort() = default;

    // ═══════════════════════════════════
    // 单轴命令 (Decoupled 模式)
    // ═══════════════════════════════════

    /**
     * @brief 向指定物理轴下发 Jog 命令
     * @param axis 目标物理轴 (1 = X1, 2 = X2)
     * @param direction 运动方向
     * @return true = 命令已接受
     */
    virtual bool jogAxis(int axis, MotionDirection direction) = 0;

    /**
     * @brief 向指定物理轴下发绝对定位命令
     * @param axis 目标物理轴 (1 = X1, 2 = X2)
     * @param position 目标位置 (mm)
     * @return true = 命令已接受
     */
    virtual bool moveAbsoluteAxis(int axis, double position) = 0;

    /**
     * @brief 向指定物理轴下发相对定位命令
     * @param axis 目标物理轴 (1 = X1, 2 = X2)
     * @param delta 相对位移 (mm)
     * @return true = 命令已接受
     */
    virtual bool moveRelativeAxis(int axis, double delta) = 0;

    /**
     * @brief 向指定物理轴下发停止命令
     * @param axis 目标物理轴 (1 = X1, 2 = X2)
     * @return true = 命令已接受
     */
    virtual bool stopAxis(int axis) = 0;

    // ═══════════════════════════════════
    // 龙门命令 (Coupled 模式)
    // ═══════════════════════════════════

    /**
     * @brief 向龙门系统下发 Jog 命令
     *
     * Coupled 模式下调用。端口实现需拆分为：
     *   - X1: jogAxis(1, direction)
     *   - X2: jogAxis(2, invert(direction))  镜像方向
     *
     * @param direction 逻辑运动方向
     * @return true = 命令已接受
     */
    virtual bool jogGantry(MotionDirection direction) = 0;

    /**
     * @brief 向龙门系统下发绝对定位命令
     *
     * Coupled 模式下调用。端口实现需拆分为：
     *   - X1: moveAbsoluteAxis(1, position)
     *   - X2: moveAbsoluteAxis(2, -position)  镜像位置
     *
     * @param position 逻辑目标位置 (mm)
     * @return true = 命令已接受
     */
    virtual bool moveAbsoluteGantry(double position) = 0;

    /**
     * @brief 向龙门系统下发相对定位命令
     *
     * Coupled 模式下调用。端口实现需拆分为：
     *   - X1: moveRelativeAxis(1, delta)
     *   - X2: moveRelativeAxis(2, -delta)  镜像位移
     *
     * @param delta 逻辑相对位移 (mm)
     * @return true = 命令已接受
     */
    virtual bool moveRelativeGantry(double delta) = 0;

    /**
     * @brief 向龙门系统下发停止命令
     *
     * 同时停止 X1 和 X2 两个物理轴。
     * @return true = 命令已接受
     */
    virtual bool stopGantry() = 0;

    // ═══════════════════════════════════
    // 通用
    // ═══════════════════════════════════

    /**
     * @brief 查询指定物理轴的命令槽是否空闲
     * @param axis 物理轴编号 (1 = X1, 2 = X2)
     * @return true = 可接受新命令
     */
    virtual bool isAxisSlotFree(int axis) const = 0;
};
