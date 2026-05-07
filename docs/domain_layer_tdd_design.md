# Domain 层 TDD 架构设计文档  
## —— 双轴龙门同步系统

> **版本**：v2.0  
> **状态**：Phase 0–7 已实现，Phase 8+ 待开发  
> **业务约束来源**：[龙门系统接口业务语义约束](./domain/docs/龙门系统接口业务语义约束.md)  
> **上次更新**：2026-05-07

---

## 一、概述

### 1.1 设计目标

将"控制电机"升级为"定义运动语义系统"。Domain 层仅负责"是否允许操作"，不负责"如何执行"。所有物理差异（方向、坐标）由 HAL 层消化。

### 1.2 核心语义真理

1. **X 是唯一对外暴露的逻辑轴**  
2. **X1/X2 是实现细节，不参与业务决策**  
3. **所有操作基于统一逻辑方向（Forward/Backward）**  
4. **联动是状态，不是命令**  
5. **分动与联动互斥**  
6. **联动建立必须满足位置一致性**  
7. **运行过程中必须持续满足同步约束**  
8. **限位与报警优先级高于一切运动**  
9. **Domain 负责"是否允许操作"，不负责"如何执行"**  
10. **所有物理差异由 HAL 层消化**

### 1.3 分层架构

```
┌─────────────────────────────┐
│     Application / UI        │  UseCase 层，编排业务流程
├─────────────────────────────┤
│      Domain 层 (本层)        │  实体、值对象、领域服务、端口
├─────────────────────────────┤
│   Infrastructure / HAL      │  驱动实现、PLC 抽象、Fake 仿真
└─────────────────────────────┘
```

---

## 二、领域组件全景图

### 2.1 组件分类与文件清单

| 类型 | 组件名 | 文件路径 | 状态 |
|------|--------|----------|:----:|
| **实体** | `Axis` | `domain/entity/Axis.h` | ✅ 已完成 |
| **实体** | `PhysicalAxis` | `domain/entity/PhysicalAxis.h` | ✅ 已完成 |
| **实体** | `LogicalAxis` | `domain/entity/LogicalAxis.h` | ✅ 已完成 |
| **实体 (聚合根)** | `GantrySystem` | `domain/entity/GantrySystem.h` | ✅ 已完成 |
| **领域事件** | `GantryEvents` | `domain/event/GantryEvents.h` | ✅ 已完成 |
| **值对象** | `MotionDirection` | `domain/value/MotionDirection.h` | ✅ 已完成 |
| **值对象** | `GantryPosition` | `domain/value/GantryPosition.h` | ✅ 已完成 |
| **值对象** | `PositionConsistency` | `domain/value/PositionConsistency.h` | ✅ 已完成 |
| **值对象** | `CouplingCondition` | `domain/value/CouplingCondition.h` | ✅ 已完成 |
| **值对象** | `SafetyCheckResult` | `domain/value/SafetyCheckResult.h` | ✅ 已完成 |
| **值对象** | `Operability` | `domain/value/Operability.h` | ✅ 已完成 |
| **值对象** | `CommandResult` | `domain/value/CommandResult.h` | ✅ 已完成 |
| **值对象** | `PhysicalAxisState` | (内联于 PhysicalAxis.h) | ✅ 已完成 |
| **服务** | `GantryCouplingService` | `domain/service/GantryCouplingService.h` | ✅ 已完成 |
| **服务** | `GantrySafetyService` | `domain/service/GantrySafetyService.h` | ✅ 已完成 |
| **服务** | `GantryStateAggregator` | `domain/service/GantryStateAggregator.h` | ✅ 已完成 |
| **服务** | `GantryDomainService` | `domain/service/GantryDomainService.h` | ⬜ 待实现 (Phase 8) |
| **端口** | `IGantryStateQuery` | `domain/port/IGantryStateQuery.h` | ✅ 已完成 |
| **端口** | `IGantryEventPublisher` | `domain/port/IGantryEventPublisher.h` | ✅ 已完成 |
| **端口** | `IGantryFeedbackPort` | `domain/port/IGantryFeedbackPort.h` | ✅ 已完成 |
| **端口** | `IGantryCommandPort` | `domain/port/IGantryCommandPort.h` | ✅ 已完成 |
| **端口** | `IGantryEventBus` | `domain/port/IGantryEventBus.h` | ✅ 已完成 |
| **Fake 实现** | `FakePLC` | `infrastructure/FakePLC.h` | ✅ 已完成 |
| **Fake 实现** | `FakeGantryFeedbackPort` | `infrastructure/FakeGantryFeedbackPort.h` | ✅ 已完成 |
| **Fake 实现** | `FakeGantryCommandPort` | `infrastructure/FakeGantryCommandPort.h` | ✅ 已完成 |
| **Fake 实现** | `FakeGantryEventBus` | `infrastructure/FakeGantryEventBus.h` | ✅ 已完成 |

