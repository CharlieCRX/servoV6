#ifndef GANTRY_VIEW_MODEL_CORE_H
#define GANTRY_VIEW_MODEL_CORE_H

#include <string>
#include <vector>
#include <deque>
#include <sstream>
#include "domain/entity/GantrySystem.h"
#include "domain/event/GantryEvents.h"
#include "domain/value/GantryMode.h"
#include "domain/value/CouplingCondition.h"
#include "infrastructure/logger/Logger.h"

/**
 * @file GantryViewModelCore.h
 * @brief 龙门系统 ViewModel Core — 将 GantrySystem 聚合根的状态投影为 UI 可消费的扁平数据
 *
 * 职责：
 *   - 状态投影：将 GantrySystem 的双轴状态投影为 UI 可绑定的属性
 *   - 控制指令：接收 UI 触发的耦合/解耦/Jog/Move/Stop 命令
 *   - 事件管理：每周期 drain GantrySystem 的事件队列并存入日志和 UI 事件列表
 *   - Tick 驱动：每周期调用 aggregateState() 以维持聚合状态同步
 *
 * 设计原则：
 *   - 纯平台无关层，不依赖 Qt
 *   - 直接委托给 GantrySystem 聚合根，不创建冗余的中间层
 */
class GantryViewModelCore {
public:
    /**
     * @brief 构造龙门 ViewModel Core
     * @param gantry 龙门系统聚合根引用
     */
    explicit GantryViewModelCore(GantrySystem& gantry);

    // ═══════════════════════════════════
    // 1. 状态投影 (State Projection) — 只读
    // ═══════════════════════════════════

    /// 当前龙门模式：true = Coupled（联动）, false = Decoupled（分动）
    bool isCoupled() const;

    /// 聚合状态 (LogicalAxis X 的状态)
    AxisState aggregatedState() const;

    /// 逻辑轴 X 的聚合位置 (mm)
    double position() const;

    /// X1 物理轴位置 (mm)
    double x1Position() const;

    /// X2 物理轴位置 (mm)
    double x2Position() const;

    /// X1 是否使能
    bool x1Enabled() const;

    /// X2 是否使能
    bool x2Enabled() const;

    /// 是否存在任何报警
    bool isAnyAlarm() const;

    /// 是否存在任何限位
    bool isAnyLimit() const;

    /// X1 正限位触发
    bool x1PosLimit() const;

    /// X1 负限位触发
    bool x1NegLimit() const;

    /// X2 正限位触发
    bool x2PosLimit() const;

    /// X2 负限位触发
    bool x2NegLimit() const;

    /// 命令槽是否空闲（可接受新命令）
    bool canAcceptCommand() const;

    /// 状态可读描述
    std::string stateDescription() const;

    /// 联动条件是否满足（用于 UI 启用耦合按钮）
    bool canCouple() const;

    // ═══════════════════════════════════
    // 2. 控制指令 (Control Inputs)
    // ═══════════════════════════════════

    /// 申请联动
    /// @return 联动条件检查结果，allowed=true 表示联动成功
    CouplingCondition::Result requestCoupling();

    /// 申请分动
    /// @param reason 分动原因（可选，用于日志）
    void requestDecoupling(const std::string& reason = "");

    /// Jog 正向（目标 X1）
    void jogX1Forward();

    /// Jog 反向（目标 X1）
    void jogX1Reverse();

    /// Jog 正向（目标 X2）
    void jogX2Forward();

    /// Jog 反向（目标 X2）
    void jogX2Reverse();

    /// Jog 正向（联动模式走逻辑轴 X）
    void jogCoupledForward();

    /// Jog 反向（联动模式走逻辑轴 X）
    void jogCoupledReverse();

    /// Jog 释放（联动模式走逻辑轴 X）
    void jogCoupledRelease();

    /// MoveAbsolute (目标 X1, 仅分动模式)
    void moveAbsoluteX1(double targetPos);

    /// MoveAbsolute (目标 X2, 仅分动模式)
    void moveAbsoluteX2(double targetPos);

    /// MoveAbsolute (目标逻辑轴 X, 仅联动模式)
    void moveAbsoluteCoupled(double targetPos);

    /// MoveRelative (目标 X1, 仅分动模式)
    void moveRelativeX1(double delta);

    /// MoveRelative (目标 X2, 仅分动模式)
    void moveRelativeX2(double delta);

    /// MoveRelative (目标逻辑轴 X, 仅联动模式)
    void moveRelativeCoupled(double delta);

    /// 急停
    void stop();

    // ═══════════════════════════════════
    // 3. 事件管理与日志
    // ═══════════════════════════════════

    /// 获取并清空事件列表（供 UI 显示）
    std::vector<GantryEvents::Event> drainEvents();

    /// 获取事件列表只读引用
    const std::vector<GantryEvents::Event>& events() const;

    /// 获取最近一次命令结果描述
    std::string lastCommandResult() const;

    /// 获取指令日志（最近 N 条）
    std::string getCommandLog() const;

    // ═══════════════════════════════════
    // 4. Tick 驱动
    // ═══════════════════════════════════

    /// 每周期调用：执行状态聚合 + 事件收集 + 日志写出
    void tick();

private:
    GantrySystem& m_gantry;

    /// 缓存的最近一次命令结果消息
    std::string m_lastCommandResult;

    /// 前一周期的缓存值，用于检测变更日志
    GantryMode m_prevMode;
    AxisState m_prevAggState;
    double m_prevPos;
    bool m_prevX1Enabled;
    bool m_prevX2Enabled;
    bool m_prevAnyAlarm;
    bool m_prevAnyLimit;

    /// 指令日志环形缓冲区（最多保留 N 条）
    static constexpr size_t kCommandLogMaxEntries = 200;
    std::deque<std::string> m_commandLog;

    /// 辅助：将命令结果转换为日志并记录
    /// @param cmdName 命令名称（用于日志）
    /// @param result  GantrySystem 返回的命令结果
    void logCommandResult(const std::string& cmdName, const CommandResult& result);

    /// 辅助：检测状态变更并输出日志
    void detectAndLogStateChanges();
};

#endif // GANTRY_VIEW_MODEL_CORE_H
