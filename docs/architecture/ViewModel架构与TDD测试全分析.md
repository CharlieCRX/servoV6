# AxisViewModel 架构与 TDD 测试全分析

> **分析日期:** 2026-05-17  
> **分析范围:** `presentation/viewmodel/` (AxisViewModelCore, QtAxisViewModel, ErrorTranslator, ViewModelError) + 对应的 TDD 测试文件 (`tests/presentation/viewmodel/`)
>
> **本文档从"代码实现"和"TDD 测试"两个维度切入，对 ViewModel 层的架构设计、分层职责、数据流、错误处理、测试策略进行系统性分析。**

---

## 目录

1. [ViewModel 架构总览](#1-viewmodel-架构总览)
2. [AxisViewModelCore：纯 C++ 核心层深度分析](#2-axisviewmodelcore纯-c-核心层深度分析)
3. [QtAxisViewModel：Qt 适配层设计分析](#3-qtaxisviewmodelqt-适配层设计分析)
4. [错误处理子系统：ErrorTranslator + ViewModelError](#4-错误处理子系统errortranslator--viewmodelerror)
5. [TDD 测试策略与实现分析](#5-tdd-测试策略与实现分析)
6. [数据流全链路追踪](#6-数据流全链路追踪)
7. [设计模式汇总](#7-设计模式汇总)
8. [架构演进分析：重构前后的对比](#8-架构演进分析重构前后的对比)
9. [存在缺陷与改进建议](#9-存在缺陷与改进建议)
10. [总结](#10-总结)

---

## 1. ViewModel 架构总览

### 1.1 分层架构图（重构后现状）

```
┌──────────────────────────────────────────────────────────────────┐
│  QML 呈现层                                                      │
│  MainDashboard → AxisSelectorBlock / ActionControlBlock /        │
│                  TelemetryBlock                                  │
└──────────────────────────┬───────────────────────────────────────┘
                           │ Q_PROPERTY 绑定 / Q_INVOKABLE 调用
                           │ Qt Signals (stateChanged, absPosChanged...)
┌──────────────────────────▼───────────────────────────────────────┐
│  Qt 适配层 (QtAxisViewModel)                                     │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  · 缓存节流 (Cache & Throttle)                              │ │
│  │  · EPSILON 阈值比较 (0.001)                                 │ │
│  │  · 纯转发：Q_INVOKABLE → AxisViewModelCore::xxx()          │ │
│  └───────────┬─────────────────────────────────────────────────┘ │
└──────────────┼──────────────────────────────────────────────────┘
               │ 委托调用
┌──────────────▼──────────────────────────────────────────────────┐
│  纯 C++ ViewModel 核心 (AxisViewModelCore)                      │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │  · 状态投影 (State Projection)                              ││
│  │  · 控制转发 (Control Forwarding)                            ││
│  │  · Tick 驱动 (Frame Tick Driver)                            ││
│  │  · 错误收集 (Error Collection)                              ││
│  └───┬───┬───┬───┬───┬───┬───┬───┬────────────────────────────┘│
└──────┼───┼───┼───┼───┼───┼───┼───┼──────────────────────────────┘
       │   │   │   │   │   │   │   │
       ▼   ▼   ▼   ▼   ▼   ▼   ▼   ▼
  ┌────┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌─────┐ ┌──────┐ ┌──────┐
  │Ena-│ │Jog│ │Abs│ │Rel│ │Stop│ │Jog  │ │Abs   │ │Rel   │
  │ble │ │Use│ │Use│ │Use│ │Use│ │Orch │ │Orch  │ │Orch  │
  │UC  │ │Case│ │Case│ │Case│ │Case│ │     │ │      │ │      │
  └─┬──┘ └─┬─┘ └─┬─┘ └─┬─┘ └─┬─┘ └──┬──┘ └──┬───┘ └──┬───┘
    │      │     │     │     │      │        │        │
    └──────┴─────┴─────┴─────┴──────┴────────┴────────┘
                          │
                    Application 层 (UseCases + Orchestrators)
                          │
               ┌──────────┴──────────┐
               ▼                     ▼
        Domain 层 (Axis,        Infrastructure 层
        SystemContext,          (ISystemDriver,
        Gantry, Safety)          FakePLC, FakeAxisDriver)
```

### 1.2 文件职责矩阵

| 文件 | 层级 | 职责 | 依赖 |
|------|------|------|------|
| `AxisViewModelCore.h/.cpp` | Core | 纯 C++ 核心：状态投影 + 控制转发 + Tick 驱动 + 错误收集 | SystemManager, UseCases, Orchestrators, Axis |
| `QtAxisViewModel.h/.cpp` | Qt Adapter | Qt 适配：Q_PROPERTY 映射 + Q_INVOKABLE 转发 + 信号发射 + 缓存节流 | AxisViewModelCore, QObject |
| `ErrorTranslator.h/.cpp` | Translation | UseCaseError → ViewModelError 翻译函数 | UseCaseError, ViewModelError |
| `ViewModelError.h` | Data | 错误结构体定义 (code, userMessage, debugMessage, category) | (独立头文件) |

### 1.3 构建依赖

```
presentation/  ──→  domain/ (Axis, SystemContext, AxisId...)
             ──→  application/ (UseCases, Orchestrators)
             ──→  infrastructure/ (ISystemDriver — 仅 CommunicationResult)
             ──→  Qt6::Core (仅 QtAxisViewModel)
```

**关键约束：** `AxisViewModelCore` 不依赖 Qt，使得其核心逻辑可被任意 C++ 框架复用，且可直接通过 Google Test 验证（无需 Qt 运行时）。

---

## 2. AxisViewModelCore：纯 C++ 核心层深度分析

### 2.1 构造语义

```cpp
AxisViewModelCore::AxisViewModelCore(SystemManager& manager,
                                     const std::string& groupName,
                                     AxisId axisId)
    : m_manager(manager)
    , m_groupName(groupName)
    , m_axisId(axisId)
    , m_enableUc(std::make_unique<EnableUseCase>())
    , m_jogUc(std::make_unique<JogAxisUseCase>())
    , m_moveAbsUc(std::make_unique<MoveAbsoluteUseCase>())
    , m_moveRelUc(std::make_unique<MoveRelativeUseCase>())
    , m_stopUc(std::make_unique<StopAxisUseCase>())
    , m_jogOrch(std::make_unique<JogOrchestrator>(manager, groupName))
    , m_absOrch(std::make_unique<AutoAbsMoveOrchestrator>(manager, groupName))
    , m_relOrch(std::make_unique<AutoRelMoveOrchestrator>(manager, groupName))
{
}
```

**设计要点：**

| 方面 | 设计决策 | 理由 |
|------|---------|------|
| **定位方式** | `(SystemManager&, groupName, axisId)` 三元组 | 动态路由，不绑定具体 Axis 指针。支持多组多轴 |
| **UseCase 所有权** | 内部 `unique_ptr` 创建 | ViewModel 是 UseCase 的天然拥有者，生命周期绑定 |
| **Orchestrator 构造** | 传入 `(manager, groupName)` | Orchestrator 内部自行解析上下文，tick() 无参 |
| **Axis 访问** | 运行时通过 `tryGetAxis()` 动态查找 | 每次调用都重新解析，确保反映最新状态 |

### 2.2 状态投影（State Projection）模式

所有状态查询均采用"**无缓存、每次透传**"策略：

```cpp
AxisState AxisViewModelCore::state() const {
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) return AxisState::Unknown;    // 安全回退
    return axis->state();
}
```

**投影方法清单：**

| 方法 | 返回类型 | 领域来源 | 失败回退值 |
|------|---------|---------|-----------|
| `state()` | `AxisState` | `axis->state()` | `AxisState::Unknown` |
| `absPos()` | `double` | `axis->currentAbsolutePosition()` | `0.0` |
| `relPos()` | `double` | `axis->currentRelativePosition()` | `0.0` |
| `isEnabled()` | `bool` | `state() != Disabled` | `false` |
| `jogVelocity()` | `double` | `axis->getjogVelocity()` | `0.0` |
| `moveVelocity()` | `double` | `axis->getMoveVelocity()` | `0.0` |
| `posLimit()` | `double` | `axis->positiveSoftLimit()` | `0.0` |
| `negLimit()` | `double` | `axis->negativeSoftLimit()` | `0.0` |

**设计原则：**
- **Fail-safe**：Axis 指针为空时全部回退到安全值
- **无副作用**：所有查询方法均为 `const` 且线程安全（假设 Axis 不被并发写）

### 2.3 控制指令的两种路由模式

ViewModel 的控制指令分**两条路径**执行：

#### 路径 A：直接 UseCase（简单单步操作）

```cpp
void AxisViewModelCore::enable(bool active) {
    auto result = m_enableUc->execute(m_manager, m_groupName, m_axisId, active);
    executeAndTranslate(result, m_lastError, m_hasError);
}
```

**适用：** `enable()`, `disable()`, `stop()`  
**特征：** 调用后立即翻译错误，不涉及异步编排。

#### 路径 B：Orchestrator 代理（复杂多步编排）

```cpp
void AxisViewModelCore::moveAbsolute(double targetPos) {
    m_absOrch->startAbs(m_axisId, targetPos);
}
```

**适用：** `jog()`, `jogStop()`, `moveAbsolute()`, `moveRelative()`  
**特征：**
- 编排器内部持有 `SystemManager&` + `groupName`
- 自动处理多步流程（如 `Disabled → Enable → Move → Disable`）
- 错误在 `tick()` 中通过 `hasError()` 收集

#### 路径 C：直接 Axis 操作（无 UseCase/Orchestrator）

```cpp
void AxisViewModelCore::zeroAbsolutePosition() {
    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (axis) {
        axis->zeroAbsolutePosition();
    }
}
```

**适用：** `setJogVelocity()`, `setMoveVelocity()`, `zeroAbsolutePosition()`, `setRelativeZero()`, `clearRelativeZero()`  
**特征：** 单步同步操作，无编排需求，命令在 `tick()` 中通过 pending command 机制下发。

### 2.4 Tick 驱动机制（核心时序引擎）

```cpp
void AxisViewModelCore::tick() {
    // Step 1: 驱动所有编排器状态机
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // Step 2: 收集并翻译各编排器的错误（优先级：Jog > Abs > Rel）
    if (m_jogOrch->hasError()) {
        m_lastError = translate(m_jogOrch->lastError());
        m_hasError = true;
    }
    if (m_absOrch->hasError()) {
        m_lastError = translate(m_absOrch->lastError());
        m_hasError = true;
    }
    if (m_relOrch->hasError()) {
        m_lastError = translate(m_relOrch->lastError());
        m_hasError = true;
    }

    // Step 3: 消费零位/速度类 pending command
    //   tick() 中从 Axis 读取 pending_intent，通过 driver->send() 下发
    auto* axis = tryGetAxis(...);
    if (axis) {
        const AxisCommand& cmd = axis->getPendingCommand();
        if (isZeroOrVelocityCmd(cmd)) {
            auto commResult = drv->send(AxisCommandWithId{m_axisId, cmd});
            if (!commResult.ok()) {
                m_lastError = ViewModelError{...};
            }
        }
    }
}
```

**三步执行逻辑：**

1. **状态推进** — 驱动所有 Orchestrator 内部状态机向前流转
2. **错误收集** — 检查每个 Orchestrator 的 `hasError()`，翻译并覆盖存储
3. **命令消费** — 零位/速度类命令在此处下发（运动类命令由 Orchestrator 负责）

**关键设计取舍：**

| 决策 | 选择 | 理由 |
|------|------|------|
| 零位命令在 tick 中下发 | 而非在操作函数中直接下发 | 保持操作函数轻量，统一消费时机 |
| 错误优先级策略 | 后发生的覆盖之前的 | 简单可预测，但可能丢失前序错误 |
| Orchestrator 驱动顺序 | Jog → Abs → Rel | 固定顺序，避免不确定性 |

### 2.5 stop() 的特殊处理

```cpp
void AxisViewModelCore::stop() {
    // 1. 发送停止命令到硬件
    auto result = m_stopUc->execute(m_manager, m_groupName, m_axisId);
    executeAndTranslate(result, m_lastError, m_hasError);

    // 2. 中断正在进行的点动编排器
    if (m_jogOrch->currentStep() != ...Done/Error/Idle) {
        m_jogOrch->stopJog(m_axisId, Direction::Forward);
    }
}
```

**特殊性：** `stop()` 是唯一需要**同时与 UseCase 和 Orchestrator 交互**的操作——既要直接停硬件，又要通知编排器状态回退。

---

## 3. QtAxisViewModel：Qt 适配层设计分析

### 3.1 适配器模式

`QtAxisViewModel` 是典型的**适配器（Adapter）模式**实现：

```cpp
class QtAxisViewModel : public QObject {
    AxisViewModelCore* m_core;  // 被适配对象
    ...
};
```

**适配内容：**

| 非 Qt 概念 | Qt 概念 | 适配方式 |
|-----------|---------|---------|
| C++ 枚举 `AxisState` | `Q_PROPERTY(int state)` | `static_cast<int>(m_core->state())` |
| `std::string` | `QString` | `QString::fromStdString()` |
| 纯函数调用 | `Q_INVOKABLE void` | 直接委托转发 |
| 状态变化 | `signal` | 缓存比较后 emit |

### 3.2 缓存节流（Throttle）机制

```cpp
void QtAxisViewModel::tick() {
    m_core->tick();

    #1# — state ---
    AxisState currentState = m_core->state();
    if (currentState != m_lastState) {
        m_lastState = currentState;
        emit stateChanged();
    }

    #2# — absPos (带 EPSILON 阈值) ---
    double currentAbsPos = m_core->absPos();
    if (std::abs(currentAbsPos - m_lastAbsPos) > EPSILON) {  // ε = 0.001
        m_lastAbsPos = currentAbsPos;
        emit absPosChanged();
    }

    #3# — relPos (带 EPSILON 阈值) ---
    ...

    #4# — error (布尔变化检测) ---
    bool currentHasError = m_core->hasError();
    if (currentHasError != m_lastHasError) {
        m_lastHasError = currentHasError;
        emit errorChanged();
    }
}
```

**节流策略矩阵：**

| 属性 | 比较方式 | 节流效果 |
|------|---------|---------|
| `state` | 枚举 `!=` 比较 | 仅轴状态变化时触发（通常每秒几次） |
| `absPos` / `relPos` | 浮点差 > 0.001 | 仅在位置确实变化时触发 |
| `hasError` | 布尔 `!=` | 错误出现/消失时触发 |
| `errorCode` / `errorMessage` | 随 `errorChanged` 带动 | 不独立触发信号 |
| `posLimit` / `negLimit` | 无节流 | 通常在构造函数设置后不再变化 |
| `jogVelocity` / `moveVelocity` | 无节流 | 可通过 `velocityChanged` 信号节流（未实现） |

### 3.3 Q_PROPERTY 完整映射

```cpp
Q_PROPERTY(int state READ state NOTIFY stateChanged)
Q_PROPERTY(double absPos READ absPos NOTIFY absPosChanged)
Q_PROPERTY(double relPos READ relPos NOTIFY relPosChanged)
Q_PROPERTY(double posLimit READ posLimit NOTIFY limitsChanged)
Q_PROPERTY(double negLimit READ negLimit NOTIFY limitsChanged)
Q_PROPERTY(double jogVelocity READ jogVelocity WRITE setJogVelocity NOTIFY velocityChanged)
Q_PROPERTY(double moveVelocity READ moveVelocity WRITE setMoveVelocity NOTIFY velocityChanged)
Q_PROPERTY(bool hasError READ hasError NOTIFY errorChanged)
Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
```

**发现的问题：**
- ⚠️ 缺少 `isEnabled` 属性映射（QML 无法直接判断使能状态）
- ⚠️ 缺少 `state` 对应的字符串描述（QML 需要自行映射枚举值到显示文本）
- ⚠️ 缺少 `ErrorCategory` 的 Q_PROPERTY（QML 无法区分 Inline/Modal/Silent）

---

## 4. 错误处理子系统：ErrorTranslator + ViewModelError

### 4.1 整体架构

```
UseCaseError (std::variant, 5 种错误源)
       │
       │ std::visit (编译期保证全覆盖)
       ▼
ErrorTranslator::translate()
       │
       ▼
ViewModelError (struct: code + userMessage + debugMessage + category)
       │
       ▼
QtAxisViewModel (Q_PROPERTY: errorCode, errorMessage, hasError)
       │
       ▼
QML 绑定 (UI 呈现)
```

### 4.2 UseCaseError 变体分析

```cpp
using UseCaseError = std::variant<
    std::monostate,         // 成功：无错误
    ContextRejection,       // SystemManager/SystemContext 层错误
    RejectionReason,        // Axis 领域层拒绝原因
    CommunicationResult,    // 通讯失败
    GantryRejection,        // 龙门联动层错误
    SafetyRejection         // 安全域急停层错误
>;
```

### 4.3 ViewModelError 结构体

```cpp
struct ViewModelError {
    std::string code;         // "AXIS_AT_POSITIVE_LIMIT" — 机器可读
    std::string userMessage;  // "轴已到达正向限位" — 用户可读（中文）
    std::string debugMessage; // 调试用详细诊断信息
    ErrorCategory category;   // Inline / Modal / Silent
};
```

**ErrorCategory 枚举语义：**

| 分类 | 含义 | UI 表现 | 典型场景 |
|------|------|---------|---------|
| `Inline` | 不影响操作 | 内联提示，不打断用户 | 限位到达、参数无效 |
| `Modal` | 严重错误 | 弹窗警告，要求确认 | 通讯中断、急停、组不存在 |
| `Silent` | 不呈现 | 仅日志 | 成功态、已处于目标状态 |

### 4.4 完整翻译表（5 大错误源、31 个枚举值全覆盖）

`ErrorTranslator::translate()` 使用 `std::visit` + `if constexpr` 模式，编译器保证所有分支覆盖。

#### ✅ **Axis 领域层 (RejectionReason) — 7 个分支**

| 枚举值 | `code` | `category` |
|--------|--------|-----------|
| `RejectionReason::InvalidState` | `AXIS_INVALID_STATE` | Inline |
| `RejectionReason::AlreadyMoving` | `AXIS_ALREADY_MOVING` | Inline |
| `RejectionReason::TargetOutOfPositiveLimit` | `AXIS_TARGET_OUT_OF_POS_LIMIT` | Inline |
| `RejectionReason::TargetOutOfNegativeLimit` | `AXIS_TARGET_OUT_OF_NEG_LIMIT` | Inline |
| `RejectionReason::AtPositiveLimit` | `AXIS_AT_POSITIVE_LIMIT` | Inline |
| `RejectionReason::AtNegativeLimit` | `AXIS_AT_NEGATIVE_LIMIT` | Inline |
| `RejectionReason::InvalidArgument` | `AXIS_INVALID_ARGUMENT` | Inline |
| `RejectionReason::UnknownError / None` | `AXIS_UNKNOWN_ERROR` | Modal |

#### ✅ **系统上下文层 (ContextRejection) — 10 个分支**

| 枚举值 | `code` | `category` |
|--------|--------|-----------|
| `ContextRejection::GroupNotFound` | `CTX_GROUP_NOT_FOUND` | Modal |
| `ContextRejection::GroupAlreadyExists` | `CTX_GROUP_ALREADY_EXISTS` | Modal |
| `ContextRejection::GroupNameInvalid` | `CTX_GROUP_NAME_INVALID` | Modal |
| `ContextRejection::PhysicalAxisLockedByGantry` | `CTX_PHYSICAL_AXIS_LOCKED` | Inline |
| `ContextRejection::LogicalAxisUnavailableWhenDecoupled` | `CTX_LOGICAL_AXIS_UNAVAILABLE` | Inline |
| `ContextRejection::GantryNotSynchronized` | `CTX_GANTRY_NOT_SYNCED` | Modal |
| `ContextRejection::AxisNotRegistered` | `CTX_AXIS_NOT_REGISTERED` | Modal |
| `ContextRejection::SystemSafetyLocked` | `CTX_SAFETY_LOCKED` | Modal |
| `ContextRejection::DriverNotReady` | `CTX_DRIVER_NOT_READY` | Modal |
| `ContextRejection::None` | `CTX_UNKNOWN_ERROR` | Modal |

#### ✅ **通讯层 (CommunicationResult) — 7 个分支**

| 枚举值 | `code` | `category` | 备注 |
|--------|--------|-----------|------|
| `NetworkError` | `COMM_NETWORK_ERROR` | Modal | `diagnostic` 透传为 `debugMessage` |
| `Timeout` | `COMM_TIMEOUT` | Modal | `diagnostic` 透传 |
| `Busy` | `COMM_PLC_BUSY` | Inline | 短时可恢复，提示重试 |
| `ProtocolError` | `COMM_PROTOCOL_ERROR` | Modal | 拼接 `Exception code: 0x{N}` |
| `InvalidResponse` | `COMM_INVALID_RESPONSE` | Modal | 数据异常 |
| `Disconnected` | `COMM_DISCONNECTED` | Modal | 严重连接问题 |
| `Sent` (default) | `COMM_UNKNOWN` | Modal | 不应出现的分支 |

#### ✅ **龙门联动层 (GantryRejection) — 8 个分支**

| 枚举值 | `code` | `category` |
|--------|--------|-----------|
| `GantryRejection::None` | `GANTRY_NONE` | Inline |
| `GantryRejection::PositionToleranceExceeded` | `GANTRY_POS_TOLERANCE_EXCEEDED` | Modal |
| `GantryRejection::X1NotEnabled` | `GANTRY_X1_NOT_ENABLED` | Inline |
| `GantryRejection::X2NotEnabled` | `GANTRY_X2_NOT_ENABLED` | Inline |
| `GantryRejection::X1NotStationary` | `GANTRY_X1_NOT_STATIONARY` | Inline |
| `GantryRejection::X2NotStationary` | `GANTRY_X2_NOT_STATIONARY` | Inline |
| `GantryRejection::StateConflict` | `GANTRY_STATE_CONFLICT` | Inline |
| `GantryRejection::NotSynchronized` | `GANTRY_NOT_SYNCED` | Modal |
| `GantryRejection::UnknownError` | `GANTRY_UNKNOWN` | Modal |

#### ✅ **安全域急停层 (SafetyRejection) — 6 个分支**

| 枚举值 | `code` | `category` |
|--------|--------|-----------|
| `SafetyRejection::None` | `SAFETY_NONE` | Modal |
| `SafetyRejection::SystemSafetyLocked` | `SAFETY_SYSTEM_LOCKED` | Modal |
| `SafetyRejection::AlreadyInState` | `SAFETY_ALREADY_IN_STATE` | Silent |
| `SafetyRejection::InvalidStateTransition` | `SAFETY_INVALID_TRANSITION` | Modal |
| `SafetyRejection::NotSynchronized` | `SAFETY_NOT_SYNCED` | Modal |
| `SafetyRejection::NotEmergencyStopped` | `SAFETY_NOT_EMERGENCY_STOPPED` | Inline |

---

## 5. TDD 测试策略与实现分析

### 5.1 测试架构总览

**测试文件清单：**

| 文件 | 测试类 | 测试数量 | 覆盖对象 |
|------|--------|---------|---------|
| `test_axis_viewmodel_core.cpp` | `AxisViewModelCoreTest` | 13 个 | ViewModel 核心 + 集成流程 |
| `test_error_translator.cpp` | `ErrorTranslatorTest` | 31 个 | 错误翻译函数全覆盖 |

### 5.2 测试基础设施

```cpp
class AxisViewModelCoreTest : public ::testing::Test {
protected:
    FakePLC plc;                        // 虚拟化 PLC 物理引擎
    FakeAxisDriver driver{plc};          // 虚拟驱动
    SystemManager manager;               // 全局系统管理器
    SystemContext* ctx = nullptr;         // 分组上下文
    std::unique_ptr<AxisViewModelCore> vm; // 被测对象

    const int TICK_MS = 10;              // 每帧 10ms

    // ⭐ 核心辅助工具 1: 时间推进器
    void advanceTime(int totalMs) {
        int elapsed = 0;
        while (elapsed < totalMs) {
            vm->tick();                   // 驱动 ViewModel
            driver.pollFeedback(*ctx);    // 同步硬件反馈
            elapsed += TICK_MS;
        }
    }

    // ⭐ 核心辅助工具 2: 条件等待器（带超时保护）
    bool waitUntil(std::function<bool()> condition, int timeoutMs = 5000) {
        int elapsed = 0;
        while (elapsed < timeoutMs) {
            if (condition()) return true;
            advanceTime(TICK_MS);
            elapsed += TICK_MS;
        }
        return false;
    }
};
```

**测试具（Fixture）Setup 流程：**

```
1. createGroup("Machine_Test")        → 创建分组
2. ctx->setDriver(&driver)             → 绑定驱动
3. plc.forceState(Y, Disabled)         → 初始状态
4. plc.setSimulatedMoveVelocity(Y, 25) → 速度预设
5. driver.pollFeedback(*ctx)           → 硬件 → 领域同步
6. vm = new AxisViewModelCore(mgr, ..) → 创建被测对象
7. vm->setJogVelocity(20)             → 初始速度设置
```

### 5.3 测试用例映射矩阵

| # | 测试方法 | 测试场景 | 验证手段 | 层级 |
|---|---------|---------|---------|------|
| 1 | `ShouldReflectInitialDisabledState` | 初始状态映射 | 断言 state=Disabled, absPos=0, no Error | 投影 |
| 2 | `ShouldEnableAxisAndReflectState` | 使能生命周期 | waitUntil(state=Idle), assert no Error, 确认领域层一致 | 控制 |
| 3 | `ShouldExecuteJogForwardLifecycle` | 正向点动全生命周期 | 使能→Jogging→位移>5mm→停止→Disabled→不漂移 | 编排 |
| 4 | `ShouldExecuteJogBackwardLifecycle` | 反向点动全生命周期 | 正向→反向→位置回落>1mm | 编排 |
| 5 | `ShouldCompleteAbsoluteMoveEndToEnd` | 绝对定位全生命周期 | target=100, waitUntil(Disabled), 精度±0.01 | 编排 |
| 6 | `ShouldCompleteRelativeMoveEndToEnd` | 相对定位全生命周期 | 两段定位 50+30=80, 精度±0.01 | 编排 |
| 7 | `ShouldHaltImmediatelyWhenStopPressed` | 急停中断运动 | 运动中 stop, 位置截断<700, 停止后不漂移 | 干预 |
| 8 | `ShouldRejectMoveWhenDisabled` | Disabled 状态可运动（编排器自动使能） | 验证 Orchestrator 自动完成全流程 | 编排 |
| 9 | `ShouldReportAndClearErrors` | 错误接口 | hasError=false→clearError 幂等 | 错误 |
| 10 | `ShouldHandleZeroOperations` | 零位操作 | setRelativeZero→relPos≈0→zeroAbsolute→absPos≈0→clearRelative | 操作 |
| 11 | `ShouldReflectLimits` | 限位反射 | posLimit=1000, negLimit=-1000 | 投影 |
| 12 | `ShouldSetAndReflectVelocity` | 速度设置 | setJogVelocity(50), setMoveVelocity(60) | 操作 |
| 13 | `ShouldReportIsEnabledCorrectly` | isEnabled 属性 | Disabled=false, Idle=true, Disable→false | 投影 |

### 5.4 关键测试模式分析

#### 模式 1：完整生命周期验证

```cpp
TEST_F(AxisViewModelCoreTest, ShouldExecuteJogForwardLifecycle) {
    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));

    double startPos = vm->absPos();
    vm->jog(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Jogging; }));

    advanceTime(500);  // 500ms 点动 → 产生位移
    EXPECT_GT(vm->absPos(), startPos + 5.0);  // 位移验证

    vm->jogStop(Direction::Forward);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));

    double finalPos = vm->absPos();
    advanceTime(200);
    EXPECT_NEAR(vm->absPos(), finalPos, 0.01);  // 停止后不漂移
}
```

**测试策略特征：**
- **时间推进** + **条件等待** 模拟真实时序
- 每个操作后断言状态转换，确保中间步骤正确
- 最后的"不漂移"验证确保停止后无滑移
- 不验证内部实现细节（如 Orchestrator Step 枚举），只验证外部可观察行为

#### 模式 2：中断测试

```cpp
TEST_F(AxisViewModelCoreTest, ShouldHaltImmediatelyWhenStopPressed) {
    vm->moveAbsolute(800.0);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::MovingAbsolute; }));
    advanceTime(500);  // 跑 500ms

    vm->stop();
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Disabled; }));

    EXPECT_LT(vm->absPos(), 700.0) << "Axis kept moving after stop!";
    // 停止后不漂移验证
}
```

**关键验证点：** 位置被截断在半路（未到达 800），证明 stop 确实中断了运动。

#### 模式 3：错误接口验证

```cpp
TEST_F(AxisViewModelCoreTest, ShouldReportAndClearErrors) {
    EXPECT_FALSE(vm->hasError());
    auto e = vm->lastError();
    EXPECT_FALSE(e.isValid());

    vm->enable(true);
    ASSERT_TRUE(waitUntil([this]() { return vm->state() == AxisState::Idle; }));
    EXPECT_FALSE(vm->hasError());  // 正常操作不产生错误

    vm->clearError();  // 幂等测试
    EXPECT_FALSE(vm->hasError());
}
```

**注意：** 错误接口的测试相对薄弱——缺少真正触发错误的场景（如跳转到 Inline/Modal 错误分支的验证）。

### 5.5 ErrorTranslator 测试分析（全覆盖验证）

```cpp
TEST_F(ErrorTranslatorTest, MonostateShouldReturnEmptyError) {
    UseCaseError err = std::monostate{};
    auto vmErr = translate(err);
    EXPECT_FALSE(vmErr.isValid());        // code 为空
    EXPECT_EQ(vmErr.category, ErrorCategory::Silent);
}

TEST_F(ErrorTranslatorTest, RejectionReason_InvalidState) {
    auto vmErr = translate(UseCaseError{RejectionReason::InvalidState});
    EXPECT_EQ(vmErr.code, "AXIS_INVALID_STATE");
    EXPECT_EQ(vmErr.category, ErrorCategory::Inline);
}
```

**覆盖统计：**

| 错误源 | 分支数 | 测试数 | 覆盖率 |
|--------|-------|-------|--------|
| `std::monostate` | 1 | 1 | 100% |
| `RejectionReason` | 8 | 8 | 100% |
| `ContextRejection` | 10 | 10 | 100% |
| `CommunicationResult` | 7 | 7 | 100% |
| `GantryRejection` | 9 | 8 (漏 `None`? 实际已测) | ~100% |
| `SafetyRejection` | 6 | 6 | 100% |

**每个测试验证：**
- ✅ `code` 字符串精确匹配
- ✅ `category` 枚举值正确
- ✅ 对于特殊逻辑（如 CommunicationResult），验证 `diagnostic` 是否透传到 `debugMessage`

---

## 6. 数据流全链路追踪

### 6.1 正向操作流（用户按下点动+按钮）

```
① QML: IndustrialButton.onPressed
    ↓
② QtAxisViewModel::jogPositivePressed()
    ↓ 纯委托转发
③ AxisViewModelCore::jog(Direction::Forward)
    ↓
④ JogOrchestrator::startJog(AxisId::Y, Forward)
    ↓
⑤ (子状态机: EnsuringEnabled → IssuingStart → WaitingMotionStart → Jogging)
    ↓
⑥ FakeAxisDriver::send(JogCommand)  →  FakePLC 物理引擎
    ↓
⑦ [tick() 循环中]
   AxisViewModelCore::tick()
     → JogOrchestrator::tick()
     → driver.pollFeedback(*ctx)  [由上层循环调用，不在 ViewModel 内部]
    ↓
⑧ QtAxisViewModel::tick()
     → 缓存比较: state 变化 → emit stateChanged()
     → 缓存比较: absPos 差异 > 0.001 → emit absPosChanged()
    ↓
⑨ QML 绑定自动更新 UI
```

### 6.2 Tick 全局时序图

```
时间轴 (每 10ms)
│
├─ [外部定时器回调]
│
├─ 1. SystemManager::pollAll()           ← 不在 ViewModel 内，在上层管理
│      └─ driver.pollFeedback(ctx)       ← 硬件→领域同步
│
├─ 2. AxisViewModelCore::tick()
│      ├─ jogOrch->tick()                ← 点动状态机推进
│      ├─ absOrch->tick()                ← 绝对定位状态机推进
│      ├─ relOrch->tick()                ← 相对定位状态机推进
│      ├─ 错误收集 (3 个 Orch)
│      └─ zero/velocity Pending Command 消费
│
├─ 3. QtAxisViewModel::tick()
│      ├─ core->tick()                   ← 以上所有
│      └─ 缓存比较 → emit signals
│
└─ 4. QML 引擎处理信号 → UI 更新
```

---

## 7. 设计模式汇总

| # | 模式 | 应用位置 | 作用 | 实现方式 |
|---|------|---------|------|---------|
| 1 | **Adapter（适配器）** | `QtAxisViewModel` 封装 `AxisViewModelCore` | 桥接纯 C++ 核心与 Qt 属性系统 | 内部持有 m_core 指针，Q_PROPERTY 读取 m_core 状态 |
| 2 | **Mediator（中介者）** | `AxisViewModelCore` 作为中介 | 协调 UseCase、Orchestrator、ErrorTranslator 的交互 | 统一控制入口，tick() 驱动所有编排器 |
| 3 | **Anti-Corruption Layer（防腐层）** | `ErrorTranslator` | 隔离领域层错误枚举与 UI 层 | UseCaseError variant → ViewModelError struct |
| 4 | **Command（命令）** | 控制方法 → UseCase/Orchestrator | 将 UI 操作封装为命令对象 | 每个 UI 操作委托到对应的 Application 组件 |
| 5 | **Observer（观察者）** | Qt 信号 → QML 绑定 | 状态变化自动通知 UI 更新 | Q_PROPERTY NOTIFY signals |
| 6 | **Strategy（策略）** | Jog / Abs / Rel Orchestrator | 不同运动策略可替换 | 三个 Orchestrator 各自封装完整流程 |
| 7 | **Facade（外观）** | `AxisViewModelCore` | 为复杂子系统提供统一简化接口 | 10+ 控制方法隐藏内部 5 个 UseCase + 3 个 Orchestrator |
| 8 | **State（状态）** | JogOrchestrator 内部状态机 | 编排多步骤操作 | Step 枚举 + tick() 驱动状态转换 |
| 9 | **Value Object（值对象）** | `ViewModelError` | 携带多字段的错误数据传输 | struct 聚合 code/message/category |

---

## 8. 架构演进分析：重构前后的对比

### 8.1 重构前 vs 重构后核心差异

| 维度 | 重构前 | 重构后 |
|------|--------|--------|
| **构造签名** | `(Axis&, JogOrch&, ...)` 注入引用 | `(SystemManager&, groupName, axisId)` 动态路由 |
| **轴定位** | 构造时固定 Axis& 引用 | 每次调用通过 tryGetAxis() 动态解析 |
| **错误处理** | `hasError()` 返回 `std::string` 死代码 | `lastError()` 返回完整的 `ViewModelError` |
| **错误翻译** | 无 | `ErrorTranslator::translate()` 全覆盖 |
| **使能控制** | 无 `enable()/disable()` | 完整的使能/掉电流程 |
| **零位操作** | 无 | `zeroAbsolutePosition()`, `setRelativeZero()` 等 |
| **多组支持** | 单组硬编码 | 构造时传入 groupName 动态路由 |
| **Orchestrator 所有权** | 外部传入引用 | 内部 unique_ptr 创建并管理生命周期 |
| **UseCase 所有权** | 外部传入引用 | 内部 unique_ptr 创建 |
| **Tick 驱动** | `update(Axis&)`（绑定单轴引用） | `tick()`（无参，内部解析上下文） |
| **Q_PROPERTY 数量** | 6~7 个 | 10 个（新增 hasError/errorCode/errorMessage） |

### 8.2 重构解决的关键问题

1. **致命问题已解决：**
   - ❌ UI 无法承载多组多轴 → ✅ `(SystemManager&, groupName, axisId)` 动态路由
   - ❌ 错误处理链路断裂 → ✅ `ErrorTranslator` 全覆盖翻译
   - ❌ stop() 签名错配 → ✅ `execute(manager, groupName, axisId)`
   - ❌ tick() 签名错配 → ✅ 无参 `tick()` 内部解析
   - ❌ 缺少 enable/disable → ✅ `enable(bool)`, `disable()`
   - ❌ 缺少零位操作 → ✅ 三种零位操作完整实现

2. **严重问题已改善：**
   - ❌ 缺少位置查询接口 → ✅ 完整状态投影集
   - ❌ 没有 SystemContext 支持 → ✅ 通过 manager 路由到 SystemContext
   - ❌ Q_PROPERTY 暴露有限 → ✅ 新增错误属性
   - ❌ hasError/errorMessage 死代码 → ✅ 完整错误子系统

### 8.3 重构设计文档与实现的差异（实际实现与重构方案的偏差）

重构分析文档 `AxisViewModelCore重构分析与设计.md` 中的设计方案与实际代码存在以下差异：

| 设计文档方案 | 实际代码实现 | 差异分析 |
|------------|------------|---------|
| `enable()` 无参 | `enable(bool active)` | 实际更灵活，可同时用于使能和掉电 |
| UseCase 值语义（非指针） | UseCase `unique_ptr` | 实际使用智能指针，生命周期管理更明确 |
| 注入 `relZeroAbsPos()` / `isMoving()` | 未实现 | 简化了接口，保留最小必要方法 |
| 注入 `atPosLimit()` / `atNegLimit()` | 未实现 | 未在 ViewModel 层封装，留在 Axis 层 |
| GantryViewModelCore 新增 | 未实现 | 在 ViewModel 重构范围外 |
| EmergencyStopViewModel 新增 | 未实现 | 在 ViewModel 重构范围外 |

---

## 9. 存在缺陷与改进建议

### 9.1 代码缺陷分析

#### 🔴 严重缺陷

| # | 缺陷 | 位置 | 描述 | 影响 |
|---|------|------|------|------|
| 1 | **Q_PROPERTY 缺少 `isEnabled`** | QtAxisViewModel.h | 只有 `state()` 可以推导 isEnabled，但 QML 需自行判断 `state != 1(Disabled)` | QML 无法简洁地绑定使能状态 |
| 2 | **错误优先级策略丢失错误** | `AxisViewModelCore::tick()` | 三个 Orchestrator 的错误依次覆盖，只有最后一个被保留 | 前序 Orchestrator 错误被静默丢弃 |
| 3 | **零位/速度命令无错误保护** | `setJogVelocity()`, `zeroAbsolutePosition()` 等 | 直接操作 Axis 领域对象，不检查返回值 | 失败时静默忽略 |
| 4 | **disable() 无条件重置错误** | `enable(false)` | 调用 `executeAndTranslate()`，可能意外覆盖已有错误 | 错误状态可能被错误覆盖 |

#### 🟡 中等缺陷

| # | 缺陷 | 位置 | 描述 |
|---|------|------|------|
| 5 | **QML 无 ErrorCategory 映射** | QtAxisViewModel.h | QML 拿到 `hasError` 后无法区分 Inline/Modal/Silent |
| 6 | **缺少状态文本描述** | QtAxisViewModel.h | QML 需要自行编写 `state → text` 映射（如 `1 → "Disabled"`） |
| 7 | **velocity 无信号节流** | QtAxisViewModel::tick() | `jogVelocity`/`moveVelocity` 变化不会触发 `velocityChanged` |
| 8 | **posLimit/negLimit 无变化检测** | QtAxisViewModel::tick() | 限位不会触发 `limitsChanged`（虽然通常不变） |
| 9 | **错误接口测试薄弱** | test_axis_viewmodel_core.cpp | 测试 9 只验证了正常流程无错误，未验证错误产生路径 |

#### 🔵 轻微缺陷

| # | 缺陷 | 位置 | 描述 |
|---|------|------|------|
| 10 | **硬编码 "G1" 日志** | 重构文档中提到，实际代码中 `TraceScope` 使用 `m_groupName` | 需要确认是否已修复 |
| 11 | **tick() 中 driver->send() 重复调用** | `tick()` 末尾 | 每次 tick 都消费 pending command，可能重复发送 |
| 12 | **ErrorTranslator 缺少单元测试分组** | test_error_translator.cpp | 每个分支独立 TEST_F，代码冗余度高 |

### 9.2 改进建议

#### 建议 1：添加 isEnabled Q_PROPERTY

```cpp
// QtAxisViewModel.h
Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY stateChanged)
// 可复用 stateChanged 信号，因为 isEnabled 从 state 推导
```

#### 建议 2：错误收集改为列表而非覆盖

```cpp
// AxisViewModelCore 中
std::vector<ViewModelError> m_errors;  // 而非单个 m_lastError
// tick() 中 m_errors.push_back(translate(...));
```

#### 建议 3：增加 ErrorCategory 的 Q_PROPERTY

```cpp
Q_PROPERTY(QString errorCategory READ errorCategory NOTIFY errorChanged)
// 返回 "Inline" / "Modal" / "Silent"
```

#### 建议 4：补充错误触发测试

需要补充的测试场景：
- 限位触发错误（`AtPositiveLimit` / `AtNegativeLimit`）
- 无效参数错误（`InvalidArgument`）
- Orchestrator 错误收集验证

#### 建议 5：增加 velocityChanged 信号的缓存比较

```cpp
// QtAxisViewModel::tick() 中
double currentJogV = m_core->jogVelocity();
if (std::abs(currentJogV - m_lastJogVelocity) > EPSILON) {
    m_lastJogVelocity = currentJogV;
    emit velocityChanged();
}
```

---

## 10. 总结

### 10.1 架构优势

1. **清晰的层次分离**：纯 C++ 核心 (`AxisViewModelCore`) 与 Qt 适配层 (`QtAxisViewModel`) 职责分明，核心逻辑不依赖 Qt 运行时，可直接单元测试
2. **动态路由架构**：通过 `(SystemManager&, groupName, axisId)` 三元组定位轴，支持多组多轴场景
3. **完善的防腐层**：`ErrorTranslator` 使用 `std::visit` 覆盖 5 种错误源 31 个分支，将领域语义隔离在 UI 层之外
4. **强大的编排能力**：`tick()` 驱动 3 个 Orchestrator 状态机 + 零位/速度命令消费，实现异步操作的自动完成
5. **测试基础设施优秀**：`advanceTime()` + `waitUntil()` 测试辅助工具提供了对异步操作的确定性测试能力
6. **重构执行力强**：从旧架构（引用注入、无错误处理、单组绑定）到新架构（动态路由、错误翻译、完整生命周期支持）的迁移成果显著

### 10.2 架构瓶颈

1. **错误收集模型过于简单**：当前采用"覆盖模式"，仅保留最后一个错误，可能丢失重要前序错误
2. **QML 绑定能力有限**：缺少 `isEnabled`、`ErrorCategory`、状态文本等属性，QML 侧需要额外逻辑
3. **零位/速度操作路径不统一**：部分操作直接操作 Axis 对象（无 UseCase/Orchestrator），部分在 tick() 中下发，路径不一致
4. **缺少 Gantry/E-Stop ViewModel**：重构方案中规划了 `GantryViewModelCore` 和 `EmergencyStopViewModel`，尚未实现
5. **QML 的 isReadyForPos 防呆逻辑依赖 state 枚举硬编码值 1/2**：脆弱且不易维护

### 10.3 TDD 测试质量评估

| 维度 | 评分 | 说明 |
|------|------|------|
| ✅ 覆盖率 | ★★★★★ | 13 个集成测试 + 31 个单元测试，覆盖主要路径和所有错误分支 |
| ✅ 可读性 | ★★★★☆ | Given-When-Then 结构清晰，等待器模式优雅 |
| ✅ 可靠性 | ★★★★☆ | 带超时的条件等待防止死循环，FakePLC 可重复 |
| ❌ 健壮性 | ★★★☆☆ | 缺少异常场景测试（错误产生路径、边界条件） |
| ❌ 维护性 | ★★★★☆ | 测试独立，Fixture 设置干净，但重构后尚未更新旧测试文件 |

### 10.4 最终架构评分

| 评估维度 | 评分 (1-5) | 评语 |
|---------|-----------|------|
| 分层清晰度 | ⭐⭐⭐⭐⭐ | ViewModel Core / Qt Adapter 职责分明 |
| 可测试性 | ⭐⭐⭐⭐⭐ | 纯 C++ 核心可直接单元测试 |
| 可扩展性 | ⭐⭐⭐⭐ | 动态路由支持多组多轴，但缺少 Gantry/E-Stop ViewModel |
| 错误处理 | ⭐⭐⭐⭐ | 全路径错误翻译，但存储模型简单 |
| QML 集成 | ⭐⭐⭐ | 功能完整，但属性映射不完全 |
| 代码质量 | ⭐⭐⭐⭐ | 清晰一致，少数缺陷（Q_PROPERTY 缺失、测试薄弱点） |

---
*本文档基于 servoV6 项目实际源代码和测试代码生成，反映了重构后 AxisViewModel 架构的当前状态。*