### 2.2 组件关系图

```
┌──────────────────────────────────────────────────────────────────┐
│                      GantrySystem (聚合根)                         │
│  ┌─────────────┐  ┌─────────────┐  ┌──────────────────────────┐ │
│  │ PhysicalAxis │  │ PhysicalAxis │  │     LogicalAxis          │ │
│  │  (X1)        │  │  (X2)        │  │     (X = 逻辑轴)         │ │
│  │  position()  │  │  position()  │  │  aggregatedState()      │ │
│  │  isEnabled() │  │  isEnabled() │  │  canAcceptCommand()     │ │
│  │  isAlarmed() │  │  isAlarmed() │  │  tryAcceptJog/Move(*)   │ │
│  │  isLimit()   │  │  isLimit()   │  │  CommandSlot 互斥       │ │
│  │  syncState() │  │  syncState() │  └──────────────────────────┘ │
│  └──────┬───────┘  └──────┬───────┘                               │
│         │                 │                                        │
│         └──────┬──────────┘                                        │
│                ▼                                                   │
│  ┌─────────────────────────────────────────────┐                  │
│  │        aggregateState() [每周期]             │                  │
│  │        checkSyncMaintenance() [仅Coupled]   │                  │
│  │        requestCoupling() / requestDecoupling│                  │
│  │        checkOperability() × 运动命令         │                  │
│  └─────────────────────────────────────────────┘                  │
└──────────────────────────────────────────────────────────────────┘
        │                           │
        │ IGantryStateQuery         │ IGantryEventPublisher
        ▼                           ▼
┌──────────────────┐    ┌──────────────────────┐
│ IGantryStateQuery  │    │ GantryEvents::Event   │
│ mode()            │    │ Coupled / Decoupled   │
│ position()        │    │ DeviationFault        │
│ isCoupled()       │    │ LimitTriggered        │
│ aggregatedState() │    │ AlarmRaised           │
│ ...               │    │ CommandRejected       │
└──────────────────┘    └──────────────────────┘
```

### 2.3 领域事件流

```
requestCoupling()
    ├─ CouplingRequested 事件 → CouplingCondition::checkAll()
    │    ├─ 通过 → Coupled 事件, mode = Coupled
    │    └─ 失败 → 仅返回 Result, 无 Coupled 事件

aggregateState() (每扫描周期)
    ├─ 限位边沿触发 → LimitTriggered 事件
    ├─ 报警边沿触发 → AlarmRaised 事件
    ├─ Coupled 模式 + checkSyncMaintenance() 失败
    │    └─ DeviationFault 事件 → 自动 requestDecoupling("Deviation fault")
    └─ 无异常 → 无事件

jog() / moveAbsolute() / moveRelative()
    ├─ 操作拒绝 → CommandRejected 事件 (含原因)
    └─ 操作接受 → 命令写入 LogicalAxis 命令槽

requestDecoupling()
    └─ Decoupled 事件 (含原因)
```

---

## 三、业务约束 → 代码 → 测试 完全映射表

### 3.1 约束覆盖矩阵

