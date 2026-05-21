# Presentation 层架构分析与设计文档

> 本文档对 servoV6 项目的 Presentation 层进行完整的架构分析，涵盖代码结构、分层设计、核心机制、数据流路径、设计模式及测试策略。

---

## 1. 目录结构总览

```
presentation/
├── CMakeLists.txt                          # 构建配置 (AUTOMOC ON)
│
├── viewmodel/                              # C++ ViewModel 层
│   ├── AxisViewModelCore.h                 # 纯 C++ ViewModel 核心（无 Qt 依赖）
│   ├── AxisViewModelCore.cpp               # -- 状态投影 + 控制转发 + Tick 驱动
│   ├── QtAxisViewModel.h                   # Qt QObject 适配层
│   └── QtAxisViewModel.cpp                 # -- Q_PROPERTY / Q_INVOKABLE / 信号发射
│
├── qml/
│   ├── views/
│   │   └── MainDashboard.qml               # 主面板总装（当前为占位符）
│   │
│   ├── blocks/                             # 功能块（组合式 UI 模块）
│   │   ├── ActionControlBlock.qml          # 动作控制面板（点动/定位双模式）
│   │   ├── AxisSelectorBlock.qml           # 轴选择列表
│   │   └── TelemetryBlock.qml              # 实时遥测看板
│   │
│   └── components/                         # 基础 UI 组件（原子级可复用部件）
│       ├── IndustrialButton.qml            # 工业风格按钮
│       ├── AxisItemDelegate.qml            # 轴列表项代理
│       └── VelocitySettingsPopup.qml       # 速度设置弹窗
│
tests/presentation/
└── viewmodel/
    └── test_axis_viewmodel_core.cpp        # ViewModel Core 集成测试
```

**构建依赖关系（来自 CMakeLists.txt）：**

```
presentation -> Qt6::Core + domain + application
```

---

## 2. 三层架构详解

### 2.1 第一层：AxisViewModelCore（纯 C++ 核心层）

**文件：** `AxisViewModelCore.h / .cpp`

**设计目的：** 在 ViewModel 层中剥离所有 Qt 依赖，使核心逻辑可被任意 C++ 框架复用，并且**可直接通过单元测试验证**（无需 Qt 运行时）。

#### 依赖注入

```cpp
AxisViewModelCore(Axis& axis, 
                  JogOrchestrator& jogOrch, 
                  AutoAbsMoveOrchestrator& absOrch,
                  AutoRelMoveOrchestrator& relOrch,
                  StopAxisUseCase& stopUc);
```

构造函数通过**引用注入** 5 个外部依赖：
| 参数 | 类型 | 来源层级 | 职责 |
|------|------|---------|------|
| `axis` | `Axis&` | Domain | 领域实体，持有轴状态与物理位置 |
| `jogOrch` | `JogOrchestrator&` | Application/Policy | 点动策略编排（自动上电 -> Jog -> 自动断电） |
| `absOrch` | `AutoAbsMoveOrchestrator&` | Application/Policy | 绝对定位策略编排 |
| `relOrch` | `AutoRelMoveOrchestrator&` | Application/Policy | 相对定位策略编排 |
| `stopUc` | `StopAxisUseCase&` | Application/Axis | 急停用例 |

#### 核心方法分类

**① 状态投影（State Projection）---- 查询方法（const）：**

```
state()        -> AxisState      # 轴状态枚举
absPos()       -> double         # 当前绝对位置
relPos()       -> double         # 当前相对位置
isEnabled()    -> bool           # 是否使能
hasError()     -> bool           # 是否错误
errorMessage() -> std::string    # 错误信息
jogVelocity()  -> double         # 点动速度
moveVelocity() -> double         # 定位速度
posLimit()     -> double         # 正限位
negLimit()     -> double         # 负限位
```

**关键设计原则：** 所有状态查询均为**直接透传**（`return m_axis.state()`），不做任何缓存，确保 ViewModel 始终反映底层最新状态。

**② 控制指令（Control Inputs）---- 修改方法：**

```
jogPositivePressed()     -> m_jogOrch.startJog(Direction::Forward)
jogPositiveReleased()    -> m_jogOrch.stopJog(Direction::Forward)
jogNegativePressed()     -> m_jogOrch.startJog(Direction::Backward)
jogNegativeReleased()    -> m_jogOrch.stopJog(Direction::Backward)
moveAbsolute(targetPos)  -> m_absOrch.start(targetPos)
moveRelative(distance)   -> m_relOrch.start(distance)
stop()                   -> m_stopUc.execute(m_axis)
setJogVelocity(v)        -> m_axis.setJogVelocity(v)
setMoveVelocity(v)       -> m_axis.setMoveVelocity(v)
```

