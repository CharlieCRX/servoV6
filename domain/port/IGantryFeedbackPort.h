#pragma once

#include "../entity/PhysicalAxis.h"
#include "../value/GantryPosition.h"

/**
 * @file IGantryFeedbackPort.h
 * @brief 出站端口 — 接收 HAL 层反馈
 *
 * 领域层通过此端口获取物理轴的实际状态（位置、使能、限位、报警等）。
 * 实现在 Infrastructure/HAL 层。
 *
 * 使用场景：
 *   - 每个 PLC 扫描周期调用一次
 *   - aggregateState() 前先通过此端口更新物理轴状态
 */
class IGantryFeedbackPort {
public:
    virtual ~IGantryFeedbackPort() = default;

    /**
     * @brief 获取物理轴 X1 的完整状态快照
     * @return PhysicalAxisState 包含 position, enabled, alarmed,
     *         posLimitActive, negLimitActive
     */
    virtual PhysicalAxisState getX1Feedback() const = 0;

    /**
     * @brief 获取物理轴 X2 的完整状态快照
     * @return PhysicalAxisState 同上
     */
    virtual PhysicalAxisState getX2Feedback() const = 0;

    /**
     * @brief 触发报警复位
     *
     * 调用后应清除两轴报警状态。
     * 仅应在报警已确认且故障已排除后调用。
     */
    virtual void resetAlarm() = 0;

    /**
     * @brief 查询是否有任何轴处于报警状态
     */
    virtual bool isAnyAlarm() const = 0;

    /**
     * @brief 查询是否有任何轴触发了限位
     */
    virtual bool isAnyLimit() const = 0;
};