| # | 约束 | 实现组件 | 测试文件 | 测试数 |
|:-:|------|---------|---------|:------:|
| 1–4 | 模式定义与互斥 | `GantryMode` + `GantrySystem` | `test_gantry_system_mode.cpp` | 11 |
| 5–7 | 方向唯一性 | `MotionDirection` | `test_gantry_direction.cpp` | 12+ |
| 8–10 | 位置定义 + 逻辑位置 = X1.pos | `PositionConsistency` + `GantrySystem` | `test_gantry_sync.cpp` + `test_gantry_system_aggregation.cpp` | 8+ |
| 11 | 位置一致性判定 | `PositionConsistency` | `test_gantry_sync.cpp` | 5+ |
| 12–13 | 联动建立条件 | `CouplingCondition` + `GantrySystem::requestCoupling()` | `test_gantry_sync.cpp` + `test_gantry_system_mode.cpp` | 14+ |
| 14 | 联动持续同步 | `GantrySystem::checkSyncMaintenance()` | `test_gantry_system_aggregation.cpp` + `test_gantry_system_integration.cpp` | 4+ |
| 15–16 | 限位优先级 + 方向限制 | `SafetyCheckResult` + `GantrySystem::checkOperability()` | `test_gantry_sync.cpp` + `test_gantry_system_operability.cpp` | 28+ |
| 17 | 报警禁止运动 | `GantrySystem::checkOperability()` | `test_gantry_system_motion.cpp` + `test_gantry_system_operability.cpp` | 6+ |
| 18 | 操作目标互斥 | `GantrySystem::isTargetOperable()` | `test_gantry_system_operability.cpp` | 6 |
| 19 | 运动互斥 (命令槽) | `LogicalAxis` 命令槽 | `test_gantry_system_motion.cpp` | 5 |
| 20 | 状态聚合 X1/X2 → X | `GantrySystem::aggregateState()` | `test_gantry_system_aggregation.cpp` | 11 |

### 3.2 测试文件完整清单

| 测试文件 | 测试范围 | 估算测试数 | 状态 |
|---------|---------|:---------:|:----:|
| `tests/domain/test_axis.cpp` | Axis 实体 (单轴状态机) | 19 | ✅ |
| `tests/domain/test_system_context.cpp` | SystemContext | ~5 | ✅ |
| `tests/domain/gantry/test_gantry_mode.cpp` | GantryMode 枚举 | 18 | ✅ |
| `tests/domain/gantry/test_gantry_direction.cpp` | MotionDirection 方向语义 | 12 | ✅ |
| `tests/domain/gantry/test_gantry_position.cpp` | GantryPosition 位置计算 | 19 | ✅ |
| `tests/domain/gantry/test_gantry_sync.cpp` | PositionConsistency + CouplingCondition + SafetyCheckResult | 60 | ✅ |
| `tests/domain/gantry/test_gantry_activation.cpp` | (已拆分为下面5个文件) | — | — |
| `tests/domain/gantry/test_gantry_system_mode.cpp` | GantrySystem 模式管理 (TS5.2) | 11 | ✅ |
| `tests/domain/gantry/test_gantry_system_operability.cpp` | 操作可行性检查 (TS5.3) | 14 | ✅ |
| `tests/domain/gantry/test_gantry_system_motion.cpp` | 运动命令 (TS5.4) | 13 | ✅ |
| `tests/domain/gantry/test_gantry_system_aggregation.cpp` | 状态聚合 (TS5.5) | 11 | ✅ |
| `tests/domain/gantry/test_gantry_system_integration.cpp` | 聚合根集成 (TC-7) | 14 | ✅ |
| `tests/domain/gantry/test_gantry_full_integration.cpp` | 全流程集成 (TS6.1) | 5 | ✅ |
| `tests/domain/gantry/test_gantry_coupling_service.cpp` | GantryCouplingService | ~8 | ✅ |
| `tests/domain/gantry/test_gantry_safety_service.cpp` | GantrySafetyService | ~8 | ✅ |
| `tests/domain/gantry/test_physical_axis_state.cpp` | PhysicalAxisState DTO | 4 | ✅ |
| `tests/domain/gantry/test_operability.cpp` | Operability 枚举 | 2+ | ✅ |
| `tests/domain/gantry/test_command_result.cpp` | CommandResult 结构体 | 2+ | ✅ |
| `tests/domain/gantry/test_physical_axis_sync.cpp` | PhysicalAxis::syncState | 6+ | ✅ |
| `tests/infrastructure/test_fake_gantry_feedback_port.cpp` | FakeGantryFeedbackPort | 6 | ✅ |
| `tests/infrastructure/test_fake_gantry_command_port.cpp` | FakeGantryCommandPort | 10 | ✅ |
| `tests/infrastructure/test_fake_gantry_event_bus.cpp` | FakeGantryEventBus | 3 | ✅ |
| `tests/infrastructure/test_fake_plc.cpp` | FakePLC | ~3 | ✅ |
| `tests/infrastructure/test_system_integration.cpp` | 系统级集成 | ~5 | ✅ |
| **合计** | | **~260+** | ✅ |