每个控制方法都映射到一个具体的 `Application` 层策略器或用例，形成清晰的调用链。

**③ Tick 驱动（系统唯一推进入口）：**

```cpp
void tick() {
    m_jogOrch.update(m_axis);
    m_absOrch.update(m_axis);
    m_relOrch.update(m_axis);
}
```

`tick()` 是 Presentation 层唯一的**状态推进入口**，统一驱动所有 Orchestrator 的状态机流转。由外部定时器（如 QTimer）周期性调用。

#### 全链路追踪

所有控制方法均带有完整的链路追踪埋点：

```cpp
void AxisViewModelCore::moveAbsolute(double targetPos) {
    std::string traceId = generateTraceId();          // 生成 Trace ID
    TraceScope scope("G1", "Y", traceId);             // Scope 生命周期 = 操作范围
    LOG_INFO(LogLayer::UI, "AxisVM", "User requested MoveAbsolute to X");
    m_absOrch.start(targetPos);                       // 调用业务层
}
```

---

### 2.2 第二层：QtAxisViewModel（Qt 适配层）

**文件：** `QtAxisViewModel.h / .cpp`

**设计模式：** **适配器模式（Adapter Pattern）** -- 将非 Qt 的 `AxisViewModelCore` 桥接到 Qt 属性系统和 QML。

#### Q_PROPERTY 暴露

```cpp
Q_PROPERTY(int state READ state NOTIFY stateChanged)
Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)
```

#### Q_INVOKABLE 暴露

```cpp
Q_INVOKABLE void jogPositivePressed();
Q_INVOKABLE void jogPositiveReleased();
Q_INVOKABLE void jogNegativePressed();
Q_INVOKABLE void jogNegativeReleased();
Q_INVOKABLE void moveAbsolute(double targetPos);
Q_INVOKABLE void moveRelative(double distance);
Q_INVOKABLE void setJogVelocity(double v);
Q_INVOKABLE void setMoveVelocity(double v);
Q_INVOKABLE void stop();
```

所有 Q_INVOKABLE 方法均为**纯转发**，不包含任何业务逻辑。

#### 缓存节流机制（关键设计）

```cpp
void QtAxisViewModel::tick() {
    m_core->tick();  // 1. 推进底层状态机

    // 2. 状态变化检测（枚举比较）
    AxisState currentState = m_core->state();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        emit stateChanged();     // 仅状态变化时才发射信号
    }

    // 3. 位置变化检测（浮点差值 > EPSILON）
    double currentPos = m_core->absPos();
    if (std::abs(currentPos - m_lastAbsPos) > EPSILON) {
        m_lastAbsPos = currentPos;
        emit absPosChanged();    // 仅位置变化超过阈值时才发射信号
    }
}
```

**目的：** 防止 Qt 信号风暴。在 10ms 的 Tick 周期下，若每次都不加节流直接 `emit`，QML 端绑定会被频繁触发导致 UI 卡顿。

**阈值常量：**
- `EPSILON = 0.001` -- 位置变化超过 0.001 mm 才认为有变化
- 状态枚举用 `!=` 直接比较，天然节流

---

### 2.3 第三层：QML UI 层

#### 2.3.1 组件层次结构

```
MainDashboard (views/)
  ├── AxisSelectorBlock (blocks/)       -- 轴选择
  ├── ActionControlBlock (blocks/)      -- 动作控制
  │     └── IndustrialButton (components/) -- JOG ± / GO / 急停
  │     └── VelocitySettingsPopup (components/) -- 速度设置弹窗
  └── TelemetryBlock (blocks/)          -- 遥测看板
```

#### 2.3.2 组件详细分析

