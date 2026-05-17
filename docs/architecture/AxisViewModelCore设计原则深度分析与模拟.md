# AxisViewModelCore 设计原则深度分析与模拟

> 针对核心设计原则中「原则 3：一个 ViewModel = 一组 + 一轴」的深度阐述，
> 以及 UI 控制 B 组 Y 轴点动的完整数据流模拟。

---

## 一、为什么「一个 ViewModel = 一组 + 一轴」？

### 1.1 底层世界模型

参考 `SystemIntegrationTest` 的世界模型。关键认知：**每个 `SystemContext` 在构造期无条件初始化全部 6 个轴实体**（X/X1/X2/Y/Z/R）+ 龙门控制 + 急停控制。分组之间的差异**不在于"拥有哪些轴"，而在于接入了不同的物理 driver（plcA / plcB）**。

```C++
SystemContext::SystemContext() {
    // 1. 初始化 6 个固定轴实体（所有分组都一样）
    m_axes[AxisId::X]  = std::make_unique<Axis>();
    m_axes[AxisId::X1] = std::make_unique<Axis>();
    m_axes[AxisId::X2] = std::make_unique<Axis>();
    m_axes[AxisId::Y]  = std::make_unique<Axis>();
    m_axes[AxisId::Z]  = std::make_unique<Axis>();
    m_axes[AxisId::R]  = std::make_unique<Axis>();
    // 2. 龙门联动控制器
    // 3. 龙门电机控制器
    // 4. 急停控制器（值语义，SystemContext 组合持有）
}
```

```
SystemManager                    ← 全局单例，管理所有分组
├── "Machine_A" → SystemContext_A (driver → plcA)
│   ├── X, X1, X2, Y, Z, R   （全部 6 轴）
│   ├── GantryCouplingController
│   ├── GantryPowerController
│   └── EmergencyStopController
│
└── "Machine_B" → SystemContext_B (driver → plcB)
    ├── X, X1, X2, Y, Z, R   （全部 6 轴）
    ├── GantryCouplingController
    ├── GantryPowerController
    └── EmergencyStopController
```

关键事实：
- **分组是物理隔离的（driver 层面）**：Machine_A 和 Machine_B 有不同的 `FakePLC`、`FakeAxisDriver`、物理寄存器，但拥有**相同的轴命名空间**（6 个轴 ID 都存在于两组内）。
- **同一分组内的轴共享安全状态**：Machine_A 的急停会同时锁定该分组内全部 6 个轴。
- **龙门有特殊的联动约束**：Machine_B 的龙门 X1/X2 在联动时被 `GantryCouplingController` 拦截，但不影响 Machine_A 的龙门轴（两组独立）。
- **"某个分组不使用的轴"处于静默状态**：例如 `SystemIntegrationTest` 中 Machine_A 主要操作 Y/Z/R，Machine_B 主要操作 X/X1/X2，但未被操作的轴（如 Machine_A 的 X1/X2）依然存在，只是没有被 UI 面板绑定而已。

### 1.2 反模式：`AxisViewModelCore(Axis& axis, ...)` 单轴硬绑定

```
旧设计:
AxisViewModelCore vm(yAxis, jogOrch, absOrch, relOrch, stopUc);
                                ↑
                        构造函数期写死引用
```

致命问题：

| 场景 | 旧设计如何应对 | 结果 |
|------|---------------|------|
| 需要控制 Machine_A 的 Y 轴 | 创建一个 ViewModel | ✅ 勉强可用 |
| 需要同时控制 Machine_A 的 Y 和 Z | 创建两个 ViewModel | ❌ 但 jogOrch/absOrch/relOrch 是外部传入的，谁持有它们？ |
| 需要切换到 Machine_B 的 X1 | 销毁再创建？ | ❌ 无 groupName 概念，完全无法路由 |
| Machine_B 的 X1 正联动中，UI 要读 X 逻辑轴 | — | ❌ 没有 SystemContext，不知道龙门状态 |

**根本原因**：ViewModel 应该是一个"访问器"（Accessor），而不是"持有者"（Owner）。它应该能通过 `(manager, groupName, axisId)` 三元组动态解析到任意一个轴，而不是在构造期固化。

### 1.3 正解：三元组动态路由

