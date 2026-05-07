# 界面集成调试 — TDD 开发计划

> **当前里程碑**：Domain 层（Phase 0–7）已完成（160+ 测试覆盖 20 条业务约束）  
> **下一步行动**：不走重构造路，而是**以 Domain 层为锚点，向下联调 FakePLC，向上联调 QML**  
> **原则**：测试先行，先写 ViewModel 测试，再写 ViewModel 实现，最后连 QML  
> **日期**：2026-05-07

---

## 一、现状分析

### 1.1 当前已完成的内容

| 层级 | 状态 | 说明 |
|------|:----:|------|
| Domain 层（双轴龙门） | ✅ 完成 | GantrySystem 聚合根、PhysicalAxis/LogicalAxis、端口、事件全部到位 |
| Domain 层测试 | ✅ 完成 | 260+ 测试覆盖联动/安全/同步/模式/限位/报警/事件边沿 |
| Infrastructure（FakePLC） | ✅ 完成 | FakePLC、FakeGantryFeedbackPort、FakeGantryCommandPort、FakeGantryEventBus |
| Application 层（单轴） | ✅ 完成 | Enable/Jog/Move/Stop UseCase + Orchestrator |
| Presenter 层（单轴） | ✅ 完成 | AxisViewModelCore → QtAxisViewModel → QML |
| QML UI（单轴） | ✅ 完成 | MainDashboard + ActionControlBlock + TelemetryBlock + AxisSelectorBlock |

### 1.2 当前缺口（"缝"在哪里）

```
当前链路（单轴）：
  FakePLC → Axis UseCase → AxisViewModelCore → QtAxisViewModel → QML TelemetryBlock
              ↑ 这里只有单轴

目标链路（双轴龙门）：
  FakePLC ──┬──→ FakeGantryFeedbackPort ──→ GantrySystem ──→ ???ViewModel ──→ QML
            │        ↑                          ↓
            └──────── FakeGantryCommandPort ←───┘
                                                  ↓
                                             ??? 模式切换
```

**三个关键缺口**：

| # | 缺口 | 位置 | 需要做什么 |
|:-:|------|------|-----------|
| 1 | **缺少 GantrySystem ViewModel** | presentation/ | 目前只有单轴 AxisViewModelCore，没有 GantrySystem 的 ViewModel 包装 |
| 2 | **main.cpp 未接入 GantrySystem** | main.cpp | 只注入了 axisX1VM（单轴），没有注入龙门双轴+联动 |
| 3 | **AxisSelectorBlock 未激活龙门模式** | QML | "X1/X2 联动"按钮 opacity:0.6 + 点击事件空 |

---

## 二、完成原则（避免走弯路）

### 原则 1：不要绕过 Domain 层

"用 FakePLC 调试界面" 的正确做法是：

```
FakePLC → (通过端口) → GantrySystem → (通过新 ViewModel) → QML
```

**不能**：跳过 GantrySystem，直接在 QML 里造假位置数据  
**不能**：跳过 Domain 层安全逻辑，让 QML 直接写 FakePLC 寄存器

### 原则 2：测试先行

每新增一个 ViewModel 方法，必须先写测试：
- 测试 ViewModel 从 GantrySystem 映射正确的状态
- 测试 ViewModel 处理事件（DeviationFault / LimitTriggered 等）
- 测试 ViewModel 命令映射（jog → checkOperability → 命令拆分）

### 原则 3：可逆步骤

每一步都要能使用 `git revert` 回退，不影响 Domain 层测试。

---

## 三、第一阶段（P0）：创建 GantryViewModel 测试 + 实现

### 3.1 文件清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `tests/presentation/viewmodel/test_gantry_viewmodel.cpp` | 测试 | 30+ 测试用例 |
| `presentation/viewmodel/GantryViewModelCore.h` | 实现 | 纯 C++ ViewModel 核心 |
| `presentation/viewmodel/GantryViewModelCore.cpp` | 实现 | Core 实现 |
| `presentation/viewmodel/QtGantryViewModel.h` | 实现 | Qt Q_PROPERTY 包装 |
| `presentation/viewmodel/QtGantryViewModel.cpp` | 实现 | Qt 包装实现 |

