# Domain 层 TDD 架构设计文档  
## —— 双轴龙门同步系统

> **版本**：v1.0  
> **状态**：已实现 Phase 0–6，Phase 7+ 设计中  
> **业务约束来源**：[双轴龙门结构的业务语义约束（第7阶段）](./第7阶段：双轴同步龙门轴/双轴龙门结构的业务约束.md)

---

## 一、概述

### 1.1 设计目标

将“控制电机”升级为“定义运动语义系统”。Domain 层仅负责“是否允许操作”，不负责“如何执行”。所有物理差异（方向、坐标）由 HAL 层消化。

### 1.2 核心语义真理

1. **X 是唯一对外暴露的逻辑轴**  
2. **X1/X2 是实现细节，不参与业务决策**  
3. **所有操作基于统一逻辑方向（Forward/Backward）**  
4. **联动是状态，不是命令**  
5. **分动与联动互斥**  
6. **联动建立必须满足位置一致性**  
7. **运行过程中必须持续满足同步约束**  
8. **限位与报警优先级高于一切运动**  
9. **Domain 负责“是否允许操作”，不负责“如何执行”**  
10. **所有物理差异由 HAL 层消化**

### 1.3 分层架构

```
┌─────────────────────────────┐
│     Application / UI        │  UseCase 层，编排业务流程
├─────────────────────────────┤
│      Domain 层 (本层)        │  实体、值对象、领域服务、端口
├─────────────────────────────┤
│   Infrastructure / HAL      │  驱动实现、PLC 抽象
└─────────────────────────────┘
```

---

## 二、领域组件全景图

### 2.1 组件分类与文件清单

| 类型 | 组件名 | 文件路径 | 状态 |
|------|--------|----------|------|
| **实体** | `Axis` | `domain/entity/Axis.h` | ✅ 已实现 |
| **实体** | `PhysicalAxis` | `domain/entity/PhysicalAxis.h` | ⬜ Phase 7 |
| **实体** | `LogicalAxis` | `domain/entity/LogicalAxis.h` | ⬜ Phase 7 |
| **实体 (聚合根)** | `GantrySystem` | `domain/entity/GantrySystem.h` | ⬜ Phase 7 |
| **领域事件** | `GantryEvents` | `domain/event/GantryEvents.h` | ⬜ Phase 7 |
| **值对象** | `MotionDirection` | `domain/value/MotionDirection.h` | ✅ Phase 1 |
| **值对象** | `PositionConsistency` | `domain/value/PositionConsistency.h` | ✅ Phase 6 |
| **值对象** | `CouplingCondition` | `domain/value/CouplingCondition.h` | ✅ Phase 6 |
| **值对象** | `SafetyCheckResult` | `domain/value/SafetyCheckResult.h` | ✅ Phase 6 |
| **领域服务** | `GantryDomainService` | `domain/service/GantryDomainService.h` | ⬜ Phase 8 |
| **端口接口** | `IGantryStatePort` | `domain/port/IGantryStatePort.h` | ⬜ Phase 9 |

### 2.2 组件关系图（UML 风格）

```
┌──────────────────────────────────────────────────────────────┐
│                     GantrySystem (聚合根)                      │
│  mode: GantryMode    checkOperability()  aggregateState()    │
│  requestCoupling()   requestDecoupling()                     │
├──────┬──────┬──────────┬────────────┬────────────────────────┤
│      │      │          │            │                        │
│ 拥有 │ 拥有 │ 持有     │ 调用       │ 发布                   │
│      │      │          │            │                        │
▼      ▼      ▼          ▼            ▼
PhysicalAxis (×2)   LogicalAxis   CouplingCondition   GantryEvents
├─ position()        ├─ position()   ├─ checkAll()       ├─ Coupled
├─ isEnabled()       ├─ tryAcceptJog()├─ checkPosition()  ├─ Decoupled
├─ isAlarmed()       ├─ canAccept-   ├─ Result           ├─ Deviation-
├─ isLimit()         │  Command()    └───────────────────┤  Fault
├─ syncState()       └─ applyAggre-                     ├─ LimitTriggered
└─ snapshot()           gatedState()                    ├─ AlarmRaised
                                                        └─ CommandRejected
        │
        │  计算时引用
        ▼
  ┌─────────────────┐  ┌──────────────────┐  ┌──────────────────┐
  │ Position-       │  │ MotionDirection  │  │ SafetyCheck-     │
  │ Consistency     │  │                  │  │ Result           │
  │ ├─ compute-     │  │ Forward/Backward │  │ ├─ checkMotion-  │
  │ │  Deviation()  │  │ opposite()       │  │ │  Safety()       │
  │ ├─ isConsistent │  │ isForward()      │  │ ├─ Verdict        │
  │ ├─ computeLogical│ └─ isBackward()    │  │ └─ isAllowed()    │
  │ │  Position()   │                    │  └──────────────────┘
  │ └─ describe-    │                    │
  │    Deviation()  │                    │
  └─────────────────┘                    │
                                         │
          ┌──────────────────────────────┘
          │  GantryDomainService (Phase 8)
          │  命令校验流水线：模式 → 安全 → 命令槽 → 拆分 → 下发
          │
          │  IGantryStatePort (Phase 9) ─── HAL 层
```

---

## 三、业务约束 → 代码 → 测试 全映射表

| 约束编号 | 约束内容 | 实现组件 | 核心 API / 逻辑 | 测试文件 | 测试数量 |
|----------|---------|---------|----------------|---------|---------|
| 1–4 | 模式定义与互斥 (Coupled ⊕ Decoupled) | `GantryMode` | 枚举值、GantryAxis 状态切换 | `test_gantry_mode.cpp` | 18 |
| 5–7 | 方向唯一性、逻辑方向禁止物理方向 | `MotionDirection` | `isForward()`, `isBackward()`, `oppositeDirection()` | `test_gantry_direction.cpp` | 12 |
| 8 | 统一位置定义 X.position | `PositionConsistency` | `computeLogicalPosition(x1Pos)` | `test_gantry_sync.cpp` | 3 |
| 9 | 镜像关系 X1.pos ≈ -X2.pos | `PositionConsistency` | `computeDeviation()`, `expectedX1FromX2()` | `test_gantry_sync.cpp` | 8 |
| 10 | 逻辑位置计算 X.position = X1.pos | `PositionConsistency` | `computeLogicalPosition()` | `test_gantry_sync.cpp` | 3 |
| 11 | 位置一致性失效判定 | `PositionConsistency` | `isConsistent()`, `describeDeviation()` | `test_gantry_sync.cpp` | 5 |
| 12–13 | 联动建立条件（使能、无报警、无限位、位置一致） | `CouplingCondition` | `checkAll()` 完整检查 / `checkPositionOnly()` 持续检查 | `test_gantry_sync.cpp` | 12 |
| 14 | 联动期间持续同步约束 | `CouplingCondition` | `checkPositionOnly()` | `test_gantry_sync.cpp` | 3 |
| 15–16 | 限位优先级最高 + 限位后方向限制 | `SafetyCheckResult` | `checkMotionSafety()` 方向安全检查矩阵 | `test_gantry_sync.cpp` | 20 |
| 17 | 报警状态下禁止所有运动 | `SafetyCheckResult` | `checkMotionSafety()` 报警优先检查 | `test_gantry_sync.cpp` | 3 |
| 18 | 操作对象互斥 (Couped→X, Decoupled→X1/X2) | `GantrySystem` + `LogicalAxis` | `checkOperability()` + `isTargetOperable()` | `test_gantry_activation.cpp` | 3 |
| 19 | 运动互斥 (Jog ⊕ MoveAbsolute ⊕ MoveRelative) | `LogicalAxis` | `canAcceptCommand()` + `tryAccept*()` 命令槽 | `test_gantry_activation.cpp` | 3 |
| 20 | 状态一致性 (聚合语义) | `GantrySystem` | `aggregateState()` + 优先级 Alarm > Limit > Moving > Idle | `test_gantry_activation.cpp` | 7 |