```
新设计:
AxisViewModelCore vm(manager, "Machine_B", AxisId::Y);
                      ↑                ↑
              靠 manager 动态解析   靠 groupName 路由到正确的 SystemContext
```

每次执行操作时，ViewModel 内部流程：

```
vm.enable()
  → m_manager.tryGetGroup("Machine_B")    // 解析分组
  → group->tryGetAxis(AxisId::Y)          // 解析轴（含龙门/安全拦截）
  → EnableUseCase.execute(manager, "Machine_B", AxisId::Y, true)
```

这带来了**组合爆炸式的灵活性**：不需要为每种 `{组, 轴}` 组合写单独的类，同一个 `AxisViewModelCore` 类可以服务所有组合。

### 1.4 与 QML 的映射关系

QML 侧通过 Repeater / ListView 动态创建轴面板：

```qml
// 伪 QML 示意
Repeater {
    model: axisListModel  // [{groupName:"Machine_A", axisId:"Y"}, ...]
    delegate: AxisPanel {
        // 每个 AxisPanel 背后有一个 QtAxisViewModel 实例
        // QtAxisViewModel 内部持有 AxisViewModelCore(manager, groupName, axisId)
    }
}
```

**一个 AxisPanel = 一个 QtAxisViewModel = 一个 AxisViewModelCore = (groupName, axisId)**

---

## 二、实现后的结构总览

### 2.1 类关系图（实例层级）

```
                        ┌──────────────────────────┐
                        │     SystemManager         │ 全局单例
                        │  (分组注册表)              │
                        └─────┬──────────┬─────────┘
                              │          │
              ┌───────────────┤          ├───────────────┐
              │               │          │               │
     ┌────────▼────────┐     ┌▼──────────▼──┐    ┌───────▼──────────┐
     │ SystemContext A  │     │ SystemContext B│   │ SystemContext C │
     │ ("Machine_A")    │     │ ("Machine_B") │   │ (未来扩展)       │
     │                  │     │               │   │                 │
     │ axisMap (全6轴):│     │ axisMap (全6轴):│  │                 │
     │  X,X1,X2,Y,Z,R  │     │  X,X1,X2,Y,Z,R │   │                 │
     │ (测试中主要      │     │ (测试中主要     │   │                 │
     │  使用 Y/Z/R)     │     │  使用 X/X1/X2) │   │                 │
     │                  │     │               │   │                 │
     │ driver → driverA │     │ driver → driverB│  │                 │
     └───┬────┬────┬────┘     └──┬───┬───┬────┘   └─────────────────┘
         │    │    │             │   │   │
         │    │    │             │   │   │
    ┌────▼──┐ ┌▼──┐ ┌▼──┐  ┌───▼─┐ ┌▼─┐ ┌▼──┐
    │vmY_A  │ │vmZ│ │vmR│  │vmX1 │ │vm│ │vmX│
    │(A,Y)  │ │(A,Z)│ │(A,R)│ │(B,X1)│ │(B,X2)│ │(B,X)│
    └───────┘ └───┘ └───┘  └─────┘ └──┘ └───┘
    每个 vm 独立持有：
    - m_groupName (string)
    - m_axisId    (AxisId)
    - 5 个值语义 UseCase
    - 3 个 unique_ptr<Orchestrator>
    - m_lastError (ViewModelError)

    ┌──────────────┐  ┌──────────────┐
    │ GantryVM_B   │  │ ESTOP_VM_B  │
    │ (B)          │  │ (B)          │
    └──────────────┘  └──────────────┘
    分组级 ViewModel（不需要 axisId）
```

### 2.2 内存与生命周期