### 3.2 GantryViewModelCore API 设计

```cpp
class GantryViewModelCore {
public:
    GantryViewModelCore(GantrySystem& gantry);
    
    // ==== 状态投影（从 GantrySystem 映射到 UI 可消费的形式）====
    
    // 龙门工作模式
    enum class DisplayMode { SingleX1, SingleX2, GantryCoupled, GantryDecoupled };
    DisplayMode displayMode() const;
    
    // 联合状态（从 aggregatedState 映射）
    enum class CombinedState { Idle, Jogging, Moving, Error, LimitBlocked, Alarm };
    CombinedState combinedState() const;
    
    // 位置信息
    double x1Position() const;
    double x2Position() const;
    double xPosition() const;      // 逻辑轴 X 位置（联动模式下）
    double deviation() const;      // 当前偏差 |X1+X2|
    
    // 报警/限位指示灯
    bool isX1Alarm() const;
    bool isX2Alarm() const;
    bool isAnyLimit() const;
    bool isCoupled() const;
    bool isTargetOperable(AxisId target) const;
    
    // 安全状态文本（用于诊断提示）
    std::string safetyStatusText() const;
    
    // ==== 控制指令 ====
    
    // 模式切换
    struct CoupleResult {
        bool accepted;
        std::string reason;
    };
    CoupleResult requestCoupling();
    void requestDecoupling();
    
    // 运动命令
    struct MotionCmdResult {
        bool accepted;
        std::string rejectReason;
        std::string eventType;  // 如果被拒绝，产生的事件类型
    };
    MotionCmdResult jog(AxisId target, MotionDirection dir);
    MotionCmdResult moveAbsolute(AxisId target, double pos);
    MotionCmdResult moveRelative(AxisId target, double delta);
    MotionCmdResult stop(AxisId target);  // 始终 accepted
    
    // ==== 驱动机制 ====
    void tick();  // 每周期调用：syncState → aggregateState → 排空事件 → 通知 QtViewModel
};
```

### 3.3 核心测试清单（TDD Red 阶段）

**目录**：`tests/presentation/viewmodel/test_gantry_viewmodel.cpp`

#### 单元 1：状态投影映射（12 个测试）

| # | 测试名 | 场景 | 预期 |
|:-:|--------|------|------|
| 1 | `DefaultState_Decoupled_X1X2Enabled` | 刚构造完 | `displayMode=GantryDecoupled`, `combinedState=Idle` |
| 2 | `AfterCoupling_DisplayModeGantryCoupled` | requestCoupling 成功 | `displayMode=GantryCoupled`, `isCoupled=true` |
| 3 | `X1Alarm_CombinedStateAlarm` | X1 报警 | `combinedState=Alarm+Label:Alarm`, `safetyStatusText` 包含 "X1" |
| 4 | `X2Limit_CombinedStateLimitBlocked` | X2 限位 | `combinedState=LimitBlocked`, `safetyStatusText` 含 "X2" 限位方向 |
| 5 | `DeviationFault_StateDecoupled` | 偏差超限后 | `displayMode=GantryDecoupled`, 事件包含 DeviationFault |
| 6 | `DeviationDisplayValue` | X1=100, X2=-95 | `deviation()=5.0`（尚未 epsilon 判定） |
| 7 | `CycleLimitedEvent_NoDuplicate` | 限位保持，连续 tick 2 次 | 第一周期"限位被触发"标志 true，第二周期 false |
| 8 | `NormalState_SafetyTextEmpty` | 无报警/无限位 | `safetyStatusText()` 为空或 "Normal" |
| 9 | `X1Position_Passthrough` | X1=200 | `x1Position()=200.0` |
| 10 | `X2Position_Passthrough` | X2=-200 | `x2Position()=-200.0` |
| 11 | `XPosition_InCoupledMode` | Coupled + X1=100 | `xPosition()=100.0`（逻辑位置=X1） |
| 12 | `XPosition_InDecoupledMode_Zero` | Decoupled | `xPosition()=0.0`（逻辑轴不可操作） |

#### 单元 2：模式切换（6 个测试）