> 当前总计约 **260+ 个测试用例**，远超过原始设计文档规划的 128+16+30+15 ≈ 189 个的目标。

---

## 四、实体详解

### 4.1 Axis（单轴实体）

**文件**：`domain/entity/Axis.h` → `domain/entity/Axis.cpp`  
**测试**：`tests/domain/test_axis.cpp`（19 个用例）

**职责**：封装单个物理轴的全部运行时状态与命令槽。

**要点**：
- 持有状态机：`AxisState`（Unknown → Disabled → Idle → Jogging → MovingAbsolute → MovingRelative → Error）
- 持有反馈镜像：位置、限位状态
- **不感知龙门语义**，完全按单轴逻辑运作

**状态已实现**：✅ 全部完成

---

### 4.2 PhysicalAxis（物理轴实体）

**文件**：`domain/entity/PhysicalAxis.h`（纯头文件）  
**测试**：`tests/domain/gantry/test_physical_axis_state.cpp`、`tests/domain/gantry/test_physical_axis_sync.cpp`

**职责**：
- 封装单个物理执行单元（X1 或 X2）的运行时状态镜像
- 持有身份标记 `AxisId`（X1 或 X2）
- 为 `GantrySystem` 提供原始数据供安全检查与位置一致性计算

**已实现 API**：
```cpp
class PhysicalAxis {
public:
    explicit PhysicalAxis(AxisId id);
    AxisId id() const;
    bool isEnabled() const;
    bool isAlarmed() const;
    bool isPosLimitActive() const;
    bool isNegLimitActive() const;
    bool isAnyLimitActive() const;
    double position() const;
    PhysicalAxisState snapshot() const;       // 一次性快照导出
    void syncState(const PhysicalAxisState&); // 由 HAL 反馈定期调用
    void setEnabled(bool);
    void setAlarmed(bool);
    void setPosLimitActive(bool);
    void setNegLimitActive(bool);
    void setPosition(double);
};
```

**状态**：✅ 已完成

---

### 4.3 LogicalAxis（逻辑轴实体）

**文件**：`domain/entity/LogicalAxis.h`（纯头文件）

**职责**：
- 表示龙门**逻辑整体** X
- 持有聚合后的逻辑位置 `GantryPosition`
- 持有聚合后的状态（Idle / Moving / Error / Limit）
- 持有统一命令槽（约束 19：运动互斥）

**已实现 API**：
```cpp
class LogicalAxis {
public:
    enum class AggregatedMotion { Idle, Jogging, MovingAbsolute, MovingRelative };
    enum class CommandType { None, Jog, MoveAbsolute, MoveRelative, Stop };
    struct CommandSlot { ... };

    GantryPosition position() const;
    void setPosition(GantryPosition);

    AxisState aggregatedState() const;
    AggregatedMotion motion() const;
    bool isMoving() const;
    bool isError() const;
    bool hasActiveLimit() const;

    void applyAggregatedState(AxisState, AggregatedMotion, GantryPosition, bool anyLimit);

    bool canAcceptCommand() const;
    std::string tryAcceptJog(MotionDirection);
    std::string tryAcceptMoveAbsolute(double);
    std::string tryAcceptMoveRelative(double);
    std::string tryAcceptStop();  // 始终成功
    void clearCommand();
    const CommandSlot& pendingCommand() const;
};
```

**状态**：✅ 已完成

---

### 4.4 GantrySystem（龙门系统聚合根）

**文件**：`domain/entity/GantrySystem.h`（纯头文件）

**职责**：
- 管理 `GantryMode`（Coupled / Decoupled）状态机
- 持有 2 个 `PhysicalAxis`（X1, X2）和 1 个 `LogicalAxis`（X）
- 执行联动建立条件校验（约束 12–13）和联动维持校验（约束 14）
- 在每次操作时执行**操作目标互斥**（约束 18）和**运动互斥**（约束 19）
- 每周期执行**状态聚合**（约束 20）
- 发布领域事件（`GantryEvents`）