```
启动时：
  创建 SystemManager
  创建 FakePLC_A, FakePLC_B
  创建 FakeAxisDriver_A, FakeAxisDriver_B
  创建 SystemContext_A, 注册 driverA
  创建 SystemContext_B, 注册 driverB
  manager.createGroup("Machine_A")  → 持有 ctxA
  manager.createGroup("Machine_B")  → 持有 ctxB

UI 加载时（QML 解析）：
  为 Machine_A 的 Y  创建 QtAxisViewModel(manager, "Machine_A", Y)   → 内部 new AxisViewModelCore
  为 Machine_A 的 Z  创建 QtAxisViewModel(manager, "Machine_A", Z)   → 内部 new AxisViewModelCore
  为 Machine_B 的 X1 创建 QtAxisViewModel(manager, "Machine_B", X1)  → 内部 new AxisViewModelCore
  为 Machine_B 的 X2 创建 QtAxisViewModel(manager, "Machine_B", X2)  → 内部 new AxisViewModelCore
  为 Machine_B 的 X  创建 QtAxisViewModel(manager, "Machine_B", X)   → 内部 new AxisViewModelCore
  为 Machine_B 创建    QtGantryViewModel(manager, "Machine_B")
  为 Machine_B 创建    QtEmergencyStopViewModel(manager, "Machine_B")

主循环（Timer 或 event loop）：
  driverA.pollFeedback(*ctxA)  // 一次，更新 A 组所有轴
  driverB.pollFeedback(*ctxB)  // 一次，更新 B 组所有轴
  for (auto* vm : allViewModels) {
      vm->tick();              // 驱动各自的 Orchestrator
      vm->emitSignalsIfChanged();
  }
```

关键：**6 个 AxisViewModelCore 实例共享同一个 `SystemManager&` 引用，不复制任何底层数据。**

---

## 三、模拟：UI 控制 Machine_A 的 Y 轴进行点动

> 每个 SystemContext 都拥有全部 6 个轴（X/X1/X2/Y/Z/R），因此 UI 可以绑定任意 `{组, 轴}` 组合。
> 此模拟以 Machine_A 的 Y 轴为例，展示从 QML 按钮 → ViewModel → UseCase → driver → PLC 的完整数据流。
> SystemIntegrationTest 中 Machine_A 主要使用 Y/Z/R，但 Y 轴在 ctxA 和 ctxB 内都是合法可操作的实体。

### 3.1 初始化：世界状态

```
═══════════════════════════════════════════════════════════════
 时间 T=0（启动后，所有轴 Disabled）
═══════════════════════════════════════════════════════════════

SystemManager
├── "Machine_A" → ctxA (driver → plcA)
│   ├── X, X1, X2, Y, Z, R （全部 6 轴，均 Disabled, pos=0.0）
│   │   在 SystemIntegrationTest 中主要使用 Y, Z, R
│   ├── GantryCouplingController
│   ├── GantryPowerController
│   └── EmergencyStopController
└── "Machine_B" → ctxB (driver → plcB)
    ├── X, X1, X2, Y, Z, R （全部 6 轴，均 Disabled, pos=0.0）
    │   在 SystemIntegrationTest 中主要使用 X, X1, X2
    ├── GantryCouplingController
    ├── GantryPowerController
    └── EmergencyStopController

已创建的 ViewModel 实例：
  vm_Y_A   = AxisViewModelCore(manager, "Machine_A", AxisId::Y)    ← 主角
  vm_Z_A   = AxisViewModelCore(manager, "Machine_A", AxisId::Z)
  vm_X1_B  = AxisViewModelCore(manager, "Machine_B", AxisId::X1)
  vm_X2_B  = AxisViewModelCore(manager, "Machine_B", AxisId::X2)
  vm_gantry_B = GantryViewModelCore(manager, "Machine_B")

QML 状态：
  AxisPanel_Y_A.errorCode = ""           ← 空字符串，无错误
  AxisPanel_Y_A.errorUserMessage = ""
  AxisPanel_Y_A.state = "Disabled"
  AxisPanel_Y_A.absPos = "0.0 mm"
  AxisPanel_Y_A.enabled = false
```

### 3.2 T=1：用户点击"使能"按钮

