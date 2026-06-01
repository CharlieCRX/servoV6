# isLoading 状态判断 —— 从 UI 到 ViewModel 完整链路分析

## 1. 概述

`isLoading` 是位于 `AxisViewModelCore` 的一个布尔属性，通过 `QtAxisViewModel` 暴露给 QML 界面层。它的核心语义是：**"绝对/相对定位 Policy 是否正在运行中"**。

当 `isLoading == true` 时，UI 上表现为：
- "绝对定位 GO" / "相对定位 GO" 按钮禁用，文案变更为 "运行中..."
- 底部显示当前 Policy 步骤的调试文本（如 "EnsuringEnabled"）

> **重要：** `isLoading` **只**反映 `AbsMovePolicy` 和 `RelMovePolicy` 的状态，**不**涉及 `JogOrchestrator`（点动编排器）。

---

## 2. 数据流全链路

```
┌─────────────────────────────────────────────────────────────────┐
│ QML (ActionControlBlock.qml)                                    │
│                                                                 │
│  isReadyForTrigger = !viewModel.isLoading && ...                │
│  button.text = isLoading ? "运行中..." : "绝对定位 GO"           │
│  moveStep.visible = isLoading                                    │
└─────────────────────────┬───────────────────────────────────────┘
                          │ Q_INVOKABLE / Q_PROPERTY
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│ QtAxisViewModel (QtAxisViewModel.h/.cpp)                        │
│                                                                 │
│  bool isLoading() const {                                       │
│      return m_core ? m_core->isLoading() : false;               │
│  }                                                              │
│  ★ 纯代理，转发到 Core                                           │
└─────────────────────────┬───────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│ AxisViewModelCore (AxisViewModelCore.h/.cpp)                    │
│                                                                 │
│  bool isLoading() const {                                       │
│      bool absActive = m_absPolicy &&                            │
│          m_absPolicy->currentStep() != Step::Initial &&          │
│          m_absPolicy->currentStep() != Step::Done &&             │
│          m_absPolicy->currentStep() != Step::Error;              │
│                                                                 │
│      bool relActive = m_relPolicy &&                            │
│          m_relPolicy->currentStep() != Step::Initial &&          │
│          m_relPolicy->currentStep() != Step::Done &&             │
│          m_relPolicy->currentStep() != Step::Error;              │
│                                                                 │
│      return absActive || relActive;                             │
│  }                                                              │
│  ★ 核心判断：任一 Policy 不处于 Initial/Done/Error 即活跃        │
└─────────────────────────┬───────────────────────────────────────┘
                          │ currentStep()
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│ AbsMovePolicy / RelMovePolicy                                   │
│                                                                 │
│  状态机：                                                        │
│  Initial → EnsuringEnabled → TriggeringMove                     │
│         → WaitingMotionStart → WaitingMotionFinish              │
│         → Disabling → Done                                      │
│                                                                 │
│  ★ isLoading=true 涵盖: EnsuringEnabled ~ Disabling             │
│  ★ isLoading=false 涵盖: Initial / Done / Error                 │
└─────────────────────────┬───────────────────────────────────────┘
                          │ tick() 驱动
                          ▼
┌─────────────────────────────────────────────────────────────────┐
│ AxisViewModelCore::tick()                                       │
│                                                                 │
│  void tick() {                                                  │
│      consumePendingCommands();                                  │
│                                                                 │
│      if (m_absPolicy && !m_absPolicy->isDone()                  │
│          && !m_absPolicy->hasError()) {                         │
│          m_absPolicy->tick();  // 推进状态机                     │
│      }                                                          │
│      if (m_relPolicy && !m_relPolicy->isDone()                  │
│          && !m_relPolicy->hasError()) {                         │
│          m_relPolicy->tick();  // 推进状态机                     │
│      }                                                          │
│      ...                                                        │
│  }                                                              │
│  ★ 每帧推进 Policy 状态机，step 变化驱动 isLoading 变化           │
└─────────────────────────────────────────────────────────────────┘
```

---

## 3. 各层详细分析

### 3.1 QML 层 (ActionControlBlock.qml)

#### 3.1.1 `isReadyForTrigger` — 触发按钮的总闸门

```qml
property bool isReadyForTrigger: !systemLocked && !gantryOperationLocked && viewModel ? 
    (!viewModel.hasError && viewModel.state <= 2 && !viewModel.isLoading) : false
```