| # | 测试名 | 场景 | 预期 |
|:-:|--------|------|------|
| 13 | `RequestCoupling_WhenReady_Accepted` | 双轴使能+对齐 | `accepted=true`, `displayMode=GantryCoupled` |
| 14 | `RequestCoupling_X1NotEnabled_Rejected` | X1 disable | `accepted=false`, `reason` 含 "X1" |
| 15 | `RequestCoupling_AlarmActive_Rejected` | X2 报警 | `accepted=false`, `reason` 含 "Alarm" |
| 16 | `RequestCoupling_Limit_Rejected` | X1 限位 | `accepted=false`, `reason` 含 "Limit" |
| 17 | `RequestCoupling_Deviation_Rejected` | X1=100, X2=10 | `accepted=false`, `reason` 含 "deviation" |
| 18 | `RequestDecoupling_FromCoupled_Decoupled` | 联动→请求解联 | `displayMode=GantryDecoupled` |

#### 单元 3：运动命令映射（8 个测试）

| # | 测试名 | 场景 | 预期 |
|:-:|--------|------|------|
| 19 | `JogX_Forward_InCoupledMode_Accepted` | Coupled + Forward | `accepted=true` |
| 20 | `JogX_Forward_AtX1PosLimit_Rejected` | Coupled + X1 正限位 | `accepted=false`, `rejectReason` 含 "Limit" |
| 21 | `JogX1_InCoupledMode_Rejected` | Coupled + target=X1 | `accepted=false`, `rejectReason` 含 "Mode" |
| 22 | `JogX1_InDecoupledMode_Accepted` | Decoupled + target=X1 | `accepted=true` |
| 23 | `MoveAbsX_InCoupledMode_Accepted` | Coupled + 目标 200 | `accepted=true` |
| 24 | `MoveAbsX_AtLimit_Rejected` | 限位触发 | `accepted=false` |
| 25 | `Stop_AlwaysAccepted` | 任意状态 | `accepted=true` |
| 26 | `Jog_WithAlarm_Rejected` | 报警 | `accepted=false`, `rejectReason` 含 "Alarm" |

#### 单元 4：事件管理（4 个测试）

| # | 测试名 | 场景 | 预期 |
|:-:|--------|------|------|
| 27 | `Tick_ProducesLimitEvent_OnEdge` | 限位 0→1 | 第一 tick 后事件队列含 LimitTriggered |
| 28 | `Tick_NoDuplicateEvent_SecondTick` | 限位持续 | 第二 tick 无新 LimitTriggered |
| 29 | `Tick_ProducesAlarmEvent_OnEdge` | 报警 0→1 | 含 AlarmRaised |
| 30 | `Tick_ProducesDecoupledEvent_OnDeviation` | 偏差超限 | 含 DeviationFault + Decoupled |

### 3.4 QtGantryViewModel 的 Q_PROPERTY 设计

```cpp
class QtGantryViewModel : public QObject {
    Q_OBJECT
    // == 状态投影 ==
    Q_PROPERTY(int displayMode READ displayMode NOTIFY stateChanged)
    Q_PROPERTY(int combinedState READ combinedState NOTIFY stateChanged)
    Q_PROPERTY(double x1Position READ x1Position NOTIFY positionChanged)
    Q_PROPERTY(double x2Position READ x2Position NOTIFY positionChanged)
    Q_PROPERTY(double xPosition READ xPosition NOTIFY positionChanged)
    Q_PROPERTY(double deviation READ deviation NOTIFY deviationChanged)
    Q_PROPERTY(bool isCoupled READ isCoupled NOTIFY stateChanged)
    Q_PROPERTY(bool isX1Alarm READ isX1Alarm NOTIFY alarmChanged)
    Q_PROPERTY(bool isX2Alarm READ isX2Alarm NOTIFY alarmChanged)
    Q_PROPERTY(QString safetyStatus READ safetyStatus NOTIFY safetyChanged)
    Q_PROPERTY(QString eventLog READ eventLog NOTIFY eventLogChanged)

    // == 控制指令 ==
    Q_INVOKABLE void requestCoupling();
    Q_INVOKABLE void requestDecoupling();
    Q_INVOKABLE void jogX(MotionDirection dir);
    Q_INVOKABLE void jogX1(MotionDirection dir);
    Q_INVOKABLE void jogX2(MotionDirection dir);
    Q_INVOKABLE void moveAbsoluteX(double pos);
    Q_INVOKABLE void moveAbsoluteX1(double pos);
    Q_INVOKABLE void moveAbsoluteX2(double pos);
    Q_INVOKABLE void stopAll();
    Q_INVOKABLE QString lastCommandResult() const;

signals:
    void stateChanged();
    void positionChanged();
    void deviationChanged();
    void alarmChanged();
    void safetyChanged();
    void eventLogChanged();
};
```