```
═══════════════════════════════════════════════════════════════
 时间 T=1
 QML: AxisPanel_Y_A 的"使能"按钮被点击
═══════════════════════════════════════════════════════════════

【QML 层】
  Button.onClicked → QtAxisViewModel_Y_A.enable()

【QtAxisViewModel 层】  (QtAxisViewModel.h)
  void enable() { m_core->enable(); emit enabledChanged(); }

【AxisViewModelCore 层】  (AxisViewModelCore.cpp)
  void enable() {
      UseCaseError err = m_enableUc.execute(m_manager, "Machine_A", AxisId::Y, true);
      //                                    ─────────┬─────────  ──┬──
      //                                      manager 持有所有分组  │
      //                                                         轴ID
      if (!std::holds_alternative<std::monostate>(err)) {
          m_lastError = translate(err);
          m_hasError = true;
          // ↓ 触发 errorChanged 信号，QML 面板显示错误
      }
  }

【EnableUseCase 层】  (EnableUseCase.h)
  UseCaseError execute(manager, "Machine_A", AxisId::Y, true) {

      // 阶段 0：分组查找
      SystemContext* group;
      if (!manager.tryGetGroup("Machine_A", group, reason))
          return ContextRejection::GroupNotFound;  // ← 此时 group=ctxA

      // 阶段 1：轴获取（含龙门/安全拦截）
      Axis* axis;
      if (!group->tryGetAxis(AxisId::Y, axis, reason))
          return ContextRejection::...;  // 如 PhysicalAxisLockedByGantry

      // 阶段 2：领域层状态判定
      if (!axis->enable(true))
          return axis->lastRejection();  // InvalidState / AlreadyMoving

      // 阶段 3：通过统一命令总线包装下发
      if (axis->hasPendingCommand()) {
          drv = group->driver();  // → FakeAxisDriver_A
          drv->send(AxisCommandWithId{AxisId::Y, axis->getPendingCommand()});
          //                            ──┬──  ──────────┬──────────
          //                          轴ID     EnableCommand {active:true}
      }

      return std::monostate{};  // ✅ 成功
  }

【FakeAxisDriver 层】  (FakeAxisDriver.h)
  send(AxisCommandWithId{AxisId::Y, EnableCommand{true}})
    → plcA.writeCommand(AxisId::Y, EnableCommand{true})
    → 设置 PLC 内部寄存器 Modbus[Y_ENABLE_REG] = 1

【FakePLC 层】  (FakePLC.h)
  内部状态变更：
    axisStates[Y].enablePending = true
    axisStates[Y].enableCounter = 15  (150ms 延迟)

═══════════════════════════════════════════════════════════════
 时间 T=1 结束时的状态
═══════════════════════════════════════════════════════════════

  轴 Y 发送了 EnableCommand
  UseCase 返回 monostate（成功）
  ViewModel 未设置错误
  QML 面板尚无可见变化（轴还在 Enabling 状态）
```

### 3.3 T=2~20：主循环推进，等待使能完成

```
═══════════════════════════════════════════════════════════════
 时间 T=2 到 T=20（每个 tick = 10ms）
 主循环：pollFeedback → tickAllViewModels → emitSignals
═══════════════════════════════════════════════════════════════

【主循环 — 每帧】
  driverA.pollFeedback(*ctxA);   // ← 关键：统一反馈泵送
    → plcA.tick() 推进物理模拟
    → 读取 PLC 寄存器状态
    → 分发反馈到 ctxA 内的各轴（通过 axisMap）

  vm_Y_A.tick();                // 驱动 Orchestrator（当前为空，无操作）
  vm_Y_A.emitSignalsIfChanged(); // Qt 属性变更检测

【FakePLC 内部 — T=2..T=15】
  axisStates[Y].enableCounter: 15 → 14 → ... → 1
  状态：Enabling

【FakePLC 内部 — T=16】
  enableCounter 归零
  axisStates[Y].state = AxisState::Idle  ← 使能完成！
  axisStates[Y].actualPosition = 0.0
  axisStates[Y].isEnabled = true

【pollFeedback 在 T=16 时检测到状态变化】
  FakeAxisDriver::pollFeedback(ctxA)
    → 遍历 ctxA 的所有轴
    → 读取 plcA 的 Y 轴反馈
    → Axis::applyFeedback(...) 更新 Y 轴内部状态
    → Y.state() = Idle ✅

【QtAxisViewModel — emitSignalsIfChanged】
  检测到 state 从 Disabled → Idle
    → emit stateChanged()
  检测到 enabled 从 false → true
    → emit enabledChanged()

═══════════════════════════════════════════════════════════════
 时间 T=16 结束时的 QML 状态
═══════════════════════════════════════════════════════════════

  AxisPanel_Y_A.state = "Idle"      ← 从 Disabled 变为 Idle
  AxisPanel_Y_A.enabled = true      ← 使能按钮变灰/高亮
  AxisPanel_Y_A.absPos = "0.0 mm"
  AxisPanel_Y_A.errorCode = ""      ← 无错误
```