> **当前覆盖率**：已实现 120+ 测试用例，覆盖约束 1–17，待实现 18–20。

---

## 四、实体详解

### 4.1 Axis（单轴实体）

**职责**：封装单个物理轴的全部运行时状态与命令槽。

**特点**：
- 持有状态：`AxisState`（Unknown → Disabled → Idle → Jogging → MovingAbsolute → MovingRelative → Error）
- 持有反馈镜像：位置、限位状态、限位值
- 持有统一命令槽：`variant<JogCommand, MoveCommand, ...>`
- **不感知龙门语义**，完全按单轴逻辑运作

**状态转换图**：
```
        Disabled ←──→ Idle
           │           │
           └───────────┘
           enable(true/false)
```

**关键 API**：
```cpp
bool jog(Direction dir);
bool moveAbsolute(double target);
void applyFeedback(const AxisFeedback& feedback);
```

**当前状态**：✅ 已实现，19 个测试全部通过（`tests/domain/test_axis.cpp`）

---

### 4.2 Phase 7 实体层概述

Phase 7 将设计文档中的单一聚合根 `GantryAxis` 细化为**三个实体**，以精准映射业务语义中
"X 是唯一对外暴露的逻辑轴，X1/X2 是实现细节" 的核心原则。

| 实体 | 头文件 | 职责 | 约束映射 |
|------|--------|------|---------|
| `PhysicalAxis` | `domain/entity/PhysicalAxis.h` | 物理执行单元 X1/X2 的状态同步镜像 | 约束 9（镜像关系） |
| `LogicalAxis` | `domain/entity/LogicalAxis.h` | 逻辑轴 X = 龙门在 Coupled 模式下的唯一操作界面 | 约束 8、10（位置定义） |
| `GantrySystem` | `domain/entity/GantrySystem.h` | 聚合根：模式管理 + 操作目标互斥 + 运动互斥 + 状态聚合 | 约束 1–4、12–14、18–20 |

---

### 4.3 PhysicalAxis（物理轴实体）—— 待实现

**职责**：
- 封装单个物理执行单元（X1 或 X2）
- 持有身份标记 `AxisId`（X1 或 X2）
- 作为 HAL 层位置反馈的镜像存储
- 为 `GantrySystem` 提供原始数据供安全检查与位置一致性计算

**不负责**：
- 不持有命令槽（命令由 `LogicalAxis` + `GantrySystem` 统一管理）
- 不执行耦合/解耦逻辑
- 不判断操作合法性

**核心 API 草案**：

```cpp
struct PhysicalAxisState {
    bool enabled;
    bool alarmed;
    bool posLimitActive;
    bool negLimitActive;
    double position;
};

class PhysicalAxis {
public:
    explicit PhysicalAxis(AxisId id);
    AxisId id() const;

    // 状态查询（只读，由外部通过 HAL 反馈来同步更新）
    bool isEnabled() const;
    bool isAlarmed() const;
    bool isPosLimitActive() const;
    bool isNegLimitActive() const;
    double position() const;             // 物理位置 (mm)，符号与 X 轴同向

    // 快照导出（一次性导出全部状态，避免多次调用时状态不一致）
    PhysicalAxisState snapshot() const;

    // 状态同步（由 HAL 反馈定期调用）
    void syncState(const PhysicalAxisState& state);
};
```

**关键约束映射**：
| 方法 | 覆盖约束 |
|------|---------|
| `position()` | 约束 9（镜像关系数据源） |
| `syncState()` | 约束 9、11（位置一致性数据入口） |
| `isEnabled() / isAlarmed()` | 约束 13（联动建立条件） |

---

### 4.4 LogicalAxis（逻辑轴实体）—— 待实现

**职责**：
- 表示龙门**逻辑整体** X
- 持有聚合后的逻辑位置 `GantryPosition`
- 持有聚合后的状态（Moving / Idle / Alarm / Limit）
- 持有统一命令槽（约束 19：运动互斥）

**关键设计决策**：
- **不持有 PhysicalAxis 引用**：LogicalAxis 是"视图"，数据由 GantrySystem 在每次操作时
  通过聚合计算后注入
- **命令槽互斥**：同一时刻只能有一个活跃意图，新命令覆盖旧命令或返回 Busy

**核心 API 草案**：

```cpp
class LogicalAxis {
public:
    LogicalAxis();

    // ═══════════════════════════════════
    // 位置
    // ═══════════════════════════════════
    GantryPosition position() const { return m_position; }
    void setPosition(GantryPosition pos);     // 由 GantrySystem 计算后注入

    // ═══════════════════════════════════
    // 聚合状态 (约束 20)
    // ═══════════════════════════════════
    AxisState aggregatedState() const { return m_aggregatedState; }

    enum class AggregatedMotion {
        Idle,
        Jogging,
        MovingAbsolute,
        MovingRelative
    };
    AggregatedMotion motion() const { return m_motion; }
    bool isMoving() const { return m_motion != AggregatedMotion::Idle; }

    // ═══════════════════════════════════
    // 状态注入（由 GantrySystem 每周期聚合后调用）
    // ═══════════════════════════════════
    void applyAggregatedState(
        AxisState state,
        AggregatedMotion motion,
        GantryPosition pos,
        bool anyLimit
    );

    // ═══════════════════════════════════
    // 命令槽 (约束 19：运动互斥)
    // ═══════════════════════════════════
    enum class CommandType { None, Jog, MoveAbsolute, MoveRelative, Stop };

    struct CommandSlot {
        CommandType type = CommandType::None;
        MotionDirection jogDirection = MotionDirection::Forward;
        double moveTarget = 0.0;       // MoveAbsolute 用
        double moveDelta = 0.0;        // MoveRelative 用
    };

    bool canAcceptCommand() const;           // 是否有活跃命令
    RejectionReason tryAcceptJog(MotionDirection dir);
    RejectionReason tryAcceptMoveAbsolute(double target);
    RejectionReason tryAcceptMoveRelative(double delta);
    RejectionReason tryAcceptStop();
    void clearCommand();                     // 命令完成/中断后清除
    const CommandSlot& pendingCommand() const;
};
```

