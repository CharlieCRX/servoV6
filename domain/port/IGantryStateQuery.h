#pragma once

#include "../entity/AxisId.h"
#include "../entity/Axis.h"
#include "../value/GantryMode.h"
#include <string>

/**
 * @file IGantryStateQuery.h
 * @brief 龙门状态查询端口接口
 *
 * 职责：
 *   定义领域层对外暴露的只读状态查询契约。
 *   Presentation 层和 Application 层通过此端口获取
 *   龙门的当前状态，而不直接访问聚合根内部结构。
 *
 * 实现者：
 *   - GantrySystem（聚合根实现此接口）
 *   - 系统上下文适配器
 *
 * 使用方：
 *   - Presentation ViewModel
 *   - Application 层 Orchestrator
 *   - 日志/诊断模块
 *
 * 约束映射：
 *   约束2  — 模型一致性
 *   约束9  — 物理轴位置镜像
 */
class IGantryStateQuery {
public:
    virtual ~IGantryStateQuery() = default;

    /// 当前龙门模式
    virtual GantryMode mode() const = 0;

    /// 逻辑轴 X 的聚合状态
    virtual AxisState aggregatedState() const = 0;

    /// 是否处于联动模式
    virtual bool isCoupled() const = 0;

    /// 逻辑轴 X 的当前位置（加权）
    virtual double position() const = 0;

    /// X1 物理轴位置
    virtual double x1Position() const = 0;

    /// X2 物理轴位置
    virtual double x2Position() const = 0;

    /// X1 是否使能
    virtual bool x1Enabled() const = 0;

    /// X2 是否使能
    virtual bool x2Enabled() const = 0;

    /// 是否存在报警
    virtual bool isAnyAlarm() const = 0;

    /// 是否存在限位
    virtual bool isAnyLimit() const = 0;

    /// 逻辑轴命令槽是否空闲
    virtual bool canAcceptCommand() const = 0;

    /// 指定目标是否可操作
    virtual bool isTargetOperable(AxisId target) const = 0;

    /// 聚合状态的可读描述
    virtual std::string stateDescription() const = 0;
};