### 3.4 T=21：用户按下正向点动按钮

```
═══════════════════════════════════════════════════════════════
 时间 T=21
 QML: 用户按下 Jog+ 按钮（MouseArea.onPressed）
═══════════════════════════════════════════════════════════════

【QML 层】
  MouseArea.onPressed → QtAxisViewModel_Y_A.jogPositivePressed()

【QtAxisViewModel 层】
  void jogPositivePressed() { m_core->jogPositivePressed(); }

【AxisViewModelCore 层】
  void jogPositivePressed() {
      // 1. 创建 TraceScope（用于日志关联）
      TraceScope scope("Machine_A", "Y", generateTraceId());

      // 2. 调用 JogAxisUseCase（直接下发点动指令）
      auto err = m_jogUc.execute(m_manager, "Machine_A", AxisId::Y, Direction::Forward);
      if (!std::holds_alternative<std::monostate>(err)) {
          m_lastError = translate(err);
          m_hasError = true;
          return;  // ← 错误已在 QML 内联显示
      }

      // 3. 通知 JogOrchestrator 开始编排（用于后续的停止→掉电流程）
      m_jogOrch->startJog(AxisId::Y, Direction::Forward);
      //           ↓
      //  JogOrchestrator 内部：
      //    m_step = Step::EnsuringEnabled
      //    m_targetId = AxisId::Y
      //    m_dir = Direction::Forward
      //    (此时轴已 Idle，所以 Orchestrator 状态机快速推进)
  }

【JogAxisUseCase 层】  (JogAxisUseCase.h)
  UseCaseError execute(manager, "Machine_A", AxisId::Y, Direction::Forward) {

      // 阶段 0：分组查找
      SystemContext* group = ctxA;

      // 阶段 1：轴获取
      Axis* axis = ctxA->axisMap[AxisId::Y];

      // 阶段 2：领域层判定
      axis->jog(Direction::Forward)
        → Axis::jog() 检查：
          - state == Idle? → ✅ 是
          - 正向限位？ → 当前位置 0.0，正限位 +1000.0 → ✅ 未触发
          → 生成 JogCommand{active: true, direction: Forward}
          → 存入 pendingCommand

      // 阶段 3：下发命令
      driverA->send(AxisCommandWithId{AxisId::Y, JogCommand{true, Forward}})
        → plcA.writeCommand(AxisId::Y, JogCommand{true, Forward})
        → PLC 内部：
            axisStates[Y].jogActive = true
            axisStates[Y].jogDirection = Forward
            axisStates[Y].state = AxisState::Jogging

      return std::monostate{};  // ✅
  }

═══════════════════════════════════════════════════════════════
 时间 T=21 结束时的状态
═══════════════════════════════════════════════════════════════

  轴 Y 已进入 Jogging 状态
  JogOrchestrator 内部状态机 = Jogging（快速跳过了 EnsuringEnabled→IssuingJog）
```

### 3.5 T=22~50：持续点动，位置变化

```
═══════════════════════════════════════════════════════════════
 时间 T=22 到 T=50（持续按下中）
 每帧：pollFeedback → tickAllViewModels → emitSignals
═══════════════════════════════════════════════════════════════

【主循环 — 每帧】
  driverA.pollFeedback(*ctxA);
    → plcA.tick()
    → Y.jogActive == true, jogDirection == Forward
    → 速度：10.0 mm/tick（假设）
    → Y.actualPosition += 10.0

  vm_Y_A.tick()
    → m_jogOrch->tick()
        → JogOrchestrator::tick() case Step::Jogging:
            → axis->state() == Jogging → 正常，保持 Jogging
    → m_jogOrch 无错误 → 无错误上报

  vm_Y_A.emitSignalsIfChanged()
    → absPos: 0.0 → 10.0 → 20.0 → ... → 300.0
    → 每秒触发一次 absPosChanged() 信号（节流）

═══════════════════════════════════════════════════════════════
 时间 T=50 (300ms 后)
═══════════════════════════════════════════════════════════════

  QML:
    AxisPanel_Y_A.state = "Jogging"
    AxisPanel_Y_A.absPos = "300.0 mm"     ← 实时滚动
    AxisPanel_Y_A.enabled = true
```

