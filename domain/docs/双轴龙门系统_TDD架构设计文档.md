# 双轴龙门系统（Gantry）Domain 层 TDD 架构设计文档

> 版本: v1.0
> 状态: 设计阶段（Design），非实现阶段
> 依据: `docs/第7阶段：双轴同步龙门轴/双轴龙门结构的业务约束.md`

---

## 目录

1. [设计理念与核心原则](#1-设计理念与核心原则)
2. [领域模型总览](#2-领域模型总览)
3. [领域实体模型 (Entity)](#3-领域实体模型-entity)
4. [领域值对象 (Value Object)](#4-领域值对象-value-object)
5. [领域服务 (Domain Service)](#5-领域服务-domain-service)
6. [领域事件 (Domain Event)](#6-领域事件-domain-event)
7. [端口接口 (Port / Repository)](#7-端口接口-port--repository)
8. [TDD 测试用例体系](#8-tdd-测试用例体系)
9. [文件布局规划](#9-文件布局规划)
10. [与现有代码的集成策略](#10-与现有代码的集成策略)

---

## 1. 设计理念与核心原则

### 1.1 设计哲学

```
┌──────────────────────────────────────────────────────┐
│  "X（龙门轴）是唯一对外轴                                │
│   X1 / X2 是实现细节（物理执行单元）"                     │
│                                                      │
│  Domain 负责"是否允许操作"，不负责"如何执行"              │
│  所有物理差异（方向/坐标）由 HAL 层消化                   │
└──────────────────────────────────────────────────────┘
```

### 1.2 十条设计铁律

| # | 铁律 | 对应约束 |
|---|------|----------|
| 1 | X 是唯一对外暴露的逻辑轴 | 约束1, 3 |
| 2 | X1/X2 是实现细节，不参与业务决策 | 约束4, 18 |
| 3 | 所有操作基于统一逻辑方向（Forward/Backward） | 约束5, 6, 7 |
| 4 | 联动是状态，不是命令 | 约束12 |
| 5 | 分动与联动互斥 | 约束2 |
| 6 | 联动建立必须满足位置一致性 | 约束11, 13 |
| 7 | 运行中必须持续满足同步约束 | 约束14 |
| 8 | 限位与报警优先级高于一切运动 | 约束15, 16, 17 |
| 9 | Domain 负责"是否允许操作"，不负责"如何执行" | 架构原则 |
| 10 | 所有物理差异由 HAL 层消化 | 架构原则 |

### 1.3 聚合边界

```
┌─────────────────────────────────────────────────────────┐
│  GantrySystem（龙门系统聚合根）                           │
│                                                         │
│  ┌─────────────────────┐  ┌─────────────────────┐       │
│  │ LogicalAxis X        │  │ PhysicalAxis X1    │       │
│  │ (对外唯一接口)        │  │ (内部实现细节)       │       │
│  └─────────────────────┘  └─────────────────────┘       │
│                             ┌─────────────────────┐    │
│                             │ PhysicalAxis X2     │       │
│                             │ (内部实现细节)        │       │
│                             └─────────────────────┘       │
│                                                         │
│  ┌─────────────────────────────────────────────────┐    │
│  │ GantryMode (联动/分动)                            │    │
│  │ PositionConsistency (位置一致性)                  │    │
│  │ SafetyState (限位/报警)                           │    │
│  └─────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────┘
```

聚合根 **GantrySystem** 内部持有 X1、X2，外部世界只能通过 **X 逻辑轴** 与龙门系统交互。

---

## 2. 领域模型总览

### 2.1 类图总览

```
                         ┌──────────────────────────┐
                         │   IGantryFeedbackPort     │  (Port - 出站)
                         │   + getPhysicalX1State()  │
                         │   + getPhysicalX2State()  │
                         └─────────────┬────────────┘
                                       │ 实现于 HAL 层
                                       │
┌──────────────────────────┐  ┌────────┴─────────────┐
│  GantryCouplingService   │  │                      │
│  + requestCoupling()     │──│   GantrySystem       │  (聚合根)
│  + requestDecoupling()   │  │   (Aggregate Root)   │
│  + validateConditions()  │  │                      │
└─────────────┬────────────┘  │   + X  : LogicalAxis │
              │               │   + X1 : PhysicalAxis│
              │ 调用          │   + X2 : PhysicalAxis│
┌─────────────┴────────────┐  │   + mode : GantryMode│
│  GantrySafetyService     │  │   + safety: Safety   │
│  + checkMotionAllowed()  │──│                      │
│  + checkLimitDirection() │  └──────────┬───────────┘
│  + checkAlarm()          │             │
└─────────────┬────────────┘             │ 发出
              │                          │
              │ 调用               ┌──────┴──────────────┐
┌─────────────┴────────────┐      │  GantryDomainEvents  │
│  GantryStateAggregator   │      │  - ModeChanged       │
│  + aggregateState()      │      │  - DeviationFault    │
│  + aggregatePosition()   │      │  - CouplingRefused   │
└──────────────────────────┘      │  - LimitTriggered    │
                                  └─────────────────────┘

值对象 (Value Objects):
  GantryMode       : Coupled | Decoupled
  MotionDirection  : Forward | Backward
  PositionConsistency : (x1, x2, epsilon) => bool
  GantryPosition   : value (double)
  CouplingCondition: [Enabled, NoAlarm, NoLimit, Consistent]
  SafetyCheckResult: Allowed | Rejected(reason)
```

### 2.2 状态机模型

```
        ┌─────────────┐
        │  Decoupled  │
        │  (分动模式)  │
        └──────┬──────┘
               │
    requestCoupling()
    [满足联动条件]
               │
               ▼
        ┌─────────────┐                ┌──────────────────┐
        │  Coupling    │───────────────→│  Coupled          │
        │  (建立中)     │  条件满足      │  (联动运行中)      │
        └─────────────┘                └───────┬────────────┘
                                               │
                                  DeviationFault / Alarm / Limit
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │  Fault       │
                                        │  (故障状态)   │
                                        └──────┬───────┘
                                               │
                                  Reset + 满足恢复条件
                                               │
                                               ▼
                                        ┌──────────────┐
                                        │  Decoupled   │
                                        └──────────────┘
```

---

## 3. 领域实体模型 (Entity)

### 3.1 GantrySystem（龙门系统聚合根）

**文件**: `domain/entity/GantrySystem.h`

| 属性 | 类型 | 说明 |
|------|------|------|
| `m_logicalX` | `LogicalAxis` | 逻辑龙门轴（X），对外唯一接口 |
| `m_physicalX1` | `PhysicalAxis` | 物理轴 X1（内部） |
| `m_physicalX2` | `PhysicalAxis` | 物理轴 X2（内部） |
| `m_mode` | `GantryMode` | 当前模式 |
| `m_safety` | `GantrySafetyState` | 安全状态聚合 |

| 方法 | 返回 | 约束覆盖 |
|------|------|----------|
| `mode()` | `GantryMode` | 约束1, 2 |
| `logicalPosition()` | `GantryPosition` | 约束8, 10 |
| `positionConsistency()` | `PositionConsistency` | 约束9, 11 |
| `aggregateState()` | `AxisState` | 约束20 |
| `updateX1Feedback(fb)` | `void` | 内部更新 |
| `updateX2Feedback(fb)` | `void` | 内部更新 |
| `isOperable(axisId)` | `bool` | 约束18 |

> **设计说明**: GantrySystem 作为聚合根，外部不能直接访问 `m_physicalX1` / `m_physicalX2`。所有查询和操作都通过逻辑轴 X 进行。

### 3.2 LogicalAxis（逻辑轴）

**文件**: `domain/entity/LogicalAxis.h`

| 属性 | 类型 | 说明 |
|------|------|------|
| `m_position` | `GantryPosition` | 逻辑位置（从 X1.pos 映射） |
| `m_state` | `AxisState` | 聚合状态（约束20） |
| `m_direction` | `MotionDirection` | 禁止物理方向 |

| 方法 | 返回 | 约束覆盖 |
|------|------|----------|
| `position()` | `GantryPosition` | 约束8, 10 |
| `state()` | `AxisState` | 约束20 |

> **关键设计**: LogicalAxis 不暴露物理方向（CW/CCW），只使用 Forward/Backward。

### 3.3 PhysicalAxis（物理轴）

**文件**: `domain/entity/PhysicalAxis.h`

| 属性 | 类型 | 说明 |
|------|------|------|
| `m_position` | `double` | 物理位置 |
| `m_state` | `AxisState` | 物理状态（Enabled/Disabled/Error/...） |
| `m_hasAlarm` | `bool` | 报警状态 |
| `m_posLimitActive` | `bool` | 正限位触发 |
| `m_negLimitActive` | `bool` | 负限位触发 |

> **注意**: PhysicalAxis 是 GantrySystem 的内部实现细节。不在 domain 层暴露其独立操作接口（独立操作的决策在 Decoupled 模式下由上层 Policy 负责）。

---

## 4. 领域值对象 (Value Object)

### 4.1 GantryMode（龙门模式）

**文件**: `domain/value/GantryMode.h`

```cpp
enum class GantryMode {
    Coupled,    // 联动模式：X 是唯一可操作对象（约束1, 3）
    Decoupled,  // 分动模式：只能操作 X1 或 X2（约束1, 4）
};

// 模式互斥验证（约束2）
bool areMutuallyExclusive(GantryMode a, GantryMode b);
```

### 4.2 MotionDirection（运动方向）

**文件**: `domain/value/MotionDirection.h`

```cpp
enum class MotionDirection {
    Forward,   // 远离操作者（约束6）
    Backward,  // 靠近操作者（约束6）
};
// 约束7：Domomain 只接受 MotionDirection，不接受物理方向
```

> **说明**: 现有 `Direction` 枚举可以直接复用。如果现有枚举定义在 `Axis.h` 中作为电机相关的枚举，建议将业务层使用的方向抽离到 `value/` 目录。

### 4.3 PositionConsistency（位置一致性）

**文件**: `domain/value/PositionConsistency.h`

```cpp
class PositionConsistency {
public:
    static bool isConsistent(double x1Pos, double x2Pos, double epsilon);
    // 约束9: X1.pos ≈ -X2.pos（镜像关系）
    // 约束11: |X1.pos + X2.pos| > epsilon => 不同步

    static double computeLogicalPosition(double x1Pos);
    // 约束10: X.position = X1.pos

    static double computeDeviation(double x1Pos, double x2Pos);
    // |X1.pos + X2.pos|
};
```

### 4.4 GantryPosition（龙门位置）

**文件**: `domain/value/GantryPosition.h`

```cpp
class GantryPosition {
public:
    explicit GantryPosition(double value);
    double value() const;

    bool operator==(const GantryPosition& other) const;
    // 值相等语义
};
```

### 4.5 CouplingCondition（联动条件集合）

**文件**: `domain/value/CouplingCondition.h`

```cpp
class CouplingCondition {
public:
    bool x1Enabled;
    bool x2Enabled;        // 约束13.1: X1/X2 均已使能
    bool noAlarm;          // 约束13.2: 无报警
    bool noLimit;          // 约束13.3: 未触发限位
    bool positionConsistent; // 约束13.4: 位置一致性

    bool allSatisfied() const;
    // 约束13: 只有 allSatisfied() == true 才允许进入 Coupled

    std::string unsatisfiedReasons() const;
    // 诊断信息：哪些条件未满足
};
```

### 4.6 SafetyCheckResult（安全检查结果）

**文件**: `domain/value/SafetyCheckResult.h`

```cpp
class SafetyCheckResult {
public:
    bool allowed;
    std::string reason;

    static SafetyCheckResult allowed();
    static SafetyCheckResult rejected(const std::string& reason);
};
```

---

## 5. 领域服务 (Domain Service)

领域服务负责跨实体的业务逻辑，不持有状态。

### 5.1 GantryCouplingService（联动管理服务）

**文件**: `domain/service/GantryCouplingService.h`

```
职责: 管理联动/分动模式的切换，验证联动条件
```

| 方法 | 输入 | 输出 | 约束覆盖 |
|------|------|------|----------|
| `requestCoupling(GantrySystem&, feedbackX1, feedbackX2)` | 龙门系统 + X1/X2 反馈 | `SafetyCheckResult` | 约束12, 13 |
| `requestDecoupling(GantrySystem&)` | 龙门系统 | `SafetyCheckResult` | 约束4, 关闭联动 |
| `validateCouplingConditions(fbX1, fbX2)` | 物理反馈 | `CouplingCondition` | 约束13 |
| `terminateOnDeviation(GantrySystem&)` | 龙门系统 | `void` + 事件 | 约束14 |

**联动建立流程（约束13）**:
```
1. 检查 X1.state == Enabled && X2.state == Enabled
2. 检查 X1.hasAlarm == false && X2.hasAlarm == false
3. 检查 X1.posLimitActive == false && X2.posLimitActive == false
4. 检查 X1.negLimitActive == false && X2.negLimitActive == false
5. 检查 |X1.pos + X2.pos| <= GantrySystem::kPositionEpsilon
6. 所有条件满足 → 模式变更为 Coupled
7. 任一条件不满足 → 拒绝，返回拒绝原因
```

**持续同步监控（约束14）**:
```
在 Coupled 模式下，每次反馈更新后：
  if |X1.pos + X2.pos| > GantrySystem::kDeviationThreshold:
    → 触发 DeviationFaultEvent
    → 强制退出联动模式
    → 进入 Fault 状态
```

### 5.2 GantrySafetyService（安全约束服务）

**文件**: `domain/service/GantrySafetyService.h`

```
职责: 执行限位和报警约束，返回运动是否允许
```

| 方法 | 输入 | 输出 | 约束覆盖 |
|------|------|------|----------|
| `checkMotionAllowed(GantrySystem&, MotionDirection)` | 龙门系统 + 运动方向 | `SafetyCheckResult` | 约束15, 16, 17 |
| `isJogAllowed(GantrySystem&, MotionDirection)` | 龙门系统 + 点动方向 | `bool` | 约束16 |
| `isAlarmActive(GantrySystem&)` | 龙门系统 | `bool` | 约束17 |

**限位检查逻辑（约束16）**:
```
正向限位触发时：
  ✅ Jog(Backward)  - 允许（远离限位方向）
  ❌ Jog(Forward)   - 禁止（朝限位方向）
  ❌ MoveAbsolute   - 禁止
  ❌ MoveRelative   - 禁止

负向限位触发时：
  ✅ Jog(Forward)   - 允许（远离限位方向）
  ❌ Jog(Backward)  - 禁止（朝限位方向）
  ❌ MoveAbsolute   - 禁止
  ❌ MoveRelative   - 禁止
```

**报警检查逻辑（约束17）**:
```
任一物理轴处于 Alarm 状态：
  ❌ 所有运动操作
  ✅ ResetAlarm
```

### 5.3 GantryStateAggregator（状态聚合服务）

**文件**: `domain/service/GantryStateAggregator.h`

```
职责: 将 X1/X2 的物理状态聚合为龙门逻辑状态
```

| 方法 | 输入 | 输出 | 约束覆盖 |
|------|------|------|----------|
| `aggregateState(stateX1, stateX2)` | 两个物理状态 | `AxisState` | 约束20 |
| `aggregatePosition(posX1)` | X1 物理位置 | `GantryPosition` | 约束10 |
| `aggregateSafety(limitX1, limitX2, alarmX1, alarmX2)` | 物理安全状态 | `GantrySafetyState` | 约束15 |

**聚合规则（约束20）**:
```
X.state = 聚合状态：

if 任一轴：
    Moving → X = Moving
    Alarm  → X = Alarm
    Limit  → X = Limit
```

---

## 6. 领域事件 (Domain Event)

**文件**: `domain/event/GantryEvents.h`

所有事件是不可变值对象，用于解耦领域服务间的通信。

| 事件名 | 触发条件 | 携带数据 | 约束覆盖 |
|--------|----------|----------|----------|
| `GantryModeChangedEvent` | 模式切换成功时 | 旧模式, 新模式, 触发原因 | 约束2 |
| `GantryCouplingRefusedEvent` | 联动条件不满足时 | CouplingCondition, 拒绝原因 | 约束13 |
| `GantryDeviationFaultEvent` | 运行时偏差超阈值 | X1.pos, X2.pos, deviation, threshold | 约束14 |
| `GantryLimitTriggeredEvent` | 任一物理轴触发限位 | 触发的物理轴ID, 限位方向 | 约束15 |
| `GantryAlarmTriggeredEvent` | 任一物理轴报警 | 报警轴ID | 约束17 |
| `GantryPositionUpdatedEvent` | 龙门位置更新 | X.position, X1.pos, X2.pos | 约束8, 10 |

---

## 7. 端口接口 (Port / Repository)

领域层通过端口接口与外界（应用层、HAL层）解耦。

### 7.1 IGantryFeedbackPort（出站端口 - 接收 HAL 反馈）

**文件**: `domain/port/IGantryFeedbackPort.h`

```cpp
class IGantryFeedbackPort {
public:
    virtual ~IGantryFeedbackPort() = default;

    // 获取物理轴 X1 的反馈
    virtual PhysicalAxisFeedback getX1Feedback() const = 0;

    // 获取物理轴 X2 的反馈
    virtual PhysicalAxisFeedback getX2Feedback() const = 0;

    // 触发报警复位
    virtual void resetAlarm() = 0;
};
```

### 7.2 IGantryCommandPort（出站端口 - 下发运动命令）

**文件**: `domain/port/IGantryCommandPort.h`

```cpp
class IGantryCommandPort {
public:
    virtual ~IGantryCommandPort() = default;

    // 向 X1 发送命令（联动模式下与 X2 同步）
    virtual void sendToX1(const AxisCommand& cmd) = 0;

    // 向 X2 发送命令（联动模式下与 X1 同步）
    virtual void sendToX2(const AxisCommand& cmd) = 0;

    // 同时向 X1/X2 发送同步命令（联动模式）
    virtual void sendSynchronized(const AxisCommand& cmd) = 0;
};
```

### 7.3 IGantryEventBus（出站端口 - 领域事件发布）

**文件**: `domain/port/IGantryEventBus.h`

```cpp
class IGantryEventBus {
public:
    virtual ~IGantryEventBus() = default;

    virtual void publish(const GantryModeChangedEvent& event) = 0;
    virtual void publish(const GantryDeviationFaultEvent& event) = 0;
    virtual void publish(const GantryCouplingRefusedEvent& event) = 0;
    virtual void publish(const GantryLimitTriggeredEvent& event) = 0;
    virtual void publish(const GantryAlarmTriggeredEvent& event) = 0;
    virtual void publish(const GantryPositionUpdatedEvent& event) = 0;
};
```

---

## 8. TDD 测试用例体系

按照 **Red → Green → Refactor** 的 TDD 节奏，将20条约束映射为测试用例。每个测试类对应一个领域组件。

### 8.1 测试文件组织结构

```
tests/domain/gantry/
├── test_gantry_mode.cpp             # 模式语义约束 (约束1-4)
├── test_gantry_direction.cpp        # 方向语义约束 (约束5-7)
├── test_gantry_position.cpp         # 位置语义约束 (约束8-11)
├── test_gantry_coupling.cpp         # 联动建立约束 (约束12-14)
├── test_gantry_safety.cpp           # 安全语义约束 (约束15-17)
├── test_gantry_activation.cpp       # 激活与操作约束 (约束18-20)
├── test_gantry_system_integration.cpp # 聚合根集成测试
└── test_gantry_service_integration.cpp # 领域服务集成测试
```

### 8.2 🧩 第一组：模式语义约束测试 (test_gantry_mode.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-1.1 | 约束1 | `GantryMode_Coupled_ShouldAllowXOperation` | 联动模式下，X 是可操作对象 |
| TC-1.2 | 约束1 | `GantryMode_Decoupled_ShouldNotAllowXOperation` | 分动模式下，X 不可操作 |
| TC-1.3 | 约束2 | `GantryMode_ShouldBeMutuallyExclusive` | 模式互斥：不能同时处于两种模式 |
| TC-1.4 | 约束2 | `GantryMode_Switch_ShouldTransitionAtomically` | 模式切换是原子的 |
| TC-1.5 | 约束3 | `CoupledMode_Jog_ShouldOperateOnX` | 联动模式下 Jog 操作语义=对 X 整体操作 |
| TC-1.6 | 约束3 | `CoupledMode_MoveAbsolute_ShouldOperateOnX` | 联动模式下 MoveAbsolute 语义=对 X 整体操作 |
| TC-1.7 | 约束3 | `CoupledMode_MoveRelative_ShouldOperateOnX` | 联动模式下 MoveRelative 语义=对 X 整体操作 |
| TC-1.8 | 约束4 | `DecoupledMode_ShouldOnlyAllowX1OrX2` | 分动模式下只能操作 X1 或 X2 |
| TC-1.9 | 约束4 | `DecoupledMode_X_ShouldBeReadOnly` | 分动模式下 X 是只读状态 |

### 8.3 🧩 第二组：方向语义约束测试 (test_gantry_direction.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-2.1 | 约束5 | `Direction_ShouldHaveOnlyTwoValues` | 只存在 Forward/Backward 两种方向 |
| TC-2.2 | 约束6 | `Direction_Forward_ShouldBeAwayFromOperator` | Forward = 远离操作者 |
| TC-2.3 | 约束6 | `Direction_Backward_ShouldBeTowardOperator` | Backward = 靠近操作者 |
| TC-2.4 | 约束6 | `Direction_ShouldNotDependOnPhysicalDirection` | 方向与电机正反转无关 |
| TC-2.5 | 约束7 | `JogCommand_MustUseLogicalDirection` | Jog 必须使用 Forward/Backward |
| TC-2.6 | 约束7 | `MoveCommand_MustUseLogicalDirection` | Move 必须使用 Forward/Backward |
| TC-2.7 | 约束7 | `PhysicalDirection_ShouldBeRejected` | 禁止使用物理方向（CW/CCW） |

### 8.4 🧩 第三组：位置语义约束测试 (test_gantry_position.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-3.1 | 约束8 | `GantryPosition_ShouldBeUnified` | X.position 是龙门整体位置（逻辑坐标） |
| TC-3.2 | 约束9 | `PositionConsistency_X1ShouldBeNegativeOfX2` | X1.pos ≈ -X2.pos（镜像关系） |
| TC-3.3 | 约束9 | `PositionConsistency_MirrorRelationShouldHold` | 验证镜像关系 |
| TC-3.4 | 约束10 | `LogicalPosition_ShouldEqualX1Position` | X.position = X1.pos |
| TC-3.5 | 约束10 | `LogicalPosition_ShouldUpdateWithX1` | X 位置随 X1 实时更新 |
| TC-3.6 | 约束11 | `PositionInconsistency_ShouldDetectDeviation` | 偏差超过 epsilon 时检测 |
| TC-3.7 | 约束11 | `PositionInconsistency_ShouldPreventCoupling` | 不同步时禁止进入联动 |

### 8.5 🧩 第四组：联动建立约束测试 (test_gantry_coupling.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-4.1 | 约束12 | `Coupling_IsStateApplication_NotForceSwitch` | 联动是"状态申请"，不是强制切换 |
| TC-4.2 | 约束13 | `Coupling_X1NotEnabled_ShouldReject` | X1 未使能 → 拒绝联动 |
| TC-4.3 | 约束13 | `Coupling_X2NotEnabled_ShouldReject` | X2 未使能 → 拒绝联动 |
| TC-4.4 | 约束13 | `Coupling_Alarm_ShouldReject` | 有报警 → 拒绝联动 |
| TC-4.5 | 约束13 | `Coupling_LimitTriggered_ShouldReject` | 有限位 → 拒绝联动 |
| TC-4.6 | 约束13 | `Coupling_PositionInconsistent_ShouldReject` | 位置不一致 → 拒绝联动 |
| TC-4.7 | 约束13 | `Coupling_AllConditionsSatisfied_ShouldAccept` | 所有条件满足 → 接受联动 |
| TC-4.8 | 约束14 | `CoupledMode_DeviationExceeded_ShouldTriggerFault` | 运行中偏差超阈值 → 触发 DeviationFault |
| TC-4.9 | 约束14 | `DeviationFault_ShouldForceDecoupling` | DeviationFault → 强制退出联动 |
| TC-4.10 | 约束14 | `CoupledMode_DeviationWithinRange_ShouldNotFault` | 偏差在阈值内 → 正常 |

### 8.6 🧩 第五组：安全语义约束测试 (test_gantry_safety.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-5.1 | 约束15 | `Limit_HasHighestPriority` | 限位优先级最高，一旦触发，所有运动非法 |
| TC-5.2 | 约束15 | `Limit_AnyMotionShouldBeRejected` | 限位时拒绝任何运动 |
| TC-5.3 | 约束16 | `Limit_PositiveAllowedJogBackward` | 正限位：只允许负向 Jog |
| TC-5.4 | 约束16 | `Limit_PositiveForbiddenJogForward` | 正限位：禁止正向 Jog |
| TC-5.5 | 约束16 | `Limit_NegativeAllowedJogForward` | 负限位：只允许正向 Jog |
| TC-5.6 | 约束16 | `Limit_NegativeForbiddenJogBackward` | 负限位：禁止负向 Jog |
| TC-5.7 | 约束16 | `Limit_RejectMoveAbsolute` | 限位时禁止 MoveAbsolute |
| TC-5.8 | 约束16 | `Limit_RejectMoveRelative` | 限位时禁止 MoveRelative |
| TC-5.9 | 约束17 | `Alarm_RejectAllMotion` | 报警时禁止所有运动 |
| TC-5.10 | 约束17 | `Alarm_OnlyAllowResetAlarm` | 报警时只允许 ResetAlarm |
| TC-5.11 | 约束17 | `Alarm_RejectJogAndMove` | 报警时禁止 Jog/MoveAbsolute/MoveRelative |

### 8.7 🧩 第六组：激活与操作约束测试 (test_gantry_activation.cpp)

| 编号 | 对应约束 | 测试名称 | 测试描述 |
|------|----------|----------|----------|
| TC-6.1 | 约束18 | `CoupledMode_OnlyXIsOperable` | 联动模式：只能操作 X |
| TC-6.2 | 约束18 | `DecoupledMode_X1OrX2IsOperable` | 分动模式：只能操作 X1 或 X2 |
| TC-6.3 | 约束18 | `SimultaneousOperation_ShouldBeForbidden` | 禁止同时操作多个对象 |
| TC-6.4 | 约束19 | `MotionExclusive_JogAndMove` | Jog 和 Move 互斥 |
| TC-6.5 | 约束19 | `MotionExclusive_MoveAbsoluteAndMoveRelative` | MoveAbsolute 和 MoveRelative 互斥 |
| TC-6.6 | 约束19 | `OnlyOneMotionIntent_AtAnyTime` | 任意时刻只允许一个运动意图 |
| TC-6.7 | 约束20 | `StateAggregation_X1Moving_XShouldBeMoving` | 任一轴 Moving → X = Moving |
| TC-6.8 | 约束20 | `StateAggregation_X2Alarm_XShouldBeAlarm` | 任一轴 Alarm → X = Alarm |
| TC-6.9 | 约束20 | `StateAggregation_X2Limit_XShouldBeLimit` | 任一轴 Limit → X = Limit |
| TC-6.10 | 约束20 | `StateAggregation_BothIdle_XShouldBeIdle` | 两轴都 Idle → X = Idle |

### 8.8 第七组：聚合根集成测试 (test_gantry_system_integration.cpp)

| 编号 | 测试名称 | 测试描述 |
|------|----------|----------|
| TC-7.1 | `GantrySystem_FullCoupleDecoupleLifecycle` | 完整的联动建立→运行→异常→恢复生命周期 |
| TC-7.2 | `GantrySystem_UpdateFeedback_ShouldUpdatePositions` | 反馈更新应同步更新位置 |
| TC-7.3 | `GantrySystem_ExternalCannotAccessPhysicalAxes` | 外部不能直接访问物理轴 |
| TC-7.4 | `GantrySystem_ModeChange_ShouldEmitEvent` | 模式变更应发出领域事件 |
| TC-7.5 | `GantrySystem_DeviationFault_ShouldEmitEvent` | 偏差故障应发出事件 |

### 8.9 第八组：领域服务集成测试 (test_gantry_service_integration.cpp)

| 编号 | 测试名称 | 测试描述 |
|------|----------|----------|
| TC-8.1 | `CouplingService_ValidateAllConditions` | 联动服务：验证所有条件的综合判断 |
| TC-8.2 | `CouplingService_RefuseWithDiagnostics` | 联动服务：拒绝时应返回诊断信息 |
| TC-8.3 | `SafetyService_AlarmOverridesLimit` | 安全服务：报警优先级高于限位 |
| TC-8.4 | `SafetyService_LimitDirectionLogic` | 安全服务：限位方向逻辑完整性 |
| TC-8.5 | `StateAggregator_AllScenarios` | 状态聚合器：全场景覆盖 |
| TC-8.6 | `EndToEnd_DecoupledToCoupledLifecycle` | 端到端：分动→联动→运行→故障→恢复全生命周期 |

### 8.10 测试覆盖率目标

```
实体层 (Entity):     85%+ 行覆盖
值对象层 (Value):    100% 行覆盖  (纯函数，完全可测)
领域服务层 (Service): 95%+ 分支覆盖
端口接口 (Port):      由 HAL 层实现时完成集成测试
事件 (Event):         100% 覆盖所有事件类型触发路径
```

---

## 9. 文件布局规划

### 9.1 Domain 层新增文件清单

```
domain/
├── entity/
│   ├── Axis.h                      # [现有] 保留不变
│   ├── Axis.cpp                    # [现有] 保留不变
│   ├── AxisId.h                    # [现有] 保留不变
│   ├── SystemContext.h             # [现有] 保留不变，仅作为分组容器
│   ├── GantrySystem.h              # [新增] 龙门系统聚合根（★ 核心）
│   ├── LogicalAxis.h               # [新增] 逻辑龙门轴
│   └── PhysicalAxis.h              # [新增] 物理轴（X1/X2 内部实现）
│
├── value/
│   ├── GantryMode.h                # [新增] 龙门模式值对象
│   ├── MotionDirection.h           # [新增] 逻辑运动方向（或复用 Axis.h 中的 Direction）
│   ├── GantryPosition.h            # [新增] 龙门位置值对象
│   ├── PositionConsistency.h       # [新增] 位置一致性计算
│   ├── CouplingCondition.h         # [新增] 联动条件值对象
│   └── SafetyCheckResult.h         # [新增] 安全检查结果值对象
│
├── service/
│   ├── GantryCouplingService.h     # [新增] 联动管理域服务
│   ├── GantrySafetyService.h       # [新增] 安全约束域服务
│   └── GantryStateAggregator.h     # [新增] 状态聚合域服务
│
├── event/
│   └── GantryEvents.h              # [新增] 领域事件定义
│
└── port/
    ├── IGantryFeedbackPort.h       # [新增] HAL 反馈端口
    ├── IGantryCommandPort.h        # [新增] 命令下发端口
    └── IGantryEventBus.h           # [新增] 事件总线端口
```

### 9.2 测试文件清单

```
tests/domain/
├── test_axis.cpp                   # [现有] 保留
├── test_system_context.cpp         # [现有] 保留
└── gantry/                         # [新增] 龙门系统专项测试目录
    ├── test_gantry_mode.cpp
    ├── test_gantry_direction.cpp
    ├── test_gantry_position.cpp
    ├── test_gantry_coupling.cpp
    ├── test_gantry_safety.cpp
    ├── test_gantry_activation.cpp
    ├── test_gantry_system_integration.cpp
    └── test_gantry_service_integration.cpp
```

### 9.3 构建配置修改

需要在 `domain/CMakeLists.txt` 和 `tests/CMakeLists.txt` 中添加新文件：

```
# domain/CMakeLists.txt 新增:
# - entity/GantrySystem.h
# - entity/LogicalAxis.h
# - entity/PhysicalAxis.h
# - value/*.h
# - service/*.h
# - event/GantryEvents.h
# - port/*.h

# tests/CMakeLists.txt 新增:
# - tests/domain/gantry/test_gantry_mode.cpp
# - tests/domain/gantry/test_gantry_direction.cpp
# - tests/domain/gantry/test_gantry_position.cpp
# - tests/domain/gantry/test_gantry_coupling.cpp
# - tests/domain/gantry/test_gantry_safety.cpp
# - tests/domain/gantry/test_gantry_activation.cpp
# - tests/domain/gantry/test_gantry_system_integration.cpp
# - tests/domain/gantry/test_gantry_service_integration.cpp
```

---

## 10. 与现有代码的集成策略

### 10.1 与现有 `SystemContext` 的关系

```
现有模型:
  SystemContext
    ├── Axis Y, Z, R (常规轴)
    ├── Axis X  (逻辑轴占位符)
    ├── Axis X1 (物理轴占位符)
    ├── Axis X2 (物理轴占位符)
    └── m_isGantryCoupled (联动标志位)

新模型（推荐演进路径）:
  SystemContext
    ├── Axis Y, Z, R (常规轴 - 不变)
    └── GantrySystem (龙门系统聚合根 - 替代原 X/X1/X2 + m_isGantryCoupled)
          ├── LogicalAxis X
          ├── PhysicalAxis X1
          └── PhysicalAxis X2
```

### 10.2 迁移策略（渐进式）

| 阶段 | 内容 | 风险 |
|------|------|------|
| Phase 1 | 新增 GantrySystem + 值对象 + 域服务，与现有 SystemContext **并行存在** | 低 |
| Phase 2 | TDD 全部 70+ 测试用例通过 | 低 |
| Phase 3 | SystemContext 通过组合 GantrySystem 替代 m_isGantryCoupled + X/X1/X2 | 中 |
| Phase 4 | 移除 SystemContext 中的龙门相关遗留代码 | 低（经测试保护） |

### 10.3 关键决策记录

| 决策 | 原因 |
|------|------|
| GantrySystem 作为独立聚合根，不完全融入 SystemContext | 龙门系统的业务约束复杂度远超普通 Axis，独立聚合更清晰 |
| SystemContext 通过 `std::optional<GantrySystem>` 持有龙门系统 | 支持无龙门配置的分组，保持容器职责单一 |
| 领域事件通过 IGantryEventBus 端口发布而非直接使用信号槽 | 保持 domain 层纯 C++，不依赖 Qt 框架 |
| PhysicalAxis 不继承现有 Axis 类 | 现有 Axis 类携带 Jog/Move/Stop 等操作语义，PhysicalAxis 仅描述状态 |

---

## 附录 A：约束-测试-组件 三维追溯矩阵

| 约束编号 | 约束名称 | 对应测试 | 对应组件 |
|----------|----------|----------|----------|
| 约束1 | 模式定义 | TC-1.1 ~ TC-1.2 | GantryMode, GantrySystem |
| 约束2 | 模式互斥 | TC-1.3 ~ TC-1.4 | GantryMode, GantryCouplingService |
| 约束3 | 联动模式语义 | TC-1.5 ~ TC-1.7 | GantrySystem, LogicalAxis |
| 约束4 | 分动模式语义 | TC-1.8 ~ TC-1.9 | GantrySystem, LogicalAxis |
| 约束5 | 方向唯一性 | TC-2.1 | MotionDirection |
| 约束6 | 方向与物理无关 | TC-2.2 ~ TC-2.4 | MotionDirection |
| 约束7 | 命令必须基于逻辑方向 | TC-2.5 ~ TC-2.7 | LogicalAxis, GantrySafetyService |
| 约束8 | 统一位置定义 | TC-3.1 | GantryPosition, LogicalAxis |
| 约束9 | 位置一致性约束 | TC-3.2 ~ TC-3.3 | PositionConsistency |
| 约束10 | 逻辑位置计算 | TC-3.4 ~ TC-3.5 | PositionConsistency, GantryStateAggregator |
| 约束11 | 位置一致性失效 | TC-3.6 ~ TC-3.7 | PositionConsistency, GantryCouplingService |
| 约束12 | 联动是"状态申请" | TC-4.1 | GantryCouplingService |
| 约束13 | 联动建立条件 | TC-4.2 ~ TC-4.7 | GantryCouplingService, CouplingCondition |
| 约束14 | 联动期间持续约束 | TC-4.8 ~ TC-4.10 | GantryCouplingService, GantryDeviationFaultEvent |
| 约束15 | 限位优先级最高 | TC-5.1 ~ TC-5.2 | GantrySafetyService |
| 约束16 | 限位后行为限制 | TC-5.3 ~ TC-5.8 | GantrySafetyService |
| 约束17 | 报警约束 | TC-5.9 ~ TC-5.11 | GantrySafetyService |
| 约束18 | 操作对象互斥 | TC-6.1 ~ TC-6.3 | GantrySystem |
| 约束19 | 运动互斥 | TC-6.4 ~ TC-6.6 | GantrySystem |
| 约束20 | 状态一致性（聚合语义） | TC-6.7 ~ TC-6.10 | GantryStateAggregator |

---

## 附录 B：TDD 实施顺序建议

按依赖关系排序，建议按以下顺序实施：

```
第1轮 (无依赖，纯值对象):
  └── GantryMode, MotionDirection, GantryPosition
  └── 测试: test_gantry_mode.cpp, test_gantry_direction.cpp (值对象部分)

第2轮 (值对象，含计算逻辑):
  └── PositionConsistency, CouplingCondition, SafetyCheckResult
  └── 测试: test_gantry_position.cpp (值对象部分)

第3轮 (实体 + 值对象):
  └── PhysicalAxis, LogicalAxis, GantrySystem, GantryEvents
  └── 测试: test_gantry_activation.cpp, test_gantry_position.cpp (实体部分)

第4轮 (域服务 + 实体):
  └── GantryStateAggregator, GantrySafetyService, GantryCouplingService
  └── 测试: test_gantry_mode.cpp (服务部分), test_gantry_coupling.cpp, test_gantry_safety.cpp

第5轮 (集成):
  └── 端口接口 + 事件总线
  └── 测试: test_gantry_system_integration.cpp, test_gantry_service_integration.cpp
```

---

> 📌 **下一阶段**: 确认本文档后，进入第1轮 TDD 实施：实现 `GantryMode`、`MotionDirection`、`GantryPosition` 三个值对象及其对应测试。