**状态聚合规则** (约束 20)：

| X1 状态 | X2 状态 | X 聚合状态 | 聚合运动 |
|---------|---------|-----------|---------|
| Idle | Idle | Idle | Idle |
| Jogging / Moving* | 任意 | 同 X1 状态 | 同 X1 运动 |
| 任意 | Jogging / Moving* | 同 X2 状态 | 同 X2 运动 |
| Error | 任意（非 Error） | Error | Idle |
| 任意（非 Error） | Error | Error | Idle |
| Limit 活跃 | Limit 活跃 | Idle（限位阻断运动） | Idle |

**优先级**：Alarm > Limit > Moving > Idle  
即：只要任一轴报警，X 就是 Error；无报警时任一轴限位，X 标记为限位阻断；否则按运动状态取或。

---

### 4.5 GantrySystem（龙门系统聚合根）—— 待实现

**职责**：
- 管理 `GantryMode`（Coupled / Decoupled）状态机
- 持有 2 个 `PhysicalAxis`（X1, X2）和 1 个 `LogicalAxis`（X）
- 执行联动建立条件校验（约束 13）和联动维持校验（约束 14）
- 在每次操作时执行**操作目标互斥**（约束 18）：Coupled 模式只允许操作 X，Decoupled 只允许
  操作 X1 或 X2
- 在每次操作时执行**运动互斥**（约束 19）：检查命令槽状态
- 每周期执行**状态聚合**（约束 20）：将 X1/X2 状态合并为 X 的聚合状态
- 发布领域事件（`GantryEvents`）通知外部模式变更、同步偏差等

**模式状态机**：

```
                      CouplingCondition::checkAll() 通过
   Decoupled ────────────────────────────────────────────▶ Coupled
       ▲                                                      │
       │  requestDecouple()                                   │
       │  命令触发退出                                         │
       │                                                      │
       │  CouplingCondition::checkPositionOnly() 失败          │
       │  (→ DeviationFault 事件)                              │
       ◀──────────────────────────────────────────────────────┘
```

**核心 API 草案**：

```cpp
class GantrySystem {
public:
    GantrySystem(PhysicalAxis x1, PhysicalAxis x2);

    // ═══════════════════════════════════
    // 模式管理
    // ═══════════════════════════════════
    GantryMode mode() const { return m_mode; }

    // 联动建立申请 (约束 12–13)
    // 返回联动条件检查结果，同时发布 CouplingRequested 事件
    CouplingCondition::Result requestCoupling();

    // 分动申请 (约束 4)
    void requestDecoupling();

    // ═══════════════════════════════════
    // 操作目标互斥检查 (约束 18)
    // ═══════════════════════════════════
    enum class Operability {
        Allowed,
        Rejected_Mode,      // 当前模式下不可操作该目标
        Rejected_Alarm,     // 报警状态
        Rejected_Limit,     // 限位状态 + 方向禁止
        Rejected_Busy       // 命令槽已被占用
    };

    Operability checkOperability(AxisId target) const;

    // Coupled 模式：只允许 AxisId::X
    // Decoupled 模式：只允许 AxisId::X1 或 AxisId::X2
    bool isTargetOperable(AxisId target) const;

    // ═══════════════════════════════════
    // 运动命令编排 (约束 19：命令槽互斥)
    // ═══════════════════════════════════
    struct CommandResult {
        bool accepted;
        std::string rejectReason;
    };

    // Coupled: 命令写入 LogicalAxis X 的命令槽
    // Decoupled: 命令写入对应 PhysicalAxis 的活跃标记
    CommandResult jog(AxisId target, MotionDirection dir);
    CommandResult moveAbsolute(AxisId target, double pos);
    CommandResult moveRelative(AxisId target, double delta);
    CommandResult stop(AxisId target);

    // ═══════════════════════════════════
    // 状态聚合 (约束 20)
    // ═══════════════════════════════════
    void aggregateState();   // 应由定时循环 (每 PLC 扫描周期) 调用

    // ═══════════════════════════════════
    // 联动维持检查 (约束 14)
    // ═══════════════════════════════════
    // 每周期 aggregateState() 中自动调用
    // 若位置偏差超出阈值，触发 DeviationFault 事件 + 自动退出 Coupled
    bool checkSyncMaintenance() const;

    // ═══════════════════════════════════
    // 查询 (零副作用)
    // ═══════════════════════════════════
    PhysicalAxis& x1() { return m_x1; }
    PhysicalAxis& x2() { return m_x2; }
    const LogicalAxis& logical() const { return m_logical; }
    const PhysicalAxis& x1() const { return m_x1; }
    const PhysicalAxis& x2() const { return m_x2; }

    // ═══════════════════════════════════
    // 事件订阅 (用于测试与 Application 层通知)
    // ═══════════════════════════════════
    // （见 GantryEvents 设计）
};
```

---

### 4.6 GantryEvents（领域事件）—— 待实现

**职责**：定义龙门系统内所有领域事件，供 Application 层订阅和日志/诊断。

**事件类型枚举**：

```cpp
// domain/event/GantryEvents.h
namespace GantryEvents {

enum class Type {
    None,

    // 模式变更
    CouplingRequested,    // 联动建立申请已发起
    Coupled,              // 联动建立成功
    Decoupled,            // 联动退出（主动/被动）

    // 异常
    DeviationFault,       // 同步偏差超限，联动已被强制退出
    LimitTriggered,       // 限位触发
    AlarmRaised,          // 报警发生

    // 命令
    CommandRejected,      // 命令被拒（含拒绝原因）
};

struct Event {
    Type type;
    std::string description;
    // 可选附加数据（根据 type 不同）
};

}
```

**事件流**：

```
requestCoupling()
    ├─ checkAll() 通过 → Coupled 事件发布
    └─ checkAll() 失败 → (不发布事件，仅返回 Result)

aggregateState() (每周期)
    ├─ 检测到限位 → LimitTriggered 事件
    ├─ 检测到报警 → AlarmRaised 事件
    └─ Coupled 模式下 checkSyncMaintenance() 失败
       → DeviationFault 事件 + 自动 requestDecoupling()

jog() / moveAbsolute() / moveRelative()
    └─ 目标不可操作 / 命令槽忙 / 安全检查失败
       → CommandRejected 事件 (含拒绝原因)
```

---

### 4.7 Phase 7 测试用例清单（TDD 红阶段规格）

#### 测试文件：`tests/domain/gantry/test_gantry_activation.cpp`