| 组件 | 文件 | 职责 | 关键设计 |
|------|------|------|---------|
| **IndustrialButton** | `components/IndustrialButton.qml` | 工业风格按钮 | 支持圆形/矩形、press/release/click 三重信号、`onCanceled` 触发 `released()` 防卡键 |
| **AxisItemDelegate** | `components/AxisItemDelegate.qml` | 轴列表项 | 状态灯 + 轴名 + 状态文本 + 单/双轴图标，hover 高亮 |
| **VelocitySettingsPopup** | `components/VelocitySettingsPopup.qml` | 速度设置弹窗 | 模态 Popup，打开时同步 ViewModel 当前值、保存时写回 |
| **AxisSelectorBlock** | `blocks/AxisSelectorBlock.qml` | 轴选择 | 当前仅 Y 轴可交互，Z/R/X1X2 为预留。信号通知：`axisChanged(string)` |
| **ActionControlBlock** | `blocks/ActionControlBlock.qml` | 动作控制面板 | 双模式切换（点动/定位），内置防呆逻辑 |
| **TelemetryBlock** | `blocks/TelemetryBlock.qml` | 实时遥测看板 | 状态指示灯 + 大字号位置 + 限位进度条（含动画） |
| **MainDashboard** | `views/MainDashboard.qml` | 主面板 | 当前为占位符 `Item {}`，等待总装 |

#### 2.3.3 核心设计模式

**① 属性注入（Property Injection）**

所有功能块通过 `property var viewModel` 接收外部注入的 QtAxisViewModel 实例：

```qml
// ActionControlBlock.qml
property var viewModel: null

// 使用
if(viewModel) viewModel.jogPositivePressed()
```

这种模式实现了 QML 组件与 C++ 后端的**松耦合**----组件只知有 viewModel，不知其具体类型。

**② Theme 单例**

```qml
import servoV6  // 引入 Theme 单例
```

通过 Qt QML 单例机制，统一管理：
- 颜色：`Theme.bgDark` / `Theme.textMain` / `Theme.colorIdle` / `Theme.colorError` / `Theme.colorMoving`
- 字体：`Theme.fontSmall` / `Theme.fontNormal` / `Theme.fontLarge` / `Theme.fontGiant`
- 间距：`Theme.scale`（UI 自适应缩放因子）

**③ 防呆设计（Foolproof）**

ActionControlBlock 中的核心防呆逻辑：

```qml
// 仅当轴状态为 Disabled(1) 或 Idle(2) 时才允许下发定位指令
property bool isReadyForPos: viewModel ? (viewModel.state === 1 || viewModel.state === 2) : false
```

该属性控制：
- RadioButton（绝对/相对选择）的 `enabled`
- TextField（目标值输入）的 `enabled`
- 执行按钮的 `enabled` 和文本（"执行 GO" / "运行中..."）

**④ 限位进度条（Position Bar）**

TelemetryBlock 中的进度条实现：

```
负限位 ────────●─────────────────────── 正限位
               ↑ 当前位置
```

通过 readonly property 计算进度比例，QML 引擎自动追踪依赖并触发重绘：

```qml
readonly property double progressRatio: {
    let range = safePLim - safeNLim;
    if (range <= 0) return 0.5;
    return Math.max(0.0, Math.min(1.0, (safePos - safeNLim) / range));
}
```

附带 `Behavior on width { NumberAnimation { duration: 100 } }` 使进度条动画丝滑。

---

## 3. 数据流路径

### 3.1 用户操作 -> 物理动作（正向路径）

```
用户点击 "JOG +" 按钮
  ↓ onPressed 信号
ActionControlBlock.qml
  ↓ viewModel.jogPositivePressed()
QtAxisViewModel::jogPositivePressed()
  ↓ 委托调用
AxisViewModelCore::jogPositivePressed()
  ↓ LOG_INFO + TraceScope 追踪
  ↓ m_jogOrch.startJog(Direction::Forward)
JogOrchestrator / EnableUseCase / JogAxisUseCase
  ↓
FakePLC / FakeAxisDriver（硬件抽象层）
  ↓ 物理世界推进
plc.tick(TICK_MS)
  ↓ 传感器反馈同步
AxisSyncService::sync(axis, plc.getFeedback())
  ↓ 下一轮 tick()
QtAxisViewModel::tick()
  ↓ 状态/位置变化检测 -> emit signal
QML 绑定更新 UI
```

### 3.2 Tick 循环（系统心跳）

```
QTimer (10ms)
  │
  ├─-> QtAxisViewModel::tick()
  │      ├─-> AxisViewModelCore::tick()       ← 推进所有策略器
  │      └─-> 检测变化 -> emit 信号             ← 通知 QML 更新
  │
  ├─-> plc.tick(10ms)                          ← 推进物理世界
  │
  └─-> AxisSyncService::sync()                 ← 同步传感器反馈
```

---

## 4. 测试策略分析

**文件：** `tests/presentation/viewmodel/test_axis_viewmodel_core.cpp`

### 4.1 测试架构