---

## 四、第二阶段（P1）：集成到 main.cpp

### 4.1 main.cpp 变化

```
修改前：
  Axis axis;
  FakePLC plc;          // 单轴
  QtAxisViewModel qtVM; // 单轴 ViewModel
  engine.rootContext()->setContextProperty("axisX1VM", &qtVM);

修改后：
  // 双轴 FakePLC
  FakePLC plc;
  PhysicalAxis x1(AxisId::X1);
  PhysicalAxis x2(AxisId::X2);
  GantrySystem gantry(x1, x2);
  
  // 龙门 ViewModel（桥接 Domain → QML）
  QtGantryViewModel gantryVM(gantry, plc);
  engine.rootContext()->setContextProperty("gantryVM", &gantryVM);
  
  // 单轴仍然保留（向后兼容）
  QtAxisViewModel axisVM(...);
  engine.rootContext()->setContextProperty("axisX1VM", &axisVM);
```

### 4.2 Tick 循环变化

```
修改前：
  qtVM.tick();
  plc.tick(10);
  syncService.sync(axis, plc.getFeedback());

修改后：
  // 1. 从 FakePLC 读取物理轴反馈 → 写入 PhysicalAxis
  gantryVM.syncFromPLC();
  
  // 2. 龙门状态聚合（限位检测、报警检测、偏差守卫）
  gantryVM.tick();
  
  // 3. 排空事件 → 缓存到 Qt 信号
  gantryVM.drainEvents();
  
  // 4. 如果有未执行命令，通过端口写入 FakePLC
  gantryVM.dispatchCommands();
```

---

## 五、第三阶段（P2）：QML UI 适配

### 5.1 AxisSelectorBlock 激活

**修改点**：`presentation/qml/blocks/AxisSelectorBlock.qml`

```
// 原：X1/X2 联动 → opacity=0.6, onClicked={}
// 改：
AxisItemDelegate {
    name: "X1/X2 联动"
    isActive: gantryVM ? gantryVM.isCoupled : false
    statusText: gantryVM ? (gantryVM.isCoupled ? "联动中" : "待联动") : "未就绪"
    isDual: true
    opacity: 1.0
    onClicked: {
        if (gantryVM && !gantryVM.isCoupled) {
            gantryVM.requestCoupling()
        } else if (gantryVM) {
            gantryVM.requestDecoupling()
        }
    }
}
```

### 5.2 TelemetryBlock 双轴显示

**修改点**：`presentation/qml/blocks/TelemetryBlock.qml`

```
// 原：只显示当前绝对位置 + 状态灯
// 改：根据 gantryVM.displayMode 显示不同信息

// Coupled 模式：
//   - 逻辑轴 X 位置（大数字）
//   - 小字：X1=xxx mm | X2=xxx mm | 偏差=xxx mm
//   - 模式标签：🔗 联动中 / 🔓 分动

// Decoupled 模式：
//   - Tab 切换 X1 / X2 单独显示
//   - 当前选定轴的位置（大数字）
//   - 状态灯：根据 selectedAxis 的状态
```

### 5.3 ActionControlBlock 双轴适配

**修改点**：`presentation/qml/blocks/ActionControlBlock.qml`

```
// 联动模式下：
//   - 模式切换器下方增加"模式"标签 → "🔗 联动" / "🔓 分动"
//   - Jog / Move 目标固定为 X（逻辑轴）
//   - 隐藏 X1/X2 独立操作按钮

// 分动模式下：
//   - axisSelector 指定当前操作的目标轴
//   - Jog / Move 目标为 X1 或 X2（取决于选择）
```