| TC 编号 | 约束 | 测试名 | Given | When | Then |
|---------|------|--------|-------|------|------|
| TC-6.1 | 18 | `CoupledMode_OnlyXIsOperable` | Coupled 模式 | 查询 X、X1、X2 可操作性 | X=true, X1=false, X2=false |
| TC-6.2 | 18 | `DecoupledMode_X1OrX2IsOperable` | Decoupled 模式 | 查询 X、X1、X2 可操作性 | X=false, X1=true, X2=true |
| TC-6.3 | 18 | `SimultaneousOperation_ShouldBeForbidden` | Coupled 模式，X 命令已占据槽 | 对 X1 下发命令 | 被拒绝 |
| TC-6.4 | 19 | `MotionExclusive_JogAndMove` | X 正在 Jog | 下发 MoveAbsolute | 被拒绝 (Busy) |
| TC-6.5 | 19 | `MotionExclusive_MoveAbsoluteAndMoveRelative` | X 正在 MoveAbsolute | 下发 MoveRelative | 被拒绝 (Busy) |
| TC-6.6 | 19 | `OnlyOneMotionIntent_AtAnyTime` | X Idle | Jog(Fwd) → 立即 Jog(Bwd) | 后者成功 (覆盖停+新方向) |
| TC-6.7 | 20 | `StateAggregation_X1Moving_XShouldBeMoving` | X1=MovingAbsolute, X2=Idle | aggregateState() | X=MovingAbsolute |
| TC-6.8 | 20 | `StateAggregation_X2Alarm_XShouldBeAlarm` | X1=Idle, X2=Error | aggregateState() | X=Error |
| TC-6.9 | 20 | `StateAggregation_X1Limit_XShouldBeLimit` | X1.posLimit=true, X2=正常 | aggregateState() | X=Limit 阻断 |
| TC-6.10 | 20 | `StateAggregation_BothIdle_XShouldBeIdle` | X1=Idle, X2=Idle | aggregateState() | X=Idle |
| TC-6.11 | 20 | `StateAggregation_AlarmOverridesMoving` | X1=Moving, X2=Alarm | aggregateState() | X=Error |
| TC-6.12 | 20 | `StateAggregation_LimitOverridesMoving` | X1=Moving, X2=Limit | aggregateState() | X=Limit 阻断 |
| TC-6.13 | 20 | `StateAggregation_AlarmOverridesLimit` | X1=Limit, X2=Alarm | aggregateState() | X=Error |

#### 测试文件：`tests/domain/gantry/test_gantry_position.cpp`（扩展实体部分）

| TC 编号 | 约束 | 测试名 | Given | When | Then |
|---------|------|--------|-------|------|------|
| TC-3.2 | 9 | `PhysicalAxis_X1AndX2AreMirrored` | X1.pos=50.0, X2.pos=-50.0 | 计算偏差 | deviation=0 |
| TC-3.3 | 8,10 | `LogicalAxis_PositionEqualsX1` | X1.pos=100.0 | 设置 LogicalAxis 位置 | X.pos=100.0 |
| TC-3.4 | 9 | `PhysicalAxis_SyncState` | X1 初始(0,false) | syncState({enabled=true, pos=25.0}) | position=25.0, enabled=true |
| TC-3.5 | 11 | `PhysicalAxis_OutOfSync_Detected` | X1.pos=50.0, X2.pos=-49.95 | 计算一致性 | deviation=0.05 > epsilon 0.01 → inconsistent |

---

### 4.8 Phase 7 文件清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `domain/entity/PhysicalAxis.h` | 实体 | 物理轴 X1/X2（纯头文件，constexpr 可行） |
| `domain/entity/LogicalAxis.h` | 实体 | 逻辑轴 X（纯头文件） |
| `domain/entity/GantrySystem.h` | 聚合根 | 模式 + 互斥 + 聚合 + 事件 |
| `domain/event/GantryEvents.h` | 事件 | 领域事件枚举 + 事件结构体 |
| `tests/domain/gantry/test_gantry_activation.cpp` | 测试 | 约束 18-20（12 测试） |
| `tests/domain/gantry/test_gantry_position.cpp` (扩展) | 测试 | 约束 8-11 实体层面（4 新增测试） |

**预计测试增量**：16 个新测试用例，使 Phase 7 累计达到 144 个（128 + 16）。

---

## 五、值对象详解

### 5.1 MotionDirection（方向值对象）

**文件**：`domain/value/MotionDirection.h`  
**测试**：`tests/domain/gantry/test_gantry_direction.cpp`（19 个用例）

```cpp
enum class MotionDirection { Forward, Backward };

inline bool isForward(MotionDirection d);
inline bool isBackward(MotionDirection d);
inline MotionDirection oppositeDirection(MotionDirection d);
```

**设计要点**：
- 系统中唯一的方向语义定义
- 禁止任何地方使用 CW/CCW 或电机正反转
- `Forward` = 远离操作者，`Backward` = 靠近操作者

---

### 5.2 PositionConsistency（位置一致性值对象）

**文件**：`domain/value/PositionConsistency.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（19 个用例）

```cpp
class PositionConsistency {
public:
    static constexpr double kDefaultEpsilon = 0.01;  // 0.01 mm
    static constexpr double computeDeviation(double x1Pos, double x2Pos);
    static constexpr bool isConsistent(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon);
    static constexpr double computeLogicalPosition(double x1Pos);   // X.position = X1.pos
    static constexpr double expectedX1FromX2(double x2Pos);
    static std::string describeDeviation(double x1Pos, double x2Pos, double epsilon = kDefaultEpsilon);
};
```

**数学公式**：
- 偏差 `deviation = |X1.pos + X2.pos|`
- 一致性条件 `deviation ≤ epsilon`
- 逻辑位置 `X.position = X1.pos`

**测试覆盖**：
- 完美镜像偏差为 0
- 小/大失同步值正确
- epsilon 边界测试（含、恰好等于、超过）
- NaN 输入处理
- 极大值处理
- 诊断字符串生成

---

### 5.3 CouplingCondition（联动条件值对象）

**文件**：`domain/value/CouplingCondition.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（12 个用例）

```cpp
class CouplingCondition {
public:
    struct Result {
        bool allowed;
        std::string failReason;
        explicit operator bool() const;
    };

    static Result checkAll(
        bool x1Enabled, bool x2Enabled,
        bool anyAlarm, bool anyLimit,
        double x1Pos, double x2Pos,
        double epsilon = kDefaultEpsilon
    );

    static Result checkPositionOnly(
        double x1Pos, double x2Pos,
        double epsilon = kDefaultEpsilon
    );
};
```

**检查顺序**（短路逻辑）：
1. X1 未使能 → 立即拒绝
2. X2 未使能 → 立即拒绝（但 X1 优先）
3. 有报警 → 拒绝
4. 触发限位 → 拒绝
5. 位置不一致 → 拒绝

**诊断输出**：失败时 `failReason` 明确给出第一个失败原因

---

### 5.4 SafetyCheckResult（安全检查结果值对象）

**文件**：`domain/value/SafetyCheckResult.h`  
**测试**：`tests/domain/gantry/test_gantry_sync.cpp`（20 个用例）