```cpp
class AxisViewModelCoreTest : public ::testing::Test {
protected:
    FakePLC plc;                        // 物理引擎模拟
    FakeAxisDriver driver{plc};         // 驱动模拟
    AxisSyncService syncService;        // 同步服务
    Axis axis;                          // 领域实体
    // ... UseCase / Orchestrator ...
    std::unique_ptr<AxisViewModelCore> vm;  // 被测对象
};
```

### 4.2 核心测试工具

**时间推进器（advanceTime）：**

```cpp
void advanceTime(int totalMs) {
    while (elapsed < totalMs) {
        vm->tick();                                // 1. 驱动策略层
        plc.tick(TICK_MS);                         // 2. 物理引擎推进
        syncService.sync(axis, plc.getFeedback()); // 3. 传感器同步
        elapsed += TICK_MS;
    }
}
```

**条件等待器（waitUntil）：**

```cpp
bool waitUntil(std::function<bool()> condition, int timeoutMs = 5000) {
    while (elapsed < timeoutMs) {
        if (condition()) return true;
        advanceTime(TICK_MS);
    }
    return false;
}
```

### 4.3 测试用例

| 测试 | 场景 | 验证点 |
|------|------|--------|
| `ShouldReflectInitialDisabledState` | 初始状态 | 初始状态为 `Disabled`，位置为 0 |
| `ShouldExecuteJogPositiveRealistically` | 完整点动生命周期 | 按下->进入 Jogging->位移->松开->自动断电->不漂移 |
| `ShouldCompleteAbsoluteMoveRealistically` | 完整绝对定位生命周期 | 启动->MovingAbsolute->到达->自动断电->精准到达目标 |
| `ShouldHaltImmediatelyWhenStopPressed` | 急停打断 | 运动中被停->进入 Disabled->位置截断在半路 |

**关键测试设计原则：** 测试验证的是 `AxisViewModelCore` 的**对外行为**而非内部实现----测试只断言状态和位置的变化，不关心内部分支细节。

---

## 5. 设计模式总结

| 模式 | 应用位置 | 作用 |
|------|---------|------|
| **Adapter（适配器）** | `QtAxisViewModel` 封装 `AxisViewModelCore` | 桥接纯 C++ 核心与 Qt 属性系统 |
| **Command（命令）** | 所有控制方法转发到 UseCase/Orchestrator | 将 UI 操作封装为命令对象 |
| **Mediator（中介者）** | `AxisViewModelCore` 作为中介 | 协调多个 Orchestrator 的交互与调度 |
| **Observer（观察者）** | Qt 信号 -> QML 绑定 | 状态变化自动通知 UI 更新 |
| **Strategy（策略）** | 不同的 Orchestrator 代表不同运动策略 | Jog / Abs / Rel 可替换 |
| **Singleton（单例）** | `Theme` QML 单例 | 统一 UI 风格管理 |
| **Property Injection（属性注入）** | QML 功能块通过 `viewModel` 属性注入 | 松耦合 C++/QML 桥接 |

---

## 6. 当前状态与后续建议

### 当前状态
- ✅ ViewModel Core 完整实现（状态投影 + 控制指令 + Tick 驱动）
- ✅ Qt 适配层完整实现（Q_PROPERTY + Q_INVOKABLE + 缓存节流）
- ✅ 基础 UI 组件完整（按钮、列表项、弹窗）
- ✅ 核心功能块完整（轴选择、动作控制、遥测看板）
- ✅ 集成测试完整（覆盖点动、定位、急停全生命周期）
- ❌ **MainDashboard 为占位符**，尚未组装各 Block
- ❌ **Theme 单例** 的实现文件未在 presentation 目录中（可能在更上层或由 Qt 资源系统提供）
- ❌ **QtAxisViewModel 的单元测试** 尚未编写（当前只有 Core 的集成测试）

### 建议改进
1. **完成 MainDashboard 总装**：将 AxisSelectorBlock / ActionControlBlock / TelemetryBlock 组装进 MainDashboard
2. **补充 QtAxisViewModel 测试**：编写单元测试覆盖信号发射逻辑和缓存节流机制
3. **Theme 定义迁移**：将 Theme 定义明确化，确保所有颜色/字体/间距有统一来源
4. **多轴支持**：当前架构强耦合单轴，后续需支持多轴 ViewModel 的创建与管理
5. **错误处理 UI**：ViewModel Core 已有 `hasError()` / `errorMessage()`，但 QML 层尚未展示错误状态