---

## 六、增量测试计划

### 6.1 新增测试文件

| 文件 | 测试数 | 说明 |
|------|:------:|------|
| `tests/presentation/viewmodel/test_gantry_viewmodel.cpp` | 30+ | GantryViewModelCore 测试（P0） |
| `tests/presentation/viewmodel/test_qt_gantry_viewmodel.cpp` | 10+ | QtGantryViewModel Q_PROPERTY 测试（P1） |
| `tests/infrastructure/test_gantry_plc_integration.cpp` | 8+ | main.cpp 集成场景测试（P1） |

### 6.2 不需要修改的测试

- **所有 Domain 层测试**（260+）：不动一行
- **所有 Application 单轴测试**：不变
- **所有原 Fake 基础设施测试**：不变

### 6.3 最终测试总数

| 阶段 | 新增 | 累积 |
|:----:|:----:|:----:|
| 当前 | 260+ | 260+ |
| P0 | 30+ | 290+ |
| P1 | 18+ | 308+ |
| P2 | 0（QML 无测试） | 308+ |

---

## 七、实施顺序与依赖

```
P0: GantryViewModel 测试 + 实现
  ├── test_gantry_viewmodel.cpp（先写）
  ├── GantryViewModelCore.h/cpp（实现）
  └── QtGantryViewModel.h/cpp（Qt 包装）
  └── 依赖：GantrySystem（已有）、IGantryStateQuery（已有）
  └── 依赖：FakeGantryFeedbackPort（已有）

P1: main.cpp 集成
  ├── test_gantry_plc_integration.cpp（先写）
  ├── main.cpp（修改）
  └── 依赖：P0 完成
  └── 依赖：FakePLC、FakeGantryCommandPort（已有）

P2: QML UI 适配
  ├── AxisSelectorBlock.qml（修改）
  ├── TelemetryBlock.qml（修改）
  ├── ActionControlBlock.qml（修改）
  └── 依赖：P1 完成，gantryVM 已注入 QML 上下文
```

---

## 八、安全门控（评审标准）

进入 P1 的前提：
- [ ] P0 的 30 个测试全部通过（Green）
- [ ] GantryViewModelCore 覆盖所有 AxisId（X1/X2/X）的操作映射
- [ ] QtGantryViewModel 的所有 Q_PROPERTY 信号在状态变化时正确触发

进入 P2 的前提：
- [ ] P1 集成测试通过
- [ ] main.cpp 启动后 GantrySystem 能通过 FakePLC 正确同步状态
- [ ] 手动运行后能在 Console 看到 GantrySystem 状态变化日志

验收标准（P2 完成）：
- [ ] QML 启动后能看到 X1/X2 实时位置
- [ ] 点击"X1/X2 联动"按钮后，系统进入 Coupled 模式（如果条件满足）
- [ ] 手动设置 FakePLC 限位，QML 限位指示灯亮起
- [ ] 手动触发偏差，系统自动解联，QML 状态更新
- [ ] 急停按钮在任何状态下都能停止所有运动

---

## 九、与原始 TDD 设计文档的关系

**原始 `docs/domain_layer_tdd_design.md` 中 Phase 8–12 保持不变**，仍然保留在文件中。

本计划是**在当前 Domain 层完成（Phase 0-7）和 Phase 8-12 之间的一个过渡阶段**，优先级高于 Phase 8，因为：
- 它能提供"可见的反馈"——看到龙门在 UI 上动起来
- 它不会破坏任何现有 Domain 层的抽象（GantryDomainService 可后续再抽）
- 它为 Phase 8 提供了"真实的集成测试场景"

```
原始计划：
  Phase 0-7（Domain 核心） → Phase 8（GantryDomainService）→ Phase 9（端口重构）→ ...

调整后：
  Phase 0-7（Domain 核心） → 【UI 集成调试 ← 当前】 → Phase 8 → Phase 9 → ...
```

---

## 十、一句话总览

> **当前不做重构，走 Show, don't tell 路线：用测试驱动的 GantryViewModel 把 Domain 层的双轴龙门逻辑真实连接到 FakePLC 和 QML，让系统"活起来"。**
