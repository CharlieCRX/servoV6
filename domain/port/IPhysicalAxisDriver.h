#pragma once

#include "../entity/AxisId.h"
#include "../entity/PhysicalAxis.h"
#include <unordered_map>

/**
 * @file IPhysicalAxisDriver.h
 * @brief 物理轴驱动端口接口
 *
 * 职责：
 *   定义领域层与基础设施层之间物理轴操作的契约。
 *   所有对物理硬件的操作（使能、Jog、Move、停止、复位报警）
 *   都通过此端口抽象，领域层不直接依赖具体驱动实现。
 *
 * 实现者：
 *   - infrastructure/FakeAxisDriver.h  (测试/仿真)
 *   - 真实硬件驱动 (生产环境)
 *
 * 使用方：
 *   - GantryCouplingService（不直接使用，由 Orchestrator 调用）
 *   - Application 层 Orchestrator
 *   - EnableUseCase, JogUseCase, MoveUseCase, StopUseCase
 *
 * 约束映射：
 *   约束2  — 模型一致性
 *   约束11 — 物理轴双通道一致性
 */
class IPhysicalAxisDriver {
public:
    virtual ~IPhysicalAxisDriver() = default;

    /// 初始化指定轴
    virtual bool initialize(AxisId id) = 0;

    /// 关闭指定轴
    virtual void shutdown(AxisId id) = 0;

    /// 轴是否就绪
    virtual bool isReady(AxisId id) const = 0;

    /// 使能轴
    virtual void enable(AxisId id) = 0;

    /// 去使能轴
    virtual void disable(AxisId id) = 0;

    /// Jog 正向
    virtual void jogForward(AxisId id, double velocity) = 0;

    /// Jog 负向
    virtual void jogBackward(AxisId id, double velocity) = 0;

    /// 绝对定位
    virtual void moveAbsolute(AxisId id, double position, double velocity) = 0;

    /// 相对定位
    virtual void moveRelative(AxisId id, double delta, double velocity) = 0;

    /// 停止
    virtual void stop(AxisId id) = 0;

    /// 复位报警
    virtual bool resetAlarm(AxisId id) = 0;

    /**
     * @brief 同步物理轴状态
     *
     * 由 HAL 层定期调用（如每 1ms），将所有物理轴的
     * 最新状态读回并返回，供领域层更新 PhysicalAxis 实体。
     *
     * @return 从 AxisId 到 PhysicalAxisState 的映射
     */
    virtual std::unordered_map<AxisId, PhysicalAxisState> syncPhysicalState() = 0;
};