触发按钮可用的**全部条件**：
| 条件 | 含义 |
|------|------|
| `!systemLocked` | 非急停/非安全锁定 |
| `!gantryOperationLocked` | 非龙门操作锁定 |
| `viewModel != null` | 已绑定 ViewModel |
| `!viewModel.hasError` | 无错误 |
| `viewModel.state <= 2` | 轴状态 ≤ Idle（Unknown=0, Disabled=1, Idle=2） |
| **`!viewModel.isLoading`** | **Policy 未运行中** |

> `isLoading` 是最后一个条件 —— 即使其他条件都满足，只要 Policy 还在运行，触发按钮就禁用。

#### 3.1.2 按钮文案的三态切换

```qml
// 触发按钮
text: root.isReadyForTrigger ? "绝对定位 GO" : (
    viewModel && viewModel.isLoading ? "运行中..." : "不可用"
)
```

| 状态 | 文案 | 原因 |
|------|------|------|
| `isReadyForTrigger == true` | "绝对定位 GO" | 所有条件满足 |
| `isLoading == true` | **"运行中..."** | Policy 运行中，不能再次触发 |
| 其他 | "不可用" | 安全锁定 / 故障 / 轴运动中 等 |

#### 3.1.3 `isReadyForSetTarget` — 设置目标不受 `isLoading` 限制

```qml
property bool isReadyForSetTarget: !systemLocked && !gantryOperationLocked && viewModel ? 
    (!viewModel.hasError && viewModel.state <= 2) : false
```

> **[设计意图]** 设置目标按钮**不需要** `!viewModel.isLoading`。即使在 Policy 运行中（`isLoading == true`），用户仍然可以设置新的绝对目标 / 相对距离，覆盖旧值。

#### 3.1.4 `moveStep` — 调试信息显示

```qml
Text {
    text: viewModel ? viewModel.moveStep : ""
    visible: viewModel && viewModel.isLoading
    color: "gray"
    font.pixelSize: Theme.fontSmall
    font.family: "Monospace"
}
```

仅当 `isLoading == true` 时显示当前 Policy 步骤的文本（如 "EnsuringEnabled"、"WaitingMotionFinish" 等），用于调试。

---

### 3.2 QtAxisViewModel（Qt 适配层）

```cpp
// QtAxisViewModel.h
Q_INVOKABLE bool isLoading() const;

// QtAxisViewModel.cpp
bool QtAxisViewModel::isLoading() const {
    // ── ★ Loading 状态查询（供 QML 显示加载指示器）──
    return m_core ? m_core->isLoading() : false;
}
```

- `QtAxisViewModel` 是 QML 与 `AxisViewModelCore` 之间的适配层。
- `isLoading()` 是 **纯代理**：如果 `m_core` 存在则转发，否则返回 `false`。
- 使用 `Q_INVOKABLE` 暴露给 QML 的 property binding 系统。

---

### 3.3 AxisViewModelCore（核心判断逻辑）

```cpp
bool AxisViewModelCore::isLoading() const
{
    bool absActive = m_absPolicy &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Initial &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Done &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Error;

    bool relActive = m_relPolicy &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Initial &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Done &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Error;

    return absActive || relActive;
}
```

#### 3.3.1 判断逻辑表

| Policy 当前 Step | `isLoading` | 说明 |
|:--|:--|:--|
| `Initial` | `false` | 还没开始 |
| `EnsuringEnabled` | **`true`** | 正在发送使能命令，等待反馈 |
| `TriggeringMove` | **`true`** | 正在发送触发命令 |
| `WaitingMotionStart` | **`true`** | 等待轴开始运动 |
| `WaitingMotionFinish` | **`true`** | 等待轴运动完成 |
| `Disabling` | **`true`** | 正在发送关使能命令 |
| `Done` | `false` | 完成 |
| `Error` | `false` | 出错终止 |

#### 3.3.2 关键设计点

1. **只看 AbsMovePolicy 和 RelMovePolicy**。
   - `JogOrchestrator`（点动编排器）**不参与** `isLoading` 判断。
   - 点动是长按式的持续操作，不需要 "运行中" 的 UI 状态保护。

2. **使用"排除法"而非"枚举法"**。
   - 判断"不处于终态"（Initial/Done/Error），而非逐一枚举活跃状态。
   - 这样即使 Policy 状态机在未来增加新步骤，`isLoading` 判断无需修改。

3. **任一 Policy 活跃即为 loading**。
   - `absActive || relActive`：两个 Policy 是互斥操作的（同一轴不会同时触发绝对和相对定位），但用 OR 确保任一活跃都返回 true。