**已实现 API**：
```cpp
class GantrySystem : public IGantryStateQuery, public IGantryEventPublisher {
public:
    GantrySystem(PhysicalAxis x1, PhysicalAxis x2);

    // ── 模式管理 ──
    GantryMode mode() const override;
    CouplingCondition::Result requestCoupling();
    void requestDecoupling(const std::string& reason = "");

    // ── 操作可行性 (约束18+19) ──
    Operability checkOperability(AxisId target, MotionDirection dir) const;

    // ── 运动命令 ──
    CommandResult jog(AxisId target, MotionDirection dir);
    CommandResult moveAbsolute(AxisId target, double pos);
    CommandResult moveRelative(AxisId target, double delta);
    CommandResult stop(AxisId target);  // 始终可接受

    // ── 状态聚合 (约束20) ──
    void aggregateState();
    bool checkSyncMaintenance();  // 约束14

    // ── 查询 ──
    PhysicalAxis& x1(); PhysicalAxis& x2();
    LogicalAxis& logical();
    const PhysicalAxis& x1() const;
    const PhysicalAxis& x2() const;
    const LogicalAxis& logical() const;

    // ── 事件管理 ──
    std::vector<GantryEvents::Event> drainEvents();
    const std::vector<GantryEvents::Event>& events() const;

    // ── IGantryStateQuery 实现 ──
    bool isCoupled() const override;
    AxisState aggregatedState() const override;
    double position() const override;
    double x1Position() const override;
    double x2Position() const override;
    bool x1Enabled() const override;
    bool x2Enabled() const override;
    bool isAnyAlarm() const override;
    bool isAnyLimit() const override;
    bool canAcceptCommand() const override;
    bool isTargetOperable(AxisId target) const override;
    std::string stateDescription() const override;
};
```

**已实现的核心逻辑**：
- 模式状态机：`Decoupled ↔ Coupled`（含故障自动退出）
- 操作互斥：`Coupled → 只允许 X`，`Decoupled → 只允许 X1/X2`
- 安全预检：报警优先、限位方向矩阵、Move 命令限位无差别拒绝
- 命令槽互斥：Jog 可覆盖 Jog，其他类型互斥
- 状态聚合：优先级 `Alarm > Limit > Moving > Idle`
- 边沿事件检测：限位/报警边沿触发，持续状态不重复发布
- 联动维持：每周期检查位置偏差，超限自动 Decoupled + DeviationFault 事件

**模式状态机**：
```
Decoupled ──CouplingCondition::checkAll() 通过──▶ Coupled
    ▲                                                   │
    │  requestDecoupling()                               │
    │  命令触发退出                                      │
    │                                                   │
    │  CouplingCondition::checkPositionOnly() 失败       │
    │  → DeviationFault 事件 + 自动 Decoupled             │
    ◀────────────────────────────────────────────────────┘
```

**状态**：✅ 已完成

---

## 五、值对象详解

### 5.1 MotionDirection（方向值对象）

**文件**：`domain/value/MotionDirection.h`  
**测试**：`tests/domain/gantry/test_gantry_direction.cpp`（19 个用例）

**状态**：✅ 已完成

### 5.2 PositionConsistency（位置一致性值对象）

**文件**：`domain/value/PositionConsistency.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（共享）

**状态**：✅ 已完成

### 5.3 CouplingCondition（联动条件值对象）

**文件**：`domain/value/CouplingCondition.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（共享）

**状态**：✅ 已完成

### 5.4 SafetyCheckResult（安全检查结果值对象）