```cpp
class SafetyCheckResult {
public:
    enum class Verdict {
        Allowed,
        Rejected_Alarm,
        Rejected_Limit,
        Rejected_LimitForward,
        Rejected_LimitBackward
    };
    
    static SafetyCheckResult allowed();
    static SafetyCheckResult rejectedDueToAlarm();
    static SafetyCheckResult rejectedDueToForwardLimit();
    static SafetyCheckResult rejectedDueToBackwardLimit();
    static SafetyCheckResult rejectedDueToLimit();  // 方向不明确时的兜底

    bool isAllowed() const;
    bool isRejected() const;
    Verdict verdict() const;
    explicit operator bool() const;  // true = Allowed
};

inline SafetyCheckResult checkMotionSafety(
    bool isAlarm,
    bool fwdLimitActive,
    bool bwdLimitActive,
    MotionDirection direction
);
```

**安全判断链**（优先级从高到低）：
1. 报警 → 全部拒绝（Alarm 覆盖所有限位判断）
2. 双向限位同时触发 → 按方向分别拒绝
3. 单向限位 → 仅拒绝朝向限位的方向，允许远离方向
4. 无报警无限位 → 全部允许

**方向安全矩阵**（12 种组合 12 个测试全部通过）：

| alarm | fwdLim | bwdLim | Forward | Backward |
|-------|--------|--------|---------|----------|
| false | false  | false  | ✅ Allowed | ✅ Allowed |
| true  | false  | false  | ❌ Alarm | ❌ Alarm |
| false | true   | false  | ❌ LimitForward | ✅ Allowed |
| false | false  | true   | ✅ Allowed | ❌ LimitBackward |
| false | true   | true   | ❌ LimitForward | ❌ LimitBackward |
| true  | true   | false  | ❌ Alarm | ❌ Alarm |
| true  | false  | true   | ❌ Alarm | ❌ Alarm |

---

## 六、领域服务设计（Phase 8，待实现）

### 6.1 GantryDomainService 职责边界

`GantryDomainService` 是 Domain 层的唯一"编排器"。它**不持有状态**，而是委托给
`GantrySystem` 聚合根。它的唯一职责是：**接收命令 → 执行校验流水线 → 编排命令拆分 → 返回结果**。

关键设计原则：
- **无状态服务**：每个方法都是纯函数，输入命令 + 当前状态 → 输出结果
- **委托模式**：所有业务规则检查委托给值对象和实体，服务只负责编排调用顺序
- **端口抽象**：通过 `IGantryStatePort` 获取状态，不直接依赖 HAL 具体实现

---

### 6.2 命令校验流水线（5 步）

```
Application Layer Command
          │
          ▼
┌──────────────────────────────────────┐
│ Step 1: 模式检查 (约束 18)            │
│   GantrySystem::checkOperability()   │
│   → 操作对象在当前模式下是否合法？     │
│   → Coupled 模式下 target 必须是 X    │
│   → Decoupled 模式下 target 是 X1/X2  │
├──────────────────────────────────────┤
│ Step 2: 安全预检 (约束 15–17)         │
│   SafetyCheckResult::checkMotionSafety│
│   → 报警？→ 全部拒绝                  │
│   → 限位？→ 按方向矩阵判断            │
├──────────────────────────────────────┤
│ Step 3: 命令槽检查 (约束 19)          │
│   LogicalAxis::canAcceptCommand()    │
│   → 当前是否有活跃命令？              │
│   → 无活跃命令 → 写入新命令           │
│   → 有活跃命令 → 返回 Busy            │
├──────────────────────────────────────┤
│ Step 4: 命令拆分 (约束 8–10)          │
│   Coupled 模式:                       │
│     X 的命令 → 镜像为 X1/X2 双命令    │
│     X1.pos 需要 = -X2.pos（反向发送） │
│   Decoupled 模式:                     │
│     命令直达指定 PhysicalAxis          │
├──────────────────────────────────────┤
│ Step 5: 返回结果                      │
│   → 成功: 命令已写入命令槽            │
│   → 失败: 返回拒绝原因 + 发布事件     │
└──────────────────────────────────────┘
```

---

### 6.3 核心 API 设计

```cpp
// domain/service/GantryDomainService.h
class GantryDomainService {
public:
    /*
     * 构造函数
     * @param system     GantrySystem 聚合根引用（外部持有）
     * @param statePort  状态端口（只读查询当前硬件状态）
     */
    GantryDomainService(GantrySystem& system, IGantryStatePort& statePort);

    // ═══════════════════════════════════════════
    // 运动命令入口
    // ═══════════════════════════════════════════
    
    /*
     * executeJog: 执行点动命令
     * 
     * 校验流程:
     *   1. checkOperability(target)     — 模式检查
     *   2. checkMotionSafety(...)       — 安全预检
     *   3. logicalAxis().tryAcceptJog() — 命令槽互斥
     *   4. 拆分命令                     — Coupled 下镜像到 X1/X2
     * 
     * @param target   操作目标（X / X1 / X2），必须与当前模式匹配
     * @param dir      Forward / Backward 逻辑方向
     * @return MotionResult 包含是否接受和拒绝原因
     */
    struct MotionResult {
        bool accepted;
        std::string rejectReason;
        GantryEvents::Type eventType = GantryEvents::Type::None;
    };

    MotionResult executeJog(AxisId target, MotionDirection dir);

    /*
     * executeMoveAbsolute: 执行绝对定位命令
     * 额外预检: TargetOutOfPositiveLimit / TargetOutOfNegativeLimit
     */
    MotionResult executeMoveAbsolute(AxisId target, double position);

    /*
     * executeMoveRelative: 执行相对定位命令
     * 额外预检: 目标位置 = 当前位置 + delta，检查是否超出软限位
     */
    MotionResult executeMoveRelative(AxisId target, double delta);

    /*
     * executeStop: 停止运动
     * 停止命令始终可以接受（不受安全/模式/命令槽限制）
     */
    MotionResult executeStop(AxisId target);

    // ═══════════════════════════════════════════
    // 模式切换命令入口
    // ═══════════════════════════════════════════

    /*
     * requestCoupling: 申请进入联动模式
     * 
     * 委托给 GantrySystem::requestCoupling()
     * 返回 CouplingCondition::Result (allowed + failReason)
     */
    CouplingCondition::Result requestCoupling();

    /*
     * requestDecoupling: 申请退出联动模式
     * 
     * 委托给 GantrySystem::requestDecoupling()
     * 退出前检查是否有活跃命令 → 自动 Stop 后退出
     */
    void requestDecoupling();

    // ═══════════════════════════════════════════
    // 周期监控入口
    // ═══════════════════════════════════════════

    /*
     * monitorCycle: 每个 PLC 扫描周期调用一次
     * 
     * 执行顺序:
     *   1. 从 IGantryStatePort 拉取最新硬件状态
     *   2. 同步到 GantrySystem 的 PhysicalAxis X1/X2
     *   3. 调用 GantrySystem::aggregateState()
     *      - 内部自动执行 checkSyncMaintenance()
     *      - 若偏差超限 → DeviationFault 事件 + 自动退出 Coupled
     *   4. 收集本轮产生的领域事件，返回给 Application 层
     */
    std::vector<GantryEvents::Event> monitorCycle();

    // ═══════════════════════════════════════════
    // 查询接口（透传给 GantrySystem）
    // ═══════════════════════════════════════════
    GantryMode currentMode() const;
    const GantryPosition& currentPosition() const;
};
```