---

### 3.4 Policy 层（AbsMovePolicy / RelMovePolicy）

#### 3.4.1 状态机流转

```
                         startAbs() / startRel()
                              │
                              ▼
┌─────────┐   使能      ┌──────────────────┐
│ Initial │ ──────────► │ EnsuringEnabled  │
└─────────┘             └────────┬─────────┘
                                 │ 收到 Idle feedback
                                 ▼
                        ┌──────────────────┐
                        │ TriggeringMove   │ ← 发送 Trigger 命令
                        └────────┬─────────┘
                                 │ 命令发送成功
                                 ▼
                        ┌──────────────────┐
                        │ WaitingMotionStart│ ← 等待位置变化
                        └────────┬─────────┘
                                 │ 检测到运动
                                 ▼
                        ┌──────────────────┐
                        │WaitingMotionFinish│ ← 等待轴回到 Idle
                        └────────┬─────────┘
                                 │ 轴状态为 Idle
                                 ▼
                        ┌──────────────────┐
                        │   Disabling      │ ← 发送关使能
                        └────────┬─────────┘
                                 │
                                 ▼
                        ┌──────────────────┐
                        │      Done        │
                        └──────────────────┘
```

每个状态都可能因异常条件转入 `Error`：
- 急停安全锁定 → 优雅退出为 `Done`（不报错）
- 轴状态为 `Error` → 转入 `Error`
- 使能超时（2 秒） → 转入 `Error(ErrTimeout)`
- UseCase 返回错误 → 转入 `Error`

#### 3.4.2 各步骤详述

| Step | 动作 | 等待条件 | 成功推进到 |
|------|------|----------|-----------|
| `EnsuringEnabled` | 发送 Enable 命令 | `axis->state() == Idle` | `TriggeringMove` |
| `TriggeringMove` | 发送 Trigger 命令 | 命令发送成功 | `WaitingMotionStart` |
| `WaitingMotionStart` | 轮询等待 | `state == MovingAbsolute \|\| 位置变化 > ε` | `WaitingMotionFinish` |
| `WaitingMotionFinish` | 轮询等待 | `state == Idle` | `Disabling` |
| `Disabling` | 发送 Disable 命令 | 即时完成 | `Done` |

#### 3.4.3 急停期间的行为

在 `tick()` 开头检查急停锁：
```cpp
if (group->emergencyStopController().isSystemLocked()) {
    if (m_step != Step::Initial && m_step != Step::Done && m_step != Step::Error) {
        m_step = Step::Done;  // 优雅退出，不报错
        m_lastError = std::monostate{};
    }
    return;  // 不再执行剩余 tick 逻辑
}
```

- 急停时 Policy 直接跳入 `Done`，`isLoading` 立即变为 `false`。
- 不会报错（`lastError` 保持 `monostate`），因为是外部安全事件而非内部错误。

---

### 3.5 tick() 驱动机制

```cpp
void AxisViewModelCore::tick() {
    // Step 1: 消费 pending commands（简单写入）
    consumePendingCommands();

    // Step 2: 驱动 Policy tick（移动触发编排）
    if (m_absPolicy && !m_absPolicy->isDone() && !m_absPolicy->hasError()) {
        m_absPolicy->tick();
        if (m_absPolicy->hasError()) {
            auto vmError = translate(m_absPolicy->lastError());
            pushError(vmError, "AbsPolicy");
        }
    }
    if (m_relPolicy && !m_relPolicy->isDone() && !m_relPolicy->hasError()) {
        m_relPolicy->tick();
        // ...
    }

    // Step 3: 驱动旧编排器 tick（向后兼容）
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // Step 4: 收集旧编排器错误
    // Step 5: 日志摘要
}
```

关键点：
- 每帧都调用 `tick()`（由 Qt 定时器驱动）。
- Policy 在 `tick()` 中自动推进状态机。
- 当 `isDone()` 或 `hasError()` 为 true 时停止调用 Policy tick。
- **`isLoading` 在每帧 tick 之后发生变化** → QML 的 property binding 会在下一帧感知到变化。

---

## 4. 配套属性：moveStep()

```cpp
std::string AxisViewModelCore::moveStep() const
```

与 `isLoading` 配合使用，返回当前活跃 Policy 的步骤名称：

- `absPolicy` 活跃 → 返回其步骤：`"EnsuringEnabled"`, `"TriggeringMove"`, `"WaitingMotionStart"`, `"WaitingMotionFinish"`, `"Disabling"`, `"Error"`
- `relPolicy` 活跃 → 同理
- 两者都不活跃 → 返回 `"Idle"`