**文件**：`domain/value/SafetyCheckResult.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（共享）

**状态**：✅ 已完成

### 5.5 Operability（操作可行性枚举）

**文件**：`domain/value/Operability.h`  
**测试**：`tests/domain/gantry/test_operability.cpp`

```cpp
// GantryValue 命名空间下
enum class Operability : uint8_t {
    Allowed,                    // 操作允许
    TargetNotOperableInMode,    // 模式不匹配 (约束18)
    AlarmActive,                // 报警激活 (约束17)
    LimitTriggered,             // 限位触发 (约束15)
    LimitBlocksDirection,       // 该方向被限位阻止 (约束16)
    NotEnabled,                 // 轴未使能
    CommandSlotBusy,            // 命令槽忙 (约束19)
    NotIdle,                    // 轴非空闲
    DeviationExceeded,          // 同步偏差超限
};
```

> 注：`GantrySystem.h` 内有并行共存的旧版 `Operability` 枚举，将在后续重构中统一迁移到 `GantryValue::Operability`。

**状态**：✅ 已完成

### 5.6 CommandResult（命令结果结构体）

**文件**：`domain/value/CommandResult.h`  
**测试**：`tests/domain/gantry/test_command_result.cpp`

```cpp
struct CommandResult {
    bool accepted = false;
    std::string rejectReason;
    GantryEvents::Event event;
    // 便捷工厂方法：
    static CommandResult accept();
    static CommandResult reject(Operability r, const std::string& d);
};
```

> 注：`GantrySystem.h` 内亦有同名 `CommandResult` 结构体，将在后续统一。

**状态**：✅ 已完成

### 5.7 PhysicalAxisState（物理轴状态快照 DTO）

**文件**：`domain/entity/PhysicalAxis.h`（内联于 `PhysicalAxis` 类前）

```cpp
struct PhysicalAxisState {
    bool enabled = false;
    bool alarmed = false;
    bool posLimitActive = false;
    bool negLimitActive = false;
    double position = 0.0;
};
```

**测试**：`tests/domain/gantry/test_physical_axis_state.cpp`  
**状态**：✅ 已完成

---

## 六、端口接口设计

| 端口 | 文件 | 用途 | 实现者 | 状态 |
|------|------|------|--------|:----:|
| `IGantryStateQuery` | `domain/port/IGantryStateQuery.h` | 只读状态查询 | `GantrySystem` | ✅ |
| `IGantryEventPublisher` | `domain/port/IGantryEventPublisher.h` | 事件发布 | `GantrySystem` | ✅ |
| `IGantryFeedbackPort` | `domain/port/IGantryFeedbackPort.h` | 硬件反馈读取 | `FakeGantryFeedbackPort` | ✅ |
| `IGantryCommandPort` | `domain/port/IGantryCommandPort.h` | 命令下发 | `FakeGantryCommandPort` | ✅ |
| `IGantryEventBus` | `domain/port/IGantryEventBus.h` | 事件分发 | `FakeGantryEventBus` | ✅ |

### IGantryStateQuery（状态查询端口）

```cpp
class IGantryStateQuery {
public:
    virtual GantryMode mode() const = 0;
    virtual AxisState aggregatedState() const = 0;
    virtual bool isCoupled() const = 0;
    virtual double position() const = 0;
    virtual double x1Position() const = 0;
    virtual double x2Position() const = 0;
    virtual bool x1Enabled() const = 0;
    virtual bool x2Enabled() const = 0;
    virtual bool isAnyAlarm() const = 0;
    virtual bool isAnyLimit() const = 0;
    virtual bool canAcceptCommand() const = 0;
    virtual bool isTargetOperable(AxisId target) const = 0;
    virtual std::string stateDescription() const = 0;
};
```

**实现**：`GantrySystem` 内联实现了此接口的全部方法。  
**使用方**：`Presentation ViewModel`、`Application 层 Orchestrator`。

---

## 七、基础设施 Fake 实现

| 组件 | 文件 | 用途 | 测试 | 状态 |
|------|------|------|------|:----:|
| `FakePLC` | `infrastructure/FakePLC.h` | 仿真 PLC 存储器 | `test_fake_plc.cpp` | ✅ |
| `FakeGantryFeedbackPort` | `infrastructure/FakeGantryFeedbackPort.h` | 仿真反馈端口 | `test_fake_gantry_feedback_port.cpp` | ✅ |
| `FakeGantryCommandPort` | `infrastructure/FakeGantryCommandPort.h` | 仿真命令端口 | `test_fake_gantry_command_port.cpp` | ✅ |
| `FakeGantryEventBus` | `infrastructure/FakeGantryEventBus.h` | 仿真事件总线 | `test_fake_gantry_event_bus.cpp` | ✅ |
| `FakeAxisDriver` | `infrastructure/FakeAxisDriver.h` | 仿真轴驱动 | — | ✅ |

---

## 八、TDD 实施总览

### 8.1 已完成阶段

| Phase | 阶段名称 | 交付物 | 测试数 | 状态 |
|:-----:|---------|--------|:------:|:----:|
| 0 | 实体基础 | `Axis` 实体 + `AxisState` 状态机 | 19 | ✅ |
| 1 | 方向语义 | `MotionDirection` 值对象 | 12+ | ✅ |
| 2 | 模式语义 | `GantryMode` 枚举 + 验证 | 18 | ✅ |
| 3 | 位置语义 | `GantryPosition` + 计算 | 19 | ✅ |
| 4 | 单一领域服务 | `GantryCouplingService`、`GantrySafetyService` | ~16 | ✅ |
| 5 | (合并至 6) | — | — | — |
| 6 | 同步与安全 | `PositionConsistency` + `CouplingCondition` + `SafetyCheckResult` | 60 | ✅ |
| 7 | 实体层 | `PhysicalAxis` + `LogicalAxis` + `GantrySystem` 聚合根 | 68+ | ✅ |
| — | 值对象扩展 | `Operability` + `CommandResult` + `PhysicalAxisState` | 8+ | ✅ |
| — | Fake 基础设施 | 4 个 Fake 组件 | 22+ | ✅ |
| — | 集成测试 | 全流程 + 聚合根集成 | 19 | ✅ |
| **合计** | | | **~260+** | **✅** |

### 8.2 待开发阶段

| Phase | 阶段名称 | 预计测试数 | 优先级 |
|:-----:|---------|:---------:|:------:|
| 8 | `GantryDomainService` 领域服务 | ~30 | P1 |
| 9 | 端口重构 + `IGantryCommandPort`/`IGantryFeedbackPort` 整合到 `GantryDomainService` | ~15 | P1 |
| 10 | `GantryOrchestrator` Application 层编排器 | ~10 | P2 |
| 11 | 状态持久化 + Repository 模式 | ~10 | P2 |
| 12 | 多龙门组扩展 (`GantryGroup`) | ~15 | P3 |

**预计最终测试总数**：~340+

### 8.3 当前测试目录结构

```
tests/
├── CMakeLists.txt
├── domain/
│   ├── test_axis.cpp                          # Axis 实体
│   ├── test_system_context.cpp                # SystemContext
│   └── gantry/
│       ├── test_gantry_mode.cpp               # 模式语义
│       ├── test_gantry_direction.cpp           # 方向语义
│       ├── test_gantry_position.cpp            # 位置语义
│       ├── test_gantry_sync.cpp                # 同步与安全 (值对象)
│       ├── test_gantry_coupling.cpp            # 联动条件 (旧)
│       ├── test_gantry_coupling_service.cpp    # 联动服务
│       ├── test_gantry_safety.cpp              # 安全 (旧)
│       ├── test_gantry_safety_service.cpp      # 安全服务
│       ├── test_gantry_activation.cpp          # 激活 (旧, 已拆分为新文件)
│       ├── test_gantry_system_mode.cpp         # ⭐ GantrySystem 模式管理
│       ├── test_gantry_system_operability.cpp  # ⭐ 操作可行性
│       ├── test_gantry_system_motion.cpp       # ⭐ 运动命令
│       ├── test_gantry_system_aggregation.cpp  # ⭐ 状态聚合
│       ├── test_gantry_system_integration.cpp  # ⭐ 聚合根集成
│       ├── test_gantry_full_integration.cpp    # ⭐ 全流程集成
│       ├── test_physical_axis_state.cpp        # PhysicalAxisState DTO
│       ├── test_physical_axis_sync.cpp         # PhysicalAxis::syncState()
│       ├── test_operability.cpp                # Operability 枚举
│       └── test_command_result.cpp             # CommandResult 结构体
├── application/
│   └── ...                                     # Application 层测试
├── infrastructure/
│   ├── test_fake_gantry_feedback_port.cpp
│   ├── test_fake_gantry_command_port.cpp
│   ├── test_fake_gantry_event_bus.cpp
│   ├── test_fake_plc.cpp
│   └── test_system_integration.cpp
└── presentation/
    └── ...