---

### 6.4 MotionResult 详细语义

```cpp
struct MotionResult {
    bool accepted;           // true = 命令已写入命令槽, 等待 HAL 执行
    std::string rejectReason; // accepted=false 时的原因

    // 每个拒绝场景对应的拒绝原因常量（设计约定）:
    // "Mode: target not operable in current mode"    — 约束 18
    // "Safety: alarm is active"                       — 约束 17
    // "Safety: forward limit triggered"               — 约束 16
    // "Safety: backward limit triggered"              — 约束 16
    // "Slot: command slot is busy"                    — 约束 19
    // "Target: position out of positive soft limit"   — MoveAbsolute/Relative
    // "Target: position out of negative soft limit"   — MoveAbsolute/Relative

    GantryEvents::Type eventType; // 如果被拒，对应发布的事件类型
};
```

---

### 6.5 关键设计决策

| 决策 | 理由 |
|------|------|
| 服务无状态，委托给 `GantrySystem` | 避免服务与聚合根持有重复状态，单一数据源 |
| `monitorCycle()` 定义在服务而非实体 | 实体不依赖端口，端口注入由服务负责 |
| `executeStop()` 始终可接受 | Stop 是安全操作，不应因模式/限位/报警而拒绝 |
| 命令拆分逻辑在服务而非实体 | 拆分涉及 HAL 方向的物理知识（X2 反向），应在服务层做 |
| 拒绝原因用字符串而非枚举 | 支持运行时诊断日志，避免枚举穷举爆炸 |
| 采用 **端口-适配器** 的依赖方向 | `GantryDomainService` → `IGantryStatePort`（Domain → 端口），具体实现由 Infrastructure 层注入 |

---

### 6.6 Phase 8 测试用例清单

| TC 编号 | 约束 | 测试名 | Given | When | Then |
|---------|------|--------|-------|------|------|
| TC-8.1 | 18 | `Service_JogX_InCoupledMode_Accepted` | Coupled, X idle | executeJog(X, Fwd) | accepted=true |
| TC-8.2 | 18 | `Service_JogX1_InCoupledMode_Rejected` | Coupled | executeJog(X1, Fwd) | rejected (Mode) |
| TC-8.3 | 18 | `Service_JogX1_InDecoupledMode_Accepted` | Decoupled, X1 idle | executeJog(X1, Fwd) | accepted=true |
| TC-8.4 | 18 | `Service_JogX_InDecoupledMode_Rejected` | Decoupled | executeJog(X, Fwd) | rejected (Mode) |
| TC-8.5 | 17 | `Service_Move_InAlarmState_Rejected` | alarm=true | executeMoveAbsolute(X, 100.0) | rejected (Alarm) |
| TC-8.6 | 16 | `Service_JogForward_AtForwardLimit_Rejected` | fwdLimit=true | executeJog(X, Fwd) | rejected (Limit) |
| TC-8.7 | 16 | `Service_JogBackward_AtForwardLimit_Accepted` | fwdLimit=true | executeJog(X, Bwd) | accepted (远离限位) |
| TC-8.8 | 19 | `Service_Jog_WhenSlotBusy_Rejected` | X 正 Jog | executeMoveAbsolute(X, 50.0) | rejected (Busy) |
| TC-8.9 | 19 | `Service_Stop_WhenSlotBusy_Accepted` | X 正 Jog | executeStop(X) | accepted (Stop 始终可接受) |
| TC-8.10 | — | `Service_MoveRelative_CalcTarget` | X.pos=50.0, limit=[0, 200] | executeMoveRelative(X, 30.0) | target=80.0, accepted |
| TC-8.11 | — | `Service_MoveRelative_OutOfLimit_Rejected` | X.pos=180.0, posLim=200 | executeMoveRelative(X, 30.0) | target=210 > 200, rejected |
| TC-8.12 | 14 | `Service_MonitorCycle_DeviationFault` | Coupled, X1=50, X2=-49.95 (dev=0.05) | monitorCycle() | DeviationFault, mode→Decoupled |
| TC-8.13 | 14 | `Service_MonitorCycle_SyncOk` | Coupled, X1=50, X2=-50.00 (dev=0) | monitorCycle() | 无事件, mode 保持 Coupled |
| TC-8.14 | — | `Service_RequestCoupling_AllConditionsMet` | 使能, 无报警, 无限位, 同步 | requestCoupling() | allowed=true |
| TC-8.15 | — | `Service_RequestCoupling_OutOfSync_Rejected` | 使能, 无报警, 无限位, 不同步 | requestCoupling() | allowed=false, 含 failReason |

**预计测试数**：~15 个（Phase 8），可根据覆盖场景扩展至 ~30 个（加入更多组合场景）

---

## 七、端口接口设计（Phase 9，待实现）

### 7.1 IGantryStatePort（状态查询端口）

**职责**：定义 Domain 层对底层硬件状态的抽象查询接口。这是 **端口-适配器模式** 的核心：
Domain 层依赖此抽象接口，Infrastructure 层提供具体实现（如 `FakeGantryStatePort` 用于测试，
`EtherCATGantryStatePort` 用于实际硬件）。

**设计原则**：
- 仅暴露只读查询，不暴露写操作（写操作由 Application 层的 CommandBus 下发）
- 所有物理单位已在 HAL 层统一为毫米级浮点数
- 方向语义已在 HAL 层适配：Domain 层始终使用 Forward/Backward

