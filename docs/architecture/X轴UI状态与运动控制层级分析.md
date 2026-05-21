# X 轴 UI 状态展示与运动控制层级全链路分析

## 概述

本文档分析从 UI 按钮按下到实际运动控制的完整数据流与状态映射链路，涵盖：
1. X 轴的**龙门联动/耦合状态**在 UI 层的展示逻辑
2. **点动 (Jog)** 与**定位移动 (Position)** 的按钮触发 → 编排器状态机 → 领域层 → 驱动下发的完整路径
3. 各层级之间的状态投影与信号通知机制

---

## 目录

1. [UI 层：状态展示](#1-ui-层状态展示)
2. [ViewModel 层：状态投影与控制入口](#2-viewmodel-层状态投影与控制入口)
3. [Application 层：编排器状态机](#3-application-层编排器状态机)
4. [Domain 层：领域实体与控制器](#4-domain-层领域实体与控制器)
5. [Infrastructure 层：驱动通讯](#5-infrastructure-层驱动通讯)
6. [完整链路时序](#6-完整链路时序)
7. [信号节流机制](#7-信号节流机制)
8. [安全锁定与错误传播](#8-安全锁定与错误传播)

---

## 1. UI 层：状态展示

### 1.1 AxisSelectorBlock.qml — 轴选择与龙门状态

**负责展示**：
- 6 个轴的选中/激活状态
- X 轴（逻辑龙门轴）的耦合状态指示灯与文字
- X1/X2 物理轴是否受龙门控制

**关键属性投影**（从 `GantryViewModel` 读取）：

| 属性 | 类型 | 来源 | 含义 |
|------|------|------|------|
| `gantryViewModel.isCoupled` | bool | GantryCouplingController | 龙门已耦合 |
| `gantryViewModel.isOrchestratorBusy` | bool | GantryOrchestrator | 编排器过渡中 |
| `gantryViewModel.isDecoupledAndEnabled` | bool | Power + Coupling 合成 | 使能+解耦(状态C) |

**状态文字映射**：
```
isCouplingTransition → "耦合中..." (warning 色)
isCoupled → "耦合 · 控制中" (idle 色)
否则 → "已解耦" (dim 色)
```

**物理轴可用性规则**：
```
physicalAxesAvailable = !gantryViewModel.isCoupled
```
- 耦合时 X1/X2 显示 "↳ 龙门" 标记子标签
- 解耦时 X1/X2 恢复正常可独立操作

### 1.2 ActionControlBlock.qml — 操作控制面板

**负责**：
- 模式切换（点动/定位）
- 点动按钮按下/释放
- 定位目标输入与执行
- 龙门锁定状态屏蔽
- 系统急停状态显示

**操作锁定逻辑**：

```
gantryOperationLocked =
    (currentAxis === "X" && !isCoupled) → "龙门未耦合"
    || (currentAxis ∈ {"X1","X2"} && isCoupled) → "受龙门控制"
```

**按钮 enable 条件**：

| 操作 | 条件 |
|------|------|
| `jogEnabled` | !systemLocked && viewModel != null && !gantryOperationLocked |
| `isReadyForPos` | !systemLocked && !gantryOperationLocked && (!hasError && state ≤ 2) |

### 1.3 IndustrialButton.qml — 按钮事件

**点动按钮**：
```qml
onPressed:  viewModel.jogPositivePressed()   // 按下 → 触发正向点动
onReleased: viewModel.jogPositiveReleased()   // 释放 → 停止正向点动
```

**定位按钮**：
```qml
onClicked: viewModel.moveAbsolute(target)     // 绝对定位
onClicked: viewModel.moveRelative(target)     // 相对定位
```

---

## 2. ViewModel 层：状态投影与控制入口

### 2.1 QtAxisViewModel — QML 接口层

**职责**：将 `AxisViewModelCore` 的纯 C++ 接口包装为 Q_PROPERTY + Q_INVOKABLE，供 QML 绑定。

**Q_PROPERTY 属性**（用于 UI 绑定）：

| 属性 | 含义 |
|------|------|
| `state` | 轴状态枚举值（0=Unknown, 1=Disabled, 2=Idle, 3=Jogging, 4=MovingAbsolute, 5=MovingRelative, 6=Error） |
| `absPos` | 绝对位置 |
| `relPos` | 相对位置 |
| `isEnabled` | 是否使能（state != Disabled） |
| `stateText` | 状态文本 |
| `hasError` | 是否有错误 |
| `jogVelocity` | 点动速度 |
| `moveVelocity` | 定位速度 |

**Q_INVOKABLE 方法**（UI 调用入口）：

| 方法 | 路由目标 |
|------|----------|
| `jogPositivePressed()` | AxisViewModelCore::jog(Direction::Forward) |
| `jogPositiveReleased()` | AxisViewModelCore::jogStop(Direction::Forward) |
| `jogNegativePressed()` | AxisViewModelCore::jog(Direction::Backward) |
| `jogNegativeReleased()` | AxisViewModelCore::jogStop(Direction::Backward) |
| `moveAbsolute(targetPos)` | AxisViewModelCore::moveAbsolute(targetPos) |
| `moveRelative(distance)` | AxisViewModelCore::moveRelative(distance) |
| `enable()` / `disable()` | AxisViewModelCore::enable(true/false) |
| `stop()` | AxisViewModelCore::stop() |

### 2.2 AxisViewModelCore — 核心状态机枢纽

**内部架构**：

```
AxisViewModelCore
├── SystemManager&         ← 系统管理器（获取 SystemContext）
├── Use Cases              ← 单次操作 UseCase
│   ├── EnableUseCase
│   ├── JogAxisUseCase
│   ├── MoveAbsoluteUseCase
│   ├── MoveRelativeUseCase
│   └── StopAxisUseCase
├── Orchestrators          ← 流程编排器
│   ├── JogOrchestrator    ← 点动（自动使能→点动→停止→掉电）
│   ├── AutoAbsMoveOrchestrator  ← 绝对定位（自动使能→运动→等待→掉电）
│   └── AutoRelMoveOrchestrator  ← 相对定位（同上）
├── ErrorHistory           ← 错误列表（追加模式）
└── tick()                 ← 帧驱动入口
```

**控制指令路由**：

```
jog(dir)        → JogOrchestrator::startJog(id, dir)
jogStop(dir)    → JogOrchestrator::stopJog(id, dir)
moveAbsolute(t) → AutoAbsMoveOrchestrator::startAbs(id, target)
moveRelative(d) → AutoRelMoveOrchestrator::startRel(id, distance)
stop()          → StopAxisUseCase::execute() + JogOrchestrator::stopJog()
```

**`tick()` 帧逻辑**：
```
1. m_jogOrch->tick()    — 点动编排器推进
2. m_absOrch->tick()    — 绝对定位编排器推进
3. m_relOrch->tick()    — 相对定位编排器推进
4. collectOrchError()   — 收集编排器错误（追加到错误历史）
5. consumePendingCommands() — 消费零位/速度类 pending command
```

### 2.3 GantryViewModel — 龙门状态投影

**Q_PROPERTY 属性**：

| 属性 | 计算方式 |
|------|----------|
| `isEnabled` | m_cachedEnabled ← powerController.isEnabled() |
| `isCoupled` | m_cachedCoupled ← couplingController.isCoupled() |
| `isDecoupledAndEnabled` | enabled && !coupled && synchronized |
| `isOrchestratorBusy` | 编排器 step ∈ (EnsuringEnabled ~ WaitingDisabled) |
| `orchestratorStepText` | step 对应中文文本 |

**`tick()` 帧逻辑**：
```
1. advanceOrchestrator()       — 推进 GantryOrchestrator
2. refreshGantryState()        — 刷新龙门状态缓存 + 按需 emit
3. refreshOrchestratorState()  — 刷新编排器状态缓存 + 按需 emit
```

---

## 3. Application 层：编排器状态机

### 3.1 JogOrchestrator — 点动编排器

**状态机流程图**：

```
Idle
  │  startJog(id, dir)
  ▼
EnsuringEnabled  ──[轴 Disabled]──> 下发 Enable 命令
  │                                   │
  │  [轴 已 Idle]                     │
  ▼                                   │
IssuingJog       ──[领域校验通过]──> 下发 JogCommand
  │                                   │
  ▼                                   │
Jogging          ──[轴意外 Idle]──> IssuingStop
  │  stopJog()                        │
  ▼                                   │
IssuingStop      ──[下发停止]────>     │
  ▼                                   │
WaitingForIdle   ──[轴回到 Idle]──>   │
  ▼                                   │
EnsuringDisabled ──[下发掉电]────>     │
  ▼                                   │
Done ◄────────────────────────────────┘
  │  Error
  ▼
Error
```

**关键行为**：
- **自动管理使能/掉电**：开始前自动使能，结束后自动掉电
- **安全锁定优雅中止**：急停时从非终态直接跳转到 `Done`（而非 `Error`），确保急停恢复后可重新启动
- **AxisId + Direction 双重防误杀**：`stopJog(id, dir)` 校验 id 和 dir 都匹配才生效

### 3.2 AutoAbsMoveOrchestrator — 绝对定位编排器

**状态机流程图**：

```
Initial
  │  startAbs(id, target)
  ▼
EnsuringEnabled  ──[使能完成]──> 
  ▼
IssuingMove      ──[下发 MoveAbsoluteUseCase]──>
  ▼
WaitingMotionStart  ──[轴状态变为 MovingAbsolute 或位置变化]──>
  ▼
WaitingMotionFinish ──[isMoveCompleted() && 目标位置到达]──>
  ▼
Done
  │
  ▼
Error  (目标未到达 / 轴 Error / 通讯失败)
```

**关键行为**：
- **运动开始检测**：通过状态变为 `MovingAbsolute` 或位置变化 `> 0.01mm` 或 `isMoveCompleted()` 三路判断
- **到达校验**：完成时校验当前位置与目标的偏差 `< 0.01mm`
- **失败熔断**：Move 被拒后自动掉电

### 3.3 GantryOrchestrator — 龙门编排器

**状态机流程图**（三种流程）：

```
# 流程 A：startCoupling() — 联动
EnsuringEnabled → WaitingEnabled → Coupling → WaitingCoupled → Done

# 流程 B：stopCouplingAndDisable() — 解除联动+掉电
Decoupling → WaitingDecoupled → Disabling → WaitingDisabled → Done

# 流程 C：enableAndDecouple() — 使能并解耦
EnsuringEnabled → WaitingEnabled → Decoupling → WaitingDecoupled → Done
```

**关键行为**：
- `EnsuringEnabled` → 调用 `power.requestEnable(true)` → 下发 `GantryPowerCommand`
- `Coupling` → 调用 `coupling.requestCouple(true)` → 下发 `GantryCouplingCommand`
- `WaitingCoupled` → 监听 `coupling.isCoupled()` 和 `coupling.hasError()`
- 错误传播：`GantryRejection` → `UseCaseError` → `ViewModelError`

---

## 4. Domain 层：领域实体与控制器

### 4.1 Axis 领域实体

**状态枚举**：
```
Unknown(0) → Disabled(1) → Idle(2) → Jogging(3) / MovingAbsolute(4) / MovingRelative(5) / Error(6)
```

**命令槽位 (`AxisCommand` variant)**：
```
JogCommand            — 点动方向 + 激活标志
MoveCommand           — 类型(Abs/Rel) + 目标值 + 起点快照
StopCommand           — 停止
EnableCommand         — 使能/掉电
SetJogVelocityCommand — 设置点动速度
SetMoveVelocityCommand— 设置定位速度
ZeroAbsoluteCommand   — 绝对零位
SetRelativeZeroCommand— 设置相对零位
ClearRelativeZeroCommand— 清除相对零位
```

**核心拒绝原因**：
```
None, InvalidState, AlreadyMoving,
TargetOutOfPositiveLimit, TargetOutOfNegativeLimit,
AtPositiveLimit, AtNegativeLimit,
UnknownError, InvalidArgument
```

### 4.2 GantryCouplingController — 龙联动状态机

**五态流转**：
```
NotSynchronized ──[applyFeedback]──→ Coupled / Decoupled
    │
    ├── Decoupled ──[requestCouple(true)]──→ CouplingRequested ──[PLC 确认]──→ Coupled
    └── Coupled  ──[requestCouple(false)]──→ DecouplingRequested ──[PLC 确认]──→ Decoupled
```

**错误码映射**：
```
PLC Error 0 → GantryRejection::None
PLC Error 1 → PositionToleranceExceeded
PLC Error 2 → X1NotEnabled
PLC Error 3 → X2NotEnabled
PLC Error 4 → X1NotStationary
PLC Error 5 → X2NotStationary
```

### 4.3 GantryPowerController — 龙门电机电源状态机

**五态流转**：
```
NotSynchronized ──[applyFeedback]──→ Enabled / Disabled
    │
    ├── Disabled ──[requestEnable(true)]──→ Enabling ──[PLC 确认]──→ Enabled
    └── Enabled  ──[requestEnable(false)]──→ Disabling ──[PLC 确认]──→ Disabled
```

---

## 5. Infrastructure 层：驱动通讯

### 5.1 命令下发路径

```
ViewModel/QML
  │  Q_INVOKABLE
  ▼
AxisViewModelCore
  │  orchestrator::startXxx() / useCase::execute()
  ▼
Orchestrator / UseCase
  │  getContext() → tryGetAxis()
  │  axis->jog() → 领域校验 → 产生 AxisCommand
  │  axis->getPendingCommand()
  ▼
drv->send(AxisCommandWithId{axisId, command})
  │  ISystemDriver::send()
  ▼
FakePLC / 实际硬件
  │  通讯协议层
  ▼
PLC 执行
```

### 5.2 反馈接收路径

```
PLC
  │  pollFeedback() 轮询
  ▼
SystemContext::applyFeedback()
  │  Axis::applyFeedback() — 更新状态/位置
  │  GantryPowerController::applyFeedback() — 更新使能状态
  │  GantryCouplingController::applyFeedback() — 更新耦合状态
  ▼
下一次 tick() 时
  │  AxisViewModelCore::tick() 读取更新
  │  GantryViewModel::tick() 读取更新
  ▼
Q_PROPERTY 变化 → NOTIFY 信号 → QML 绑定的 UI 刷新
```

---

## 6. 完整链路时序

### 6.1 点动正向链路

```
时序方向: UI → ViewModel → Application → Domain → Infrastructure
┌─────────────────────────────────────────────────────────────────────┐
│ QML: IndustrialButton::onPressed                                  │
│   → viewModel.jogPositivePressed()                                │
├─────────────────────────────────────────────────────────────────────┤
│ QtAxisViewModel::jogPositivePressed()                             │
│   → m_core->jog(Direction::Forward)                                │
├─────────────────────────────────────────────────────────────────────┤
│ AxisViewModelCore::jog(Direction::Forward)                        │
│   → m_jogOrch->startJog(m_axisId, Direction::Forward)             │
│   (如果编辑错误, collectOrchError)                                 │
├─────────────────────────────────────────────────────────────────────┤
│ JogOrchestrator::startJog(X1, Forward)                            │
│   m_step = EnsuringEnabled;                                        │
│   (下一次 tick 时开始推进)                                          │
├─────────────────────────────────────────────────────────────────────┤
│ tick() 第 N 帧: EnsuringEnabled                                   │
│   → EnableUseCase::execute(manager, group, X1, true)              │
│   → Axis::enable(true) → 生成 EnableCommand{true}                 │
│   → drv->send(EnableCommand)                                      │
├─────────────────────────────────────────────────────────────────────┤
│ tick() 第 N+M 帧: 轴已 Idle → IssuingJog                          │
│   → Axis::jog(Forward) → 领域校验通过 → 生成 JogCommand{Forward}  │
│   → axis->getPendingCommand() → drv->send(JogCommand{X1,Forward}) │
│   → m_step = Jogging                                               │
├─────────────────────────────────────────────────────────────────────┤
│ 用户松开按钮                                                       │
├─────────────────────────────────────────────────────────────────────┤
│ QML: IndustrialButton::onReleased                                  │
│   → viewModel.jogPositiveReleased()                                │
├─────────────────────────────────────────────────────────────────────┤
│ AxisViewModelCore::jogStop(Direction::Forward)                    │
│   → m_jogOrch->stopJog(m_axisId, Direction::Forward)               │
├─────────────────────────────────────────────────────────────────────┤
│ JogOrchestrator::stopJog(X1, Forward)                             │
│   匹配成功 → m_step = IssuingStop                                  │
├─────────────────────────────────────────────────────────────────────┤
│ tick() 下一帧: IssuingStop                                        │
│   → Axis::stopJog(Forward) → 生成 JogCommand{Forward,active=false} │
│   → drv->send(StopJogCommand)                                      │
│   → m_step = WaitingForIdle                                         │
├─────────────────────────────────────────────────────────────────────┤
│ tick() 下一帧: 轴已 Idle → EnsuringDisabled                        │
│   → EnableUseCase::execute(manager, group, X1, false)              │
│   → Axis::enable(false) → EnableCommand{false}                     │
│   → drv->send(DisableCommand)                                       │
├─────────────────────────────────────────────────────────────────────┤
│ tick() 下一帧: 轴已 Disabled → Done                                │
└─────────────────────────────────────────────────────────────────────┘
```

### 6.2 绝对定位链路

```
QML: IndustrialButton::onClicked → viewModel.moveAbsolute(100.0)
  → AxisViewModelCore::moveAbsolute(100.0)
    → m_absOrch->startAbs(m_axisId, 100.0)
      → m_step = EnsuringEnabled

tick() 帧序列:
  [EnsuringEnabled] → EnableUseCase → 等待 Idle
  [IssuingMove]     → MoveAbsoluteUseCase::execute(manager, group, X1, 100.0)
                    → Axis::moveAbsolute(100.0) → 领域校验 → MoveCommand{Abs,100.0}
                    → drv->send(MoveCommand{X1,Abs,100.0})
                    → 记录 m_startPos = 当前位置
  [WaitingMotionStart] → 检测 state==MovingAbsolute 或 pos 变化 > 0.01mm
  [WaitingMotionFinish] → isMoveCompleted() && abs(pos - target) < 0.01mm
                       → EnableUseCase(false) → 掉电
                       → Done
```

### 6.3 龙门联动链路

```
QML: 调用 gantryViewModel.startCoupling()
  → GantryViewModel::startCoupling()
    → 创建 GantryOrchestrator
    → m_orchestrator->startCoupling()
      → m_step = EnsuringEnabled

tick() 帧序列（GantryViewModel::tick() → advanceOrchestrator()）:
  [EnsuringEnabled] → power.requestEnable(true) → GantryPowerCommand{true}
                    → drv->send(power.popPendingCommand())
  [WaitingEnabled]  → power.isEnabled() == true → 跳转到 Coupling
  [Coupling]        → coupling.requestCouple(true) → GantryCouplingCommand{true}
                    → drv->send(coupling.popPendingCommand())
  [WaitingCoupled]  → coupling.isCoupled() == true → Done
                    → 或 coupling.hasError() → Error

GantryViewModel::refreshGantryState() — 每帧刷新:
  m_cachedEnabled  = power.isEnabled()
  m_cachedCoupled  = coupling.isCoupled()
  m_cachedDecoupledAndEnabled = enabled && !coupled && synchronized

GantryViewModel::refreshOrchestratorState() — 每帧刷新:
  m_cachedOrchestratorBusy   = step ∈ {EnsuringEnabled..WaitingDisabled}
  m_cachedOrchestratorStepText = "Enabling gantry motors..." / "Coupling..." / 等
```

---

## 7. 信号节流机制

### 7.1 QtAxisViewModel 节流

```cpp
// 每帧 tick() 中比较缓存值，仅在变化时 emit
if (m_lastState != core->state()) {
    m_lastState = core->state();
    emit stateChanged();
}
// 同理: absPos, relPos, jogVelocity, moveVelocity, hasError
```

### 7.2 GantryViewModel 节流

```cpp
// refreshGantryState() — 4 个布尔值缓存对比
if (m_cachedEnabled != enabled) { ... changed = true; }
if (m_cachedCoupled != coupled) { ... changed = true; }
// ... 仅在任一变化时 emit gantryStateChanged()

// refreshOrchestratorState() — 2 个值缓存对比
if (m_cachedOrchestratorBusy != busy) { ... changed = true; }
if (m_cachedOrchestratorStepText != stepText) { ... changed = true; }
// ... 仅在任一变化时 emit orchestratorStateChanged()
```

---

## 8. 安全锁定与错误传播

### 8.1 三层安全锁定

```
Layer 0: EmergencyStopController::isSystemLocked()
  ├── 影响：所有编排器的 tick() 第一道检查
  ├── 行为：急停时编排器优雅中止到 Done
  └── 影响：UI 层 systemLocked → 禁用所有操作按钮

Layer 1: ContextRejection（SystemContext）
  ├── 场景：龙门语义拦截（X 轴只能通过逻辑 X 控制）
  └── 错误码：包含 GantryCoupled / GantryDecoupled

Layer 2: Axis::RejectionReason（领域语义）
  ├── 场景：轴状态非法、限位拦截、运动中冲突
  └── 错误码：InvalidState / AlreadyMoving / AtPositiveLimit 等
```

### 8.2 错误传播路径

```
Axis::lastRejection()
    │
    ▼
UseCaseError variant
    ├── std::monostate → 无错误
    ├── ContextRejection → 上下文拦截
    ├── RejectionReason → 领域拒绝
    ├── CommunicationResult → 通讯失败
    └── GantryRejection → 龙门拒绝
    │
    ▼
ErrorTranslator::translate()
    │
    ▼
ViewModelError
    ├── code: "ENABLE_FAILED"/"JOG_REJECTED"/"TARGET_OUT_OF_LIMIT" 等
    ├── displayMessage: 中文错误提示
    ├── debugMessage: 详细调试信息
    └── category: Modal（模态）/ Transient（瞬态）
    │
    ▼
AxisViewModelCore::pushError() → m_errorHistory.push_back(...)
    │
    ▼
QtAxisViewModel::hasError / errorCode / errorMessage
    │
    ▼
QML: ActionControlBlock → 错误面板展示
```

---

## 9. 总结：UI 状态与底层状态的全映射

| UI 展示项 | 数据源 | 底层实体 | 刷新方式 |
|-----------|--------|----------|----------|
| 轴状态文字 (stateText) | QtAxisViewModel::stateText | Axis::state() | tick() 帧轮询 |
| 绝对位置 (absPos) | QtAxisViewModel::absPos | Axis::currentAbsolutePosition() | tick() 帧轮询 |
| 相对位置 (relPos) | QtAxisViewModel::relPos | Axis::currentRelativePosition() | tick() 帧轮询 |
| 点动速度 | QtAxisViewModel::jogVelocity | Axis::getjogVelocity() | tick() 帧轮询 |
| 龙门耦合状态 | GantryViewModel::isCoupled | GantryCouplingController::isCoupled() | tick() 帧轮询 |
| 龙门使能状态 | GantryViewModel::isEnabled | GantryPowerController::isEnabled() | tick() 帧轮询 |
| 编排器忙状态 | GantryViewModel::isOrchestratorBusy | GantryOrchestrator::currentStep() | tick() 帧轮询 |
| 编排器步骤文字 | GantryViewModel::orchestratorStepText | GantryOrchestrator::currentStep() | tick() 帧轮询 |
| 点动按钮 enable | ActionControlBlock.jogEnabled | 多层合成（安全+龙门+ViewModel） | 属性绑定 |
| 定位按钮 enable | ActionControlBlock.isReadyForPos | 多层合成（安全+龙门+错误+状态） | 属性绑定 |
| 错误提示 | QtAxisViewModel::errorMessage | AxisViewModelCore::m_errorHistory | 信号 errorChanged |

**数据流模式**：

```
PLC 物理状态
  │  pollFeedback() 帧轮询
  ▼
Domain 实体 (Axis / GantryPowerController / GantryCouplingController)
  │  applyFeedback()
  ▼
Application 编排器 (JogOrch / AutoAbsOrch / GantryOrch)
  │  tick() 帧驱动
  ▼
ViewModel (AxisViewModelCore / GantryViewModel)
  │  tick() → 缓存对比 → emit NOTIFY 信号
  ▼
QML UI (AxisSelectorBlock / ActionControlBlock)
  │  属性绑定 → 自动刷新
  ▼
用户看到的最终 UI