在 QML 中仅在 `isLoading == true` 时可见，用于调试/监控。

---

## 5. 完整时序图

```
User               QML              QtAxisVM        AxisVM Core       AbsMovePolicy      Axis(Domain)
 │                  │                  │                 │                  │                 │
 │  输入目标 150.0  │                  │                 │                  │                 │
 │─────────────────►│                  │                 │                  │                 │
 │                  │ setAbsTarget(150)│                 │                  │                 │
 │                  │─────────────────►│                 │                  │                 │
 │                  │                  │ setAbsTarget(150)                  │                 │
 │                  │                  │────────────────►│                  │                 │
 │                  │                  │                 │ setAbsTarget     │                 │
 │                  │                  │                 │─────────────────────────────────►│
 │                  │                  │                 │                  │  (写入 PLC)     │
 │                  │                  │                 │                  │                 │
 │  点击"绝对定位GO"│                  │                 │                  │                 │
 │─────────────────►│                  │                 │                  │                 │
 │                  │ isReadyForTrigger│                 │                  │                 │
 │                  │ = true ✓         │                 │                  │                 │
 │                  │                  │                 │                  │                 │
 │                  │ triggerAbsMove() │                 │                  │                 │
 │                  │─────────────────►│                 │                  │                 │
 │                  │                  │ triggerAbsMove()│                  │                 │
 │                  │                  │────────────────►│                  │                 │
 │                  │                  │                 │ startAbs(id)     │                 │
 │                  │                  │                 │─────────────────►│                 │
 │                  │                  │                 │                  │ step=Ensuring…  │
 │                  │                  │                 │                  │                 │
 │                  │                  │                 │  isLoading=true ◄│                 │
 │                  │◄─ isLoading=true─│◄────────────────│                  │                 │
 │                  │                  │                 │                  │                 │
 │  按钮变"运行中…" │                  │                 │                  │                 │
 │  GO按钮禁用      │                  │                 │                  │                 │
 │                  │                  │                 │                  │                 │
 │  [下一帧 tick()] │                  │                 │                  │                 │
 │                  │                  │                 │ tick()           │                 │
 │                  │                  │                 │─────────────────►│                 │
 │                  │                  │                 │                  │ EnsuringEnabled │
 │                  │                  │                 │                  │ → Triggering…   │
 │                  │                  │                 │                  │ ...             │
 │                  │                  │                 │                  │                 │
 │  [N 帧后...]     │                  │                 │                  │                 │
 │                  │                  │                 │ tick()           │                 │
 │                  │                  │                 │─────────────────►│                 │
 │                  │                  │                 │                  │ step=Done       │
 │                  │                  │                 │  isLoading=false◄│                 │
 │                  │◄─ isLoading=false│◄────────────────│                  │                 │
 │                  │                  │                 │                  │                 │
 │  按钮恢复"绝对定位GO"               │                 │                  │                 │
 │  GO按钮可点击    │                  │                 │                  │                 │
```

---

## 6. 总结

### 6.1 状态判断总览

```
isLoading = (m_absPolicy 的 step ∉ {Initial, Done, Error})
         OR (m_relPolicy 的 step ∉ {Initial, Done, Error})
```

### 6.2 UI 影响矩阵

| 条件 | "设置目标"按钮 | "GO"按钮 |
|------|:--:|:--:|
| 正常（isReady） | ✅ 可点击 | ✅ 可点击 |
| `systemLocked` | ❌ 禁用 | ❌ 禁用 |
| `gantryOperationLocked` | ❌ 禁用 | ❌ 禁用 |
| `hasError` | ❌ 禁用 | ❌ 禁用 |
| `state > 2`（运动中） | ❌ 禁用 | ❌ 禁用 |
| **`isLoading`** | ✅ **仍可点击** | ❌ 禁用 → 文案 "运行中..." |

### 6.3 设计要点

1. **分层清晰**：QML → QtAxisViewModel（代理）→ AxisViewModelCore（判断）→ Policy（状态机）
2. **"设置目标"与"触发移动"分离**：`isLoading` 只阻止触发，不阻止设置目标
3. **排除法判断**：不枚举活跃状态，只排除终态，便于扩展
4. **仅覆盖绝对/相对定位**：点动操作不受 `isLoading` 影响
5. **急停优雅退出**：Policy 跳入 Done → `isLoading` 立即变 false → UI 恢复