```

> ⭐ 标注 = Phase 7 完成后新增的核心测试文件

---

## 九、下一步开发计划

### 9.1 Phase 8：GantryDomainService（P1）

**文件**：`domain/service/GantryDomainService.h`（新建）

**职责**：接收命令 → 执行 5 步校验流水线 → 编排命令拆分 → 返回结果

**5 步校验流水线**：
```
1. 模式检查 (约束18) → GantrySystem::checkOperability()
2. 安全预检 (约束15–17) → SafetyCheckResult::checkMotionSafety()
3. 命令槽检查 (约束19) → LogicalAxis::canAcceptCommand()
4. 命令拆分 → Coupled 下镜像为 X1/X2，Decoupled 下直达
5. 返回 CommandResult
```

**依赖**：`GantrySystem` 聚合根（已有） + `IGantryCommandPort`（已有）

**关键差异 vs 当前实现**：
- 当前 `GantrySystem` 直接实现了 `jog()` / `moveAbsolute()` 等方法
- Phase 8 将把命令拆分逻辑抽取到 `GantryDomainService`，聚合根只做校验
- `GantryDomainService.monitorCycle()` 负责每周期从 `IGantryFeedbackPort` 拉取状态 → `syncState()` → `aggregateState()`

### 9.2 Phase 9：端口重构（P1）

- 将 `GantryDomainService` 与 `IGantryCommandPort` / `IGantryFeedbackPort` 绑定
- 消除 `GantrySystem.h` 与 `domain/value/Operability.h` / `CommandResult.h` 的重复定义
- 统一迁移到 `GantryValue` 命名空间下的版本

### 9.3 Phase 10：Application Orchestrator（P2）

**文件**：`application/policy/GantryOrchestrator.h`（草稿中已有 `AutoAbsMoveOrchestrator`、`AutoRelMoveOrchestrator`、`JogOrchestrator`）

**职责**：编排 Domain 层 + Infrastructure 层的完整运动流程

### 9.4 架构扩展方向

- **多龙门支持**：将 `GantrySystem` 扩展为 `GantryGroup`，管理多对轴
- **轨迹规划**：在 Application 层加入前瞻算法
- **持久化**：引入 Repository 模式

---

## 十、设计原则回顾

> *"Domain 层只回答'能不能动'，不回答'怎么动'。*  
> *所有物理知识属于 HAL 层，所有业务规则属于 Domain 层。"*

### 10.1 已实现的关键架构决策

| 决策 | 实现位置 | 说明 |
|------|---------|------|
| Domain 不依赖 Infrastructure | `GantrySystem` 通过接口 `IGantryStateQuery` 暴露状态 | Presentation 和基础设施仅依赖接口 |
| 聚合根持有状态变更 | `GantrySystem` 内部管理所有 PhysicalAxis + LogicalAxis | 单一数据源，避免状态不一致 |
| 命令槽互斥 | `LogicalAxis::CommandSlot` 使用 `variant` 风格 | 同一时刻只有一个活跃命令 |
| 事件边沿触发 | `aggregateState()` 检查 `m_prev*` 状态 | 避免重复发布相同事件 |
| 命令拆分委托 | (计划 Phase 8 实现) | 耦合 `X 命令 → X1/X2 镜像` 在 DomainService |

### 10.2 相对原始设计的变更

| 原始设计 | 实际实现 | 原因 |
|---------|---------|------|
| 单一 `GantryAxis` 聚合根 | 拆分为 `PhysicalAxis` + `LogicalAxis` + `GantrySystem` | 更清晰分离物理和逻辑职责 |
| `GantryDomainService` 是唯一编排器 | `GantrySystem` 已实现大部分命令校验逻辑 | 减少间接层次，聚合根自洽 |
| `Phase 7` 预计 16 个测试 | Phase 7 实际完成 68+ 个测试 | TDD 过程中发现更多边界场景 |
| 预计总测试 189 个 | 已完成 ~260+ 个 | 测试覆盖更加全面 |

---

> 📌 **当前里程碑**  
> Phase 0–7 全部实现完毕，Domain 层核心逻辑（聚合根、实体、值对象、端口、事件、Fake 基础设施）均已完成。  
> **下一步**：Phase 8 `GantryDomainService` → Phase 9 端口重构 → Phase 10 Application Orchestrator。  
> **当前测试总数**：~260+ 个测试用例，覆盖 20 条业务约束中的全部。