### 3.6 T=51：用户释放点动按钮

```
═══════════════════════════════════════════════════════════════
 时间 T=51
 QML: 用户释放 Jog+ 按钮（MouseArea.onReleased）
═══════════════════════════════════════════════════════════════

【QML 层】
  MouseArea.onReleased → QtAxisViewModel_Y_A.jogPositiveReleased()

【QtAxisViewModel 层】
  void jogPositiveReleased() { m_core->jogPositiveReleased(); }

【AxisViewModelCore 层】
  void jogPositiveReleased() {
      TraceScope scope("Machine_A", "Y", traceId);

      // 1. 通知 JogOrchestrator 开始停止流程
      m_jogOrch->stopJog(AxisId::Y, Direction::Forward);
      //           ↓
      // JogOrchestrator::stopJog(id, dir)
      //   → 检查 id==m_targetId && dir==m_dir → ✅ 匹配
      //   → m_step 当前为 Jogging → 更新为 IssuingStop
  }

═══════════════════════════════════════════════════════════════
 时间 T=51 后的状态（Orchestrator 接管停止流程）
═══════════════════════════════════════════════════════════════

  JogOrchestrator 状态机：
    m_step = IssuingStop  ← 下一步 tick() 执行停止
```

### 3.7 T=52~70：Orchestrator 编排停止 → 等待空闲 → 掉电

```
═══════════════════════════════════════════════════════════════
 时间 T=52
 vm_Y_A.tick() → m_jogOrch->tick()
═══════════════════════════════════════════════════════════════

【JogOrchestrator::tick() — case Step::IssuingStop】
  // 第 0 层：分组解析
  manager.tryGetGroup("Machine_A", group) → ctxA ✅

  // 第 1 层：轴获取（含龙门/安全拦截）
  group->tryGetAxis(AxisId::Y, axis) → ✅

  // 全局错误检测
  axis->state() == Error? → ❌ 否

  // 下发停止指令
  if (!m_stopIssued) {
      axis->stopJog(Direction::Forward)
        → Axis::stopJog() 生成 JogCommand{active: false, dir: Forward}
        → driverA->send(AxisCommandWithId{Y, JogCommand{false, Forward}})
        → plcA 收到停止指令，开始减速

      m_stopIssued = true;
  }
  m_step = Step::WaitingForIdle;

═══════════════════════════════════════════════════════════════
 时间 T=53..T=70 (等待轴停稳 + 掉电)
═══════════════════════════════════════════════════════════════

【每帧 tick】
  driverA.pollFeedback(*ctxA);
    → plcA 减速模拟：速度 10→8→5→2→0
    → Y.state: Jogging → Stopping → Idle

  vm_Y_A.tick()
    → T=53..T=68: 仍在 WaitingForIdle
    → T=69: axis->state() == Idle → m_step = EnsuringDisabled
    → T=69: EnableUseCase.execute(manager, "Machine_A", Y, false)
            → axis->enable(false) → ✅
            → driverA->send(AxisCommandWithId{Y, DisableCommand})
            → plcA 收到掉电指令

    → T=70: axis->state() == Disabled → m_step = Done ✅
            LOG_SUMMARY: "Jog → SUCCESS (Safely Stopped)"

═══════════════════════════════════════════════════════════════
 时间 T=70 结束时的最终状态
═══════════════════════════════════════════════════════════════

  QML:
    AxisPanel_Y_A.state = "Disabled"
    AxisPanel_Y_A.absPos = "300.0 mm"
    AxisPanel_Y_A.enabled = false
    AxisPanel_Y_A.errorCode = ""       ← 无错误
```

---

## 四、错误场景模拟：点动时被限位拦截