```cpp
// domain/port/IGantryStatePort.h

/*
 * 龙门硬件状态快照值对象
 *
 * 每次读取时由实现类对 H/W 寄存器做一次性快照，
 * 确保整个校验流水线使用同一个状态基准，避免 TOCTOU 问题。
 */
struct GantryHardwareState {
    // X1 轴状态
    bool x1Enabled = false;
    double x1Position = 0.0;     // mm, HAL 已做单位转换
    bool x1Alarm = false;
    bool x1PosLimit = false;     // 正限位（Forward 方向）
    bool x1NegLimit = false;     // 负限位（Backward 方向）

    // X2 轴状态
    bool x2Enabled = false;
    double x2Position = 0.0;
    bool x2Alarm = false;
    bool x2PosLimit = false;
    bool x2NegLimit = false;

    // 龙门全局状态
    GantryMode currentMode = GantryMode::Decoupled;

    /*
     * 便捷派生查询（由 IGantryStatePort 实现类在 snapshot 时预计算）
     */
    bool anyAlarm() const { return x1Alarm || x2Alarm; }
    bool anyForwardLimit() const { return x1PosLimit || x2PosLimit; }
    bool anyBackwardLimit() const { return x1NegLimit || x2NegLimit; }
    bool anyLimit() const { return anyForwardLimit() || anyBackwardLimit(); }
};

/*
 * 龙门状态端口接口（抽象）
 *
 * 所有方法均为纯虚函数，由 Infrastructure 层的具体实现覆盖。
 * Domain 层仅依赖此接口，不感知底层通信协议。
 */
class IGantryStatePort {
public:
    virtual ~IGantryStatePort() = default;

    /*
     * 获取当前硬件状态一次性快照
     *
     * 实现要求：
     *   - 必须是原子操作：对 H/W 寄存器做一次性读取
     *   - 不得缓存（每次调用都从硬件读取，或由定时器定期刷新）
     *   - 线程安全（可能被 PLC 周期线程和 UI 查询线程并发调用）
     *
     * @return 当前硬件状态完整快照
     */
    virtual GantryHardwareState getCurrentState() const = 0;

    /*
     * 查询指定轴的当前位置
     *
     * 便捷方法，可基于 getCurrentState() 实现，或由子类直接
     * 从硬件寄存器高效读取单轴位置。
     *
     * @param id  轴标识（X1 或 X2）
     * @return    当前位置值 (mm)
     */
    virtual double getAxisPosition(AxisId id) const = 0;

    /*
     * 查询当前模式
     *
     * 便捷方法，可基于 getCurrentState() 实现。
     *
     * @return 当前龙门模式（Coupled / Decoupled）
     */
    virtual GantryMode getCurrentMode() const = 0;
};
```

---

### 7.2 IGantryCommandPort（命令下发端口）

**职责**：定义 Domain 层将校验后的命令下发到 HAL 层的抽象接口。

**为什么需要第二个端口？**

`IGantryStatePort` 只负责"读"，命令的"写"需要另一个端口，遵循 CQRS 原则（命令查询职责分离）。
这使得 Domain 层可以在不依赖具体硬件的情况下完成完整的运动语义闭环。

```cpp
// domain/port/IGantryCommandPort.h

#include "MotionDirection.h"

/*
 * 单轴命令结构体
 *
 * 已通过 Domain 层所有校验（模式、安全、命令槽互斥、软限位），
 * HAL 层只需执行，不再做业务判断。
 */
struct GantryAxisCommand {
    enum class Type { None, Jog, MoveAbsolute, MoveRelative, Stop };

    Type type = Type::None;
    AxisId target = AxisId::X1;          // 目标物理轴
    MotionDirection jogDirection = MotionDirection::Forward;
    double moveTarget = 0.0;             // MoveAbsolute 的目标位置 (mm)
    double moveDelta = 0.0;              // MoveRelative 的位移量 (mm)
    bool enable = false;                 // EnableCommand 的使能标志
};

/*
 * 龙门命令下发端口接口（抽象）
 *
 * Domain 层在命令校验通过后，调用此端口将命令传递给 HAL 层。
 * 实现由 Infrastructure 层提供（PLC 总、EtherCAT、Fake 驱动等）。
 */
class IGantryCommandPort {
public:
    virtual ~IGantryCommandPort() = default;

    /*
     * 下发单轴命令
     *
     * @param cmd  已校验的轴命令
     * @return      true = 命令已成功下发到 HAL 层，false = 下发失败
     */
    virtual bool sendCommand(const GantryAxisCommand& cmd) = 0;

    /*
     * 同时下发两个轴命令（用于 Coupled 模式的镜像命令）
     *
     * 实现要求：
     *   - 必须保证两个命令同时生效（原子下发）
     *   - 如果任一轴下发失败，应回滚或报错
     *   - 对于 EtherCAT / PLC，可利用分布式时钟 (DC) 同步机制
     *
     * @param cmd1  X1 轴命令
     * @param cmd2  X2 轴命令（通常与 cmd1 镜像，但方向/符号可能反转）
     * @return      true = 双轴命令均成功下发
     */
    virtual bool sendDualCommand(
        const GantryAxisCommand& cmd1,
        const GantryAxisCommand& cmd2
    ) = 0;

    /*
     * 紧急停止（旁路所有业务校验）
     *
     * 调用后硬件进入安全状态，所有轴立即停止。
     * 此操作不可逆，Domain 层状态机需要重置。
     */
    virtual bool emergencyStop() = 0;
};
```

---

### 7.3 Fake 实现（用于 TDD 测试）

```cpp
// tests/infrastructure/gantry/FakeGantryStatePort.h
// Phase 9 用于隔离测试 GantryDomainService 的 Fake 实现

class FakeGantryStatePort : public IGantryStatePort {
public:
    // 测试夹具直接设置状态值
    void setX1Enabled(bool v) { m_state.x1Enabled = v; }
    void setX2Enabled(bool v) { m_state.x2Enabled = v; }
    void setX1Position(double v) { m_state.x1Position = v; }
    void setX2Position(double v) { m_state.x2Position = v; }
    void setX1Alarm(bool v) { m_state.x1Alarm = v; }
    void setX2Alarm(bool v) { m_state.x2Alarm = v; }
    void setX1PosLimit(bool v) { m_state.x1PosLimit = v; }
    void setX1NegLimit(bool v) { m_state.x1NegLimit = v; }
    void setX2PosLimit(bool v) { m_state.x2PosLimit = v; }
    void setX2NegLimit(bool v) { m_state.x2NegLimit = v; }
    void setMode(GantryMode m) { m_state.currentMode = m; }

    // 便捷方法：一键设置完美同步状态
    void setPerfectSync(double x1Pos);

    // 便捷方法：设置偏差状态
    void setWithDeviation(double x1Pos, double deviation);

    // IGantryStatePort 接口实现
    GantryHardwareState getCurrentState() const override { return m_state; }
    double getAxisPosition(AxisId id) const override;
    GantryMode getCurrentMode() const override { return m_state.currentMode; }

private:
    GantryHardwareState m_state;
    // 初始状态：Decoupled, 未使能, 无报警, 无限位, 位置 0
};
```

```cpp
// tests/infrastructure/gantry/FakeGantryCommandPort.h
// 用于验证 Domain 层下发的命令是否正确

class FakeGantryCommandPort : public IGantryCommandPort {
public:
    // 记录最近一次发送的命令（用于测试断言）
    GantryAxisCommand lastCommand() const { return m_lastCmd; }
    GantryAxisCommand lastCommand2() const { return m_lastCmd2; }
    int commandCount() const { return m_commandCount; }
    bool wasDualCommand() const { return m_wasDual; }
    bool wasEmergencyStop() const { return m_emergencyStop; }

    // IGantryCommandPort 接口实现
    bool sendCommand(const GantryAxisCommand& cmd) override;
    bool sendDualCommand(
        const GantryAxisCommand& cmd1,
        const GantryAxisCommand& cmd2
    ) override;
    bool emergencyStop() override;

    // 重置记录（在每个测试 SetUp 中调用）
    void reset();

private:
    GantryAxisCommand m_lastCmd;
    GantryAxisCommand m_lastCmd2;
    int m_commandCount = 0;
    bool m_wasDual = false;
    bool m_emergencyStop = false;
};
```

