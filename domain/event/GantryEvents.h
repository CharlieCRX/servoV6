#pragma once

#include <string>

/**
 * @file GantryEvents.h
 * @brief 龙门系统领域事件定义
 *
 * 职责：
 *   - 定义龙门系统内所有领域事件，供 Application 层订阅和日志/诊断
 *   - 事件是不可变值对象，用于解耦领域服务间的通信
 *
 * 事件流：
 *   requestCoupling()
 *     ├─ checkAll() 通过 → Coupled 事件发布
 *     └─ checkAll() 失败 → (不发布事件，仅返回 Result)
 *
 *   aggregateState() (每周期)
 *     ├─ 检测到限位 → LimitTriggered 事件
 *     ├─ 检测到报警 → AlarmRaised 事件
 *     └─ Coupled 模式下 checkSyncMaintenance() 失败
 *        → DeviationFault 事件 + 自动退出联动
 *
 *   jog() / moveAbsolute() / moveRelative()
 *     └─ 目标不可操作 / 命令槽忙 / 安全检查失败
 *        → CommandRejected 事件 (含拒绝原因)
 */

namespace GantryEvents {

/**
 * @brief 领域事件类型枚举
 */
enum class Type {
    None,

    // 模式变更
    CouplingRequested,    ///< 联动建立申请已发起
    Coupled,              ///< 联动建立成功
    Decoupled,            ///< 联动退出（主动/被动）

    // 异常
    DeviationFault,       ///< 同步偏差超限，联动已被强制退出
    LimitTriggered,       ///< 限位触发
    AlarmRaised,          ///< 报警发生

    // 命令
    CommandRejected,      ///< 命令被拒（含拒绝原因）
};

/**
 * @brief 领域事件
 *
 * 包含事件类型和可读描述。根据事件类型不同，
 * 描述字段携带的内容也不同。
 */
struct Event {
    Type type = Type::None;
    std::string description;

    /// 便捷判断
    bool isNone() const { return type == Type::None; }

    /// 工厂方法：模式变更事件
    static Event couplingRequested() {
        return {Type::CouplingRequested, "Coupling requested"};
    }
    static Event coupled() {
        return {Type::Coupled, "Coupled mode entered"};
    }
    static Event decoupled(const std::string& reason = "") {
        return {Type::Decoupled,
                reason.empty() ? "Decoupled mode entered"
                               : "Decoupled: " + reason};
    }

    /// 工厂方法：异常事件
    static Event deviationFault(double x1Pos, double x2Pos, double deviation) {
        return {Type::DeviationFault,
                "Deviation fault: X1=" + std::to_string(x1Pos) +
                    " X2=" + std::to_string(x2Pos) +
                    " deviation=" + std::to_string(deviation)};
    }
    static Event limitTriggered(const std::string& axisName) {
        return {Type::LimitTriggered, "Limit triggered on " + axisName};
    }
    static Event alarmRaised(const std::string& axisName) {
        return {Type::AlarmRaised, "Alarm raised on " + axisName};
    }

    /// 工厂方法：命令拒绝事件
    static Event commandRejected(const std::string& reason) {
        return {Type::CommandRejected, "Command rejected: " + reason};
    }
};

}  // namespace GantryEvents