```
═══════════════════════════════════════════════════════════════
 假设：Y 轴已运动到正向限位 +1000.0，用户再次按下 Jog+
═══════════════════════════════════════════════════════════════

【AxisViewModelCore::jogPositivePressed()】
  auto err = m_jogUc.execute(manager, "Machine_A", AxisId::Y, Direction::Forward);
  // err = RejectionReason::AtPositiveLimit

  if (!std::holds_alternative<std::monostate>(err)) {
      m_lastError = translate(err);
      //           = std::visit → {
      //               code:        "AXIS_AT_POSITIVE_LIMIT"
      //               userMessage: "轴已到达正向限位"
      //               debugMessage:"Axis is at positive limit, forward motion blocked"
      //               category:    ErrorCategory::Inline
      //             }
      m_hasError = true;
      // ↓ QtAxisViewModel 发出 errorChanged()
  }

【QML 响应】
  AxisPanel_Y_A.errorCode = "AXIS_AT_POSITIVE_LIMIT"
  AxisPanel_Y_A.errorUserMessage = "轴已到达正向限位"

  QML 根据 errorCategory == "Inline"：
    → 在轴面板旁显示橙色内联提示（不弹窗）
    → Jog+ 按钮图标变灰（根据 errorCode 做行为分发）

  QML 根据 errorCode 做图标/行为分发：
    switch(errorCode):
      case "AXIS_AT_POSITIVE_LIMIT":  图标=⛔, Jog+ 按钮禁用
      case "COMM_NETWORK_ERROR":      图标=🔌, 弹窗警告
      case "CTX_SAFETY_LOCKED":       图标=🛑, 全局红色遮罩
```

---

## 五、分组隔离模拟：Machine_A 点动同时 Machine_B 运动

```
═══════════════════════════════════════════════════════════════
 时间 T=100
 用户同时在两个面板操作：
   Panel A 的 Y 轴：按下 Jog+
   Panel B 的 X 轴：点击 MoveAbs(500.0)
═══════════════════════════════════════════════════════════════

【主循环 — 每帧】
  driverA.pollFeedback(*ctxA);   // ← 推进 A 组 PLC
    → Y 轴位置变化 (Jogging)
  driverB.pollFeedback(*ctxB);   // ← 推进 B 组 PLC（独立！）
    → X 轴位置变化 (MovingAbsolute)

  vm_Y_A.tick();                // ← JogOrchestrator A 推进
    → Jogging → 位置 300→310→...
  vm_X_B.tick();                // ← AbsOrch B 推进
    → MovingAbsolute → 位置 0→50→...

  vm_Y_A.emitSignalsIfChanged(); // X 面板看不到 Y 面板的变化
  vm_X_B.emitSignalsIfChanged(); // Y 面板看不到 X 面板的变化

═══════════════════════════════════════════════════════════════
 关键：pollFeedback 是分组的！
  driverA 驱动 ctxA 内全部 6 个轴（X/X1/X2/Y/Z/R），但测试中只使用 Y/Z/R
  driverB 驱动 ctxB 内全部 6 个轴（X/X1/X2/Y/Z/R），但测试中只使用 X/X1/X2
  driver 按组隔离，一个 PLC 的故障不影响另一个 PLC 控制的轴！
  被静默的轴（如 Machine_A 的龙门轴）依然存活于 ctxA 内，
  pollFeedback 同样会更新它们的状态，只是没有 ViewModel 绑定而已。
═══════════════════════════════════════════════════════════════
```

---

## 六、总结：为什么「一个 ViewModel = 一组 + 一轴」是对的

| 维度 | 论证 |
|------|------|
| **底层世界模型** | SystemContext 天然按组隔离轴，每组有独立的 driver/PLC，ViewModel 必须能路由到正确的组 |
| **物理隔离** | Machine_A 和 Machine_B 是不同的物理设备，pollFeedback 按组独立，不可混用 |
| **组合爆炸** | 不需要为 `{A,Y} {A,Z} {B,X1} {B,X2} {B,X}` 写 5 个类，一个 AxisViewModelCore 搞定全部 |
| **UI 映射** | QML Repeater 动态创建面板，每个面板绑定一个 `QtAxisViewModel`，天然就是 N×M 模式 |
| **生命周期** | ViewModel 是轻量的"访问器"，共享 SystemManager& 引用，创建/销毁成本极低 |
| **龙门约束** | 同一个轴在不同分组中的语义不同（Machine_B 的 X1 在联动时锁定），ViewModel 通过 groupName 路由到正确的 SystemContext 自动获得正确的拦截逻辑 |