---

### 7.4 Phase 9 测试用例清单

**测试文件**：`tests/domain/gantry/test_gantry_service_integration.cpp`

Phase 9 的测试聚焦于 **集成测试**：验证 `GantryDomainService` 通过端口与
Fake 实现的完整交互流程。这不同于 Phase 7-8 的纯单元测试。

| TC 编号 | 约束 | 测试名 | Given | When | Then |
|---------|------|--------|-------|------|------|
| TC-9.1 | — | `Integration_CoupledJog_SendsDualCommand` | Coupled, sync ok | executeJog(X, Fwd) | sendDualCommand 被调用, cmd1.target=X1, cmd2.target=X2 |
| TC-9.2 | — | `Integration_DecoupledJog_SendsSingleCommand` | Decoupled, X1 idle | executeJog(X1, Fwd) | sendCommand(X1) 被调用, sendDualCommand 未调用 |
| TC-9.3 | 14 | `Integration_DeviationFault_AutoDecouples` | Coupled, X1=50, X2=-49.95 | monitorCycle() | 模式变为 Decoupled, DeviationFault 事件产生 |
| TC-9.4 | — | `Integration_StatePortCalled_EachMonitorCycle` | 任意状态 | 连续 3 次 monitorCycle() | getCurrentState() 被调用 3 次 |
| TC-9.5 | — | `Integration_StateSnapshot_IsConsistent` | Fake 状态 X1=100, X2=-100 | DomainService 在一次 monitorCycle 中多次读取 | 所有读取返回相同快照 |
| TC-9.6 | — | `Integration_EmergencyStop_BypassesAllChecks` | Coupled, X 正 Jog | emergencyStop() | 立即停止, 模式不变, 命令槽被清空 |
| TC-9.7 | — | `Integration_FullCycle_CoupleToMove` | Decoupled, sync ok | requestCoupling → executeJog → monitorCycle → executeStop → requestDecoupling | 完整流程无错误, 所有端口调用顺序正确 |

**预计测试数**：~15 个（Phase 9）

---

### 7.5 端口与实体/服务的依赖关系

```
                  ┌──────────────────┐
                  │ Application 层    │
                  │ (UseCase / UI)    │
                  └────────┬─────────┘
                           │ 调用
                           ▼
              ┌────────────────────────┐
              │  GantryDomainService   │  (Domain 层，编排器)
              │  - executeJog()        │
              │  - monitorCycle()      │
              └───┬──────────────┬─────┘
                  │ 委托         │ 委托
         ┌────────▼─────┐  ┌────▼──────────────┐
         │ GantrySystem │  │ IGantryStatePort  │  (端口，Domain 定义)
         │ (聚合根)      │  │ IGantryCommandPort│
         └──────────────┘  └──▲────────▲───────┘
                               │        │
                    ┌──────────┘        └──────────┐
                    │ 实现                          │ 实现
         ┌──────────┴──────────┐      ┌────────────┴────────────┐
         │ FakeGantryStatePort │      │ EtherCATGantryStatePort  │
         │ (测试用)             │      │ (生产用)                  │
         └─────────────────────┘      └─────────────────────────┘
```

**依赖方向**：Domain → Port（箭头从 Domain 指向 Port，符合依赖倒置原则）

Port 接口定义在 `domain/port/`，由 `infrastructure/` 实现。Domain 永远不依赖 Infrastructure。

---

### 7.6 Phase 9 文件清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `domain/port/IGantryStatePort.h` | 端口接口 | 龙门状态查询抽象 |
| `domain/port/IGantryCommandPort.h` | 端口接口 | 龙门命令下发抽象 |
| `tests/infrastructure/gantry/FakeGantryStatePort.h` | Fake 实现 | 测试用状态端口 |
| `tests/infrastructure/gantry/FakeGantryCommandPort.h` | Fake 实现 | 测试用命令端口 |
| `tests/domain/gantry/test_gantry_service_integration.cpp` | 集成测试 | Phase 9 集成测试 |

---

## 八、TDD 实施策略总结

### 8.1 实施链路

```
1. 阅读业务约束文档
2. 定义测试用例（红）
3. 定义值对象 / 实体接口（头文件）
4. 实现逻辑使测试通过（绿）
5. 重构优化（维持绿）
6. 运行全量测试确认无回归
```

### 8.2 已实施阶段

| Phase | 阶段名称 | 交付物 | 测试数 | 状态 |
|-------|---------|-------|-------|------|
| 0 | 实体基础 | `Axis` 实体 + `AxisState` 状态机 | 19 | ✅ |
| 1 | 方向语义 | `MotionDirection` 值对象 | 12 | ✅ |
| 2 | 模式语义 | `GantryMode` 枚举 + 状态验证 | 18 | ✅ |
| 3 | 位置语义 | X 逻辑位置计算测试 | 19 | ✅ |
| 4–5 | (合并入 6) | | | |
| 6 | 同步与安全 | `PositionConsistency` + `CouplingCondition` + `SafetyCheckResult` | 60 | ✅ |
| **合计** | | | **128** | ✅ |

### 8.3 待实施阶段

| Phase | 阶段名称 | 预计测试数 |
|-------|---------|----------|
| 7 | `PhysicalAxis` + `LogicalAxis` + `GantrySystem` 聚合根 | ~16 |
| 8 | `GantryDomainService` 领域服务 | ~30 |
| 9 | 端口接口 + Fake 驱动集成 | ~15 |
| **预计总测试数** | | **~189** |

### 8.4 测试目录结构

```
tests/domain/
├── test_axis.cpp                    # Axis 实体测试（19 个）
└── gantry/
    ├── test_gantry_mode.cpp         # 模式语义（18 个）
    ├── test_gantry_direction.cpp    # 方向语义（12 个）
    ├── test_gantry_position.cpp     # 位置语义（19 个）
    ├── test_gantry_sync.cpp         # 同步与安全（60 个）
    ├── test_gantry_axis.cpp         # ⬜ 聚合根（待创建）
    └── test_gantry_service.cpp      # ⬜ 领域服务（待创建）
```

---

## 九、后续规划

### 9.1 立即待办

1. **Phase 7**: 实现 `GantryAxis` 聚合根（TDD）
2. **Phase 8**: 实现 `GantryDomainService` 领域服务
3. **Phase 9**: 定义端口接口 + Fake 实现 + 集成测试

### 9.2 架构扩展方向

- **多龙门支持**：将 `GantryAxis` 扩展为 `GantryGroup`，管理 Y1/Y2/Z1/Z2 等多对轴
- **轨迹规划**：在 Application 层加入前瞻算法，Domain 层仅做合法性校验
- **持久化**：引入 Repository 模式，将轴配置、限位参数持久化到 EEPROM/文件

---

> 📌 **设计原则回顾**  
> *"Domain 层只回答 '能不能动'，不回答 '怎么动'。  
> 所有物理知识属于 HAL 层，所有业务规则属于 Domain 层。"*
