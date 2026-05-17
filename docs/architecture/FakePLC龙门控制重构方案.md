# FakePLC 龙门控制重构方案

> **版本**: v1.0
> **日期**: 2026-05-16
> **关联文档**: `docs/第7阶段：双轴同步龙门轴/FakePLC中的状态缺陷.md`

---

## 一、现状概述

### 1.1 当前架构

```text
┌──────────────────────────────────────────────┐
│                  FakePLC                       │
│                                                │
│  onGantryCommand()  ──→  m_gantry*Target      │
│                                                │
│  tickGantry(ms):                              │
│    if (pending) {                             │
│      timer += ms;                             │
│      if (timer >= DELAY) {                    │
│        feedback = target;  // 直接赋值!       │
│        pending = false;                       │
│      }                                        │
│    }                                          │
└──────────────────────────────────────────────┘
```

**本质**：命令 → 延迟 → 直接修改 feedback（"命令回显器"）

### 1.2 领域层已具备的能力

FakePLC 的上层（Domain）已经构建了完善的异步状态机框架：

| 组件 | 职责 | 状态 |
|------|------|------|
| `GantryCouplingController` | 五态耦合状态机 + 意图生成 + errorCode 翻译 | ✅ 已完成 |
| `GantryCouplingState` | 五态内核（NotSynchronized/Decoupled/CouplingRequested/Coupled/DecouplingRequested） | ✅ 已完成 |
| `GantryPowerController` | 五态电机使能状态机 | ✅ 已完成 |
| `GantryRejection` | 拒因枚举（含 PLC 1~5 + 领域 100~999） | ✅ 已完成 |
| `GantryFeedback` | 反馈 DTO（enable / isCoupled / errorCode） | ✅ 已完成 |

领域层已经做好接收真实 PLC 错误码的准备了——但 FakePLC 从未产生这些错误码。

---

## 二、核心问题映射

将缺陷文档中列出的 10 个问题映射到 FakePLC 的具体缺失：

| # | 缺陷 | FakePLC 现状 | 影响范围 |
|---|------|-------------|---------|
| 1 | 联动条件检查 | 无条件直接成功 | `applyFeedback` 的 errorCode 分支从未被触发 |
| 2 | 基于轴状态的反馈生成 | `m_gantryFeedback.enable` 只有 bool | 无法表达"正在运动/报警/超差"等中间态 |
| 3 | 联动状态自动掉线 | `isCoupled=true` 后永不变化 | 无法测试超差→自动解耦流程 |
| 4 | 超差监测 | 未计算 `|X1.pos - X2.pos|` | `GantryRejection::PositionToleranceExceeded` 永不产生 |
| 5 | 命令拒绝 | 100ms 后必然成功 | 错误恢复/重试/超时逻辑全部失真 |
| 6 | 扫描周期行为 | 仅处理 pending timer | 无命令时 feedback 永不变化 |
| 7 | 轴状态聚合 | 龙门 feedback 与轴状态割裂 | X1 报警后龙门 feedback 无感知 |
| 8 | 联动模式约束 | 无 `if (!isCoupled) reject` | 解耦态下的绝对定位应被拒绝 |
| 9 | 联动/分动互斥 | 无运动互斥逻辑 | 联动态下独立点动 X1/X2 应被拒绝 |
| 10 | 联动中间态 | 延迟→直接成功 | 无法模拟"等待双轴停止→检查位置差→同步→建立电子齿轮" |

---

## 三、重构目标

将 FakePLC 从 **"命令延迟器"** 升级为 **"PLC 状态机 + 物理仿真器"**：

```text
控制命令（Command Register）
        ↓
PLC 内部逻辑层（NEW：条件检查 + 状态机 + 扫描周期）
        ↓
物理状态（Physical State）
        ↓
状态/反馈寄存器（Status Register = GantryFeedback）
```

**关键原则**：
1. FakePLC 必须能够 **拒绝命令**（产生非零 errorCode）
2. FakePLC 的反馈必须 **基于真实物理状态计算**，而非镜像命令
3. FakePLC 必须在每个 tick 中 **持续监测**（扫描周期行为）

---

## 四、重构方案

### 4.1 新增：GantryPhysicalState（物理状态）

在 `FakePLC` 的 `private` 区域新增一个内部结构体，用于表达龙门两物理轴的真正物理状态：

```cpp
/// @brief 龙门物理状态快照（每个 tick 自动刷新）
struct GantryPhysicalState {
    // 使能状态（从 X1/X2 AxisStateInternal 聚合）
    bool x1Enabled = false;
    bool x2Enabled = false;

    // 运动状态（从 X1/X2 AxisStateInternal 聚合）
    bool x1Stationary = true;
    bool x2Stationary = true;

    // 位置差
    double positionDelta = 0.0; // |X1.absPos - X2.absPos|

    // 报警状态（轴 Error 或限位触发）
    bool x1HasAlarm = false;
    bool x2HasAlarm = false;

    // 急停
    bool emergencyStopActive = false;
};
```

### 4.2 新增：龙门联动约束常量

```cpp
/// @brief 龙门联动允许的最大位置差（mm）
static constexpr double GANTRY_MAX_POSITION_DELTA = 0.1;

/// @brief 龙门联动超差报警阈值（联动建立后持续监测）
static constexpr double GANTRY_COUPLED_POSITION_DELTA_ALARM = 0.5;
```

### 4.3 新增：内部方法 `refreshGantryPhysicalState()`

每个 tick 调用，从 X1/X2 的 AxisStateInternal 聚合龙门物理状态：

```cpp
void refreshGantryPhysicalState() {
    const auto& x1 = m_axes.at(AxisId::X1).feedback;
    const auto& x2 = m_axes.at(AxisId::X2).feedback;

    m_gantryPhysical.x1Enabled = (x1.state != AxisState::Disabled
                               && x1.state != AxisState::Unknown);
    m_gantryPhysical.x2Enabled = (x2.state != AxisState::Disabled
                               && x2.state != AxisState::Unknown);

    m_gantryPhysical.x1Stationary = (x1.state == AxisState::Idle);
    m_gantryPhysical.x2Stationary = (x2.state == AxisState::Idle);

    m_gantryPhysical.x1HasAlarm = (x1.state == AxisState::Error)
                               || x1.posLimit || x1.negLimit;
    m_gantryPhysical.x2HasAlarm = (x2.state == AxisState::Error)
                               || x2.posLimit || x2.negLimit;

    m_gantryPhysical.positionDelta = std::abs(x1.absPos - x2.absPos);

    m_gantryPhysical.emergencyStopActive = m_emergencyStoppedReg;
}
```

### 4.4 改造：`tickGantry()` — 扫描周期模型

从"延迟－完成"两态模型，升级为"每周期评估物理状态→刷新反馈寄存器"：

```cpp
void tickGantry(int ms) {
    // 步骤 1：刷新龙门物理状态快照
    refreshGantryPhysicalState();

    // 步骤 2：电机使能状态机
    tickGantryPower(ms);

    // 步骤 3：耦合/解耦状态机
    tickGantryCoupling(ms);

    // 步骤 4：联动建立后持续监测
    tickGantryCoupledMonitoring(ms);

    // 步骤 5：同步刷新 GantryFeedback.enable
    //         enable 始终反映轴真实状态：X1 和 X2 都使能 → enable=true
    m_gantryFeedback.enable = m_gantryPhysical.x1Enabled
                           && m_gantryPhysical.x2Enabled;
}
```

### 4.5 新增：`tickGantryPower()` — 电机使能状态机

```cpp
void tickGantryPower(int ms) {
    if (!m_gantryPowerCmdPending) return;

    // 急停激活时，拒绝使能命令
    if (m_gantryPhysical.emergencyStopActive && m_gantryPowerTarget) {
        m_gantryPowerCmdPending = false;
        m_gantryPowerTimer = 0;
        return;
    }

    m_gantryPowerTimer += ms;
    if (m_gantryPowerTimer >= GANTRY_POWER_DELAY_MS) {
        m_gantryPowerCmdPending = false;
        m_gantryPowerTimer = 0;

        // 将使能/掉电命令同步到 X1/X2
        processCommand(AxisId::X1, EnableCommand{m_gantryPowerTarget});
        processCommand(AxisId::X2, EnableCommand{m_gantryPowerTarget});
    }
}
```

### 4.6 新增：`tickGantryCoupling()` — 联动条件检查 + 拒绝逻辑

**这是本次重构的核心**——FakePLC 第一次拥有了"拒绝命令"的能力。

```cpp
void tickGantryCoupling(int ms) {
    if (!m_gantryCouplingCmdPending) return;

    m_gantryCouplingTimer += ms;

    if (m_gantryCouplingTarget) {
        // ========== 联动请求 ==========
        if (m_gantryCouplingTimer < GANTRY_COUPLING_DELAY_MS) return;

        int errorCode = checkCouplingConditions();
        if (errorCode != 0) {
            // 条件不满足 → 拒绝联动，写入错误码
            m_gantryFeedback.errorCode = errorCode;
            m_gantryFeedback.isCoupled = false;
            m_gantryCouplingCmdPending = false;
            m_gantryCouplingTimer = 0;
            return;
        }

        // 所有条件满足 → 联动成功
        m_gantryFeedback.isCoupled = true;
        m_gantryFeedback.errorCode = 0;
        m_gantryCouplingCmdPending = false;
        m_gantryCouplingTimer = 0;

    } else {
        // ========== 解耦请求（无条件通过） ==========
        if (m_gantryCouplingTimer >= GANTRY_COUPLING_DELAY_MS) {
            m_gantryFeedback.isCoupled = false;
            m_gantryFeedback.errorCode = 0;
            m_gantryCouplingCmdPending = false;
            m_gantryCouplingTimer = 0;
        }
    }
}
```

**联动前置条件检查**（6 项，按优先级顺序）：

```cpp
/// @return 0=通过, 1~5=对应 GantryRejection 错误码
int checkCouplingConditions() const {
    // 1. X1 使能检查
    if (!m_gantryPhysical.x1Enabled) return 2; // X1NotEnabled

    // 2. X2 使能检查
    if (!m_gantryPhysical.x2Enabled) return 3; // X2NotEnabled

    // 3. X1 静止检查
    if (!m_gantryPhysical.x1Stationary) return 4; // X1NotStationary

    // 4. X2 静止检查
    if (!m_gantryPhysical.x2Stationary) return 5; // X2NotStationary

    // 5. 位置差检查
    if (m_gantryPhysical.positionDelta >= GANTRY_MAX_POSITION_DELTA)
        return 1; // PositionToleranceExceeded

    // 6. 报警检查
    if (m_gantryPhysical.x1HasAlarm || m_gantryPhysical.x2HasAlarm)
        return 999; // UnknownError

    // 7. 急停检查
    if (m_gantryPhysical.emergencyStopActive) return 999;

    return 0;
}
```

### 4.7 新增：`tickGantryCoupledMonitoring()` — 联动持续监测

联动建立后每个 tick 检查，任一条件触发 → 自动解除联动 + 写入错误码：

```cpp
void tickGantryCoupledMonitoring(int /*ms*/) {
    if (!m_gantryFeedback.isCoupled) return;

    bool shouldDecouple = false;
    int errorCode = 0;

    // 1. 超差检查（联动建立后阈值放宽至 0.5mm）
    if (m_gantryPhysical.positionDelta >= GANTRY_COUPLED_POSITION_DELTA_ALARM) {
        shouldDecouple = true;
        errorCode = 1; // PositionToleranceExceeded
    }

    // 2. 轴报警检查
    if (!shouldDecouple && (m_gantryPhysical.x1HasAlarm || m_gantryPhysical.x2HasAlarm)) {
        shouldDecouple = true;
        errorCode = 999;
    }

    // 3. 掉电检查
    if (!shouldDecouple && (!m_gantryPhysical.x1Enabled || !m_gantryPhysical.x2Enabled)) {
        shouldDecouple = true;
        errorCode = m_gantryPhysical.x1Enabled ? 3 : 2;
    }

    // 4. 急停检查
    if (!shouldDecouple && m_gantryPhysical.emergencyStopActive) {
        shouldDecouple = true;
        errorCode = 999;
    }

    if (shouldDecouple) {
        m_gantryFeedback.isCoupled = false;
        m_gantryFeedback.errorCode = errorCode;
    }
}
```

### 4.8 增强：联动/分动运动互斥

在 `processCommand()` 中新增龙门约束（在 `JogCommand` 和 `MoveCommand` 分支中）：

```cpp
void processCommand(AxisId id, const JogCommand& cmd) {
    auto& axis = m_axes.at(id);
    if (m_emergencyStoppedReg) return;

    // 新增：联动 ON 时，不允许 X1/X2 独立点动
    if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
        return;
    }

    // ... 原有 Jog 逻辑 ...
}

void processCommand(AxisId id, const MoveCommand& cmd) {
    auto& axis = m_axes.at(id);
    if (m_emergencyStoppedReg) return;

    // 新增：联动 ON 时，不允许 X1/X2 独立定位
    if (m_gantryFeedback.isCoupled && (id == AxisId::X1 || id == AxisId::X2)) {
        return;
    }

    // ... 原有 Move 逻辑 ...
}
```

### 4.9 改造：`tick()` 主循环顺序调整

```cpp
void tick(int ms) {
    // 1. 急停延迟状态机
    tickEmergencyStop(ms);

    // 2. 各轴独立推演（先推演轴，让位置更新）
    for (auto& [id, axis] : m_axes) {
        if (axis.stop_requested) {
            axis.feedback.state = AxisState::Idle;
            axis.stop_requested = false;
        }
        updateStateTransitions(axis, ms);
        updateKinematics(axis, ms);
        checkHardwareLimits(axis);
        axis.feedback.relPos = axis.feedback.absPos - axis.feedback.relZeroAbsPos;
    }

    // 3. 龙门状态机（依赖最新的X1/X2物理状态 → 必须在轴推演之后执行）
    tickGantry(ms);
}
```

> **关键变化**：`tickGantry()` 从原来在轴推演之前，移到了轴推演之后。因为龙门状态机需要读取 X1/X2 的最新位置和状态来做条件判断。

### 4.10 测试注入接口的保护

`forceGantryFeedback()` / `forceGantryCouplingError()` 直接操作 `m_gantryFeedback`，可能与 `tickGantry()` 的自动刷新冲突。引入 `m_gantryFeedbackLocked` 标志：

```cpp
// 在 tickGantry() 开头增加保护：
void tickGantry(int ms) {
    if (m_gantryFeedbackLocked) return; // 测试注入模式，跳过自动刷新

    refreshGantryPhysicalState();
    // ...
}

void forceGantryFeedback(const GantryFeedback& fb) {
    m_gantryFeedback = fb;
    m_gantryPowerCmdPending = false;
    m_gantryCouplingCmdPending = false;
    m_gantryFeedbackLocked = true;
}

void forceGantryCouplingError(int errorCode) {
    m_gantryFeedback.errorCode = errorCode;
    m_gantryFeedbackLocked = true;
}

// 在 onGantryCommand 中自动解除锁定
void onGantryCommand(const GantryCouplingCommand& cmd) {
    m_gantryCouplingCmdPending = true;
    m_gantryCouplingTarget = cmd.enableCoupling;
    m_gantryCouplingTimer = 0;
    m_gantryFeedbackLocked = false; // 新命令到来，让 PLC 重新接管
}
```

### 4.11 `resetAll()` 更新

```cpp
void resetAll() {
    for (auto& [id, axis] : m_axes) {
        axis = AxisStateInternal{};
    }
    m_emergencyStopCmdPending = false;
    m_emergencyStopTimer = 0;
    m_emergencyStoppedReg = false;
    m_gantryFeedback = GantryFeedback{false, false, 0};
    m_gantryPowerCmdPending = false;
    m_gantryPowerTimer = 0;
    m_gantryCouplingCmdPending = false;
    m_gantryCouplingTimer = 0;
    m_gantryPhysical = GantryPhysicalState{};    // 新增
    m_gantryFeedbackLocked = false;              // 新增
}
```

---

## 五、整体数据流变化

### 重构前

```text
GantryOrchestrator
  → GantryCouplingController.requestCouple(true)
    → popPendingCommand() → GantryCouplingCommand{true}
      → Driver → FakePLC.onGantryCommand(cmd)
        → m_gantryCouplingTarget = true
          → tick(ms) → tickGantry(ms)
            → 100ms 延迟
              → m_gantryFeedback.isCoupled = true  ← 无条件！
                → pollFeedback() → GantryCouplingController.applyFeedback()
                  → m_state.applyCoupledFeedback()
```

### 重构后

```text
GantryOrchestrator
  → GantryCouplingController.requestCouple(true)
    → popPendingCommand() → GantryCouplingCommand{true}
      → Driver → FakePLC.onGantryCommand(cmd)
        → m_gantryCouplingTarget = true
          → tick(ms) → tickGantry(ms)
            → refreshGantryPhysicalState()        ← 新：读取X1/X2真实状态
            → tickGantryCoupling(ms)              ← 新：条件检查
              → checkCouplingConditions()         ← 新：6项条件逐一检查
                ├─ X1 未使能? → errorCode=2 (X1NotEnabled)
                ├─ X2 未使能? → errorCode=3 (X2NotEnabled)
                ├─ X1 运动中? → errorCode=4 (X1NotStationary)
                ├─ X2 运动中? → errorCode=5 (X2NotStationary)
                ├─ 位置差≥0.1? → errorCode=1 (PositionToleranceExceeded)
                └─ 全部通过 → isCoupled=true, errorCode=0
            → tickGantryCoupledMonitoring()       ← 新：联动后持续监测
              ├─ 超差? → isCoupled=false + errorCode
              ├─ 轴报警? → isCoupled=false + errorCode
              └─ 轴掉电? → isCoupled=false + errorCode
              → pollFeedback() → GantryCouplingController.applyFeedback()
                ├─ errorCode≠0 → m_state.applyDecoupledFeedback()  ← 首次被触发！
                └─ errorCode=0, isCoupled=true → m_state.applyCoupledFeedback()
```

---

## 六、对上层的影响分析

### 6.1 Domain 层：零改动

`GantryCouplingController`、`GantryCouplingState`、`GantryPowerController` 的实现**完全不需要修改**。它们已经设计为通过 `applyFeedback()` 消费 PLC 反馈（包括 errorCode）。

重构前唯一的问题是 FakePLC 从不产生非零 errorCode，导致以下关键分支从未被触发：

```cpp
// GantryCouplingController::applyFeedback() 中
if (m_state.isCouplingRequested()) {
    if (feedback.errorCode != 0) {
        // PLC 已明确拒绝联动 → 回退到解耦状态  ← 现在可以触发了！
        m_state.applyDecoupledFeedback();
    }
}
```

### 6.2 测试层：新增测试场景

现有 `tests/infrastructure/test_fake_plc.cpp` 仅覆盖单轴/多轴运动隔离，没有龙门耦合测试。重构后以下场景变为可测试：

| 场景 | 测试方法概要 |
|------|-------------|
| X1 未使能时请求联动 → 被拒绝 | `onGantryCommand(GantryCouplingCommand{true})` + `tick()` → `errorCode == 2` |
| X1 运动中请求联动 → 被拒绝 | 先 Jog X1，再请求联动 → `errorCode == 4` |
| 位置差过大请求联动 → 被拒绝 | 设置 X1.pos=10, X2.pos=0，请求联动 → `errorCode == 1` |
| 正常联动建立成功 | 先使能+静止+位置对齐，请求联动 → `isCoupled==true, errorCode==0` |
| 联动后超差自动解耦 | 联动建立后，手动修改 X1 位置造成超差 → 下个 tick 自动解耦 |
| 联动后 X1 报警自动解耦 | 联动建立后，force X1 Error → 下个 tick 自动解耦 |
| 联动模式下拒绝 X1 独立点动 | 联动建立后，Jog X1 → X1 不运动 |
| 解耦模式下允许 X1 独立点动 | 解耦后，Jog X1 → X1 正常运动 |

---

## 七、实施步骤

### 阶段 1：最小侵入改造（核心逻辑）

1. 在 `FakePLC` private 区新增 `GantryPhysicalState` 结构体 + `m_gantryPhysical` 成员
2. 新增 `refreshGantryPhysicalState()` 方法
3. 改造 `tickGantry()` → 拆分为 `tickGantryPower()` + `tickGantryCoupling()` + `tickGantryCoupledMonitoring()`
4. 新增 `checkCouplingConditions()` 方法
5. 在 `tick()` 中将 `tickGantry(ms)` 移到轴推演之后
6. 更新 `resetAll()`

### 阶段 2：增强保护逻辑

7. 在 `processCommand` 中新增联动/分动互斥检查（Jog + Move）
8. 新增 `m_gantryFeedbackLocked` 标志 + `tickGantry()` 锁定保护
9. 常量定义：`GANTRY_MAX_POSITION_DELTA`、`GANTRY_COUPLED_POSITION_DELTA_ALARM`

### 阶段 3：补充测试

10. 创建 `tests/infrastructure/test_fake_plc_gantry.cpp`，覆盖联动条件检查、拒绝、自动解耦、互斥等全部场景
11. 确保现有 `test_fake_plc.cpp` 全部通过（回归测试）

---

## 八、风险与边界

| 风险 | 缓解措施 |
|------|---------|
| `tickGantry()` 时序变更影响 | 急停仍在龙门之前执行（与重构前一致），仅龙门移到轴推演之后，不影响急停行为 |
| `refreshGantryPhysicalState()` 性能 | 仅读取 2 个 AxisStateInternal，O(1) 操作，无影响 |
| `forceGantryFeedback()` 与自动刷新冲突 | 通过 `m_gantryFeedbackLocked` 标志隔离，测试注入模式跳过自动刷新 |
| 联动后超差自动解耦是否需要同时停止运动 | 仅设置 `isCoupled=false` + `errorCode`，运动停止由上层 Domain/Application 基于 errorCode 决定——与真实 PLC 一致（PLC 只写寄存器，不做运动控制决策） |
| 现有测试回归 | `forceGantryFeedback()` 注入时设置 `locked=true`，行为与重构前完全一致 |

---

## 九、总结

重构后的 FakePLC 将具备以下能力：

- [x] **联动条件检查**：6 项前置条件逐一验证，任一不满足则拒绝并写入 errorCode
- [x] **基于轴状态的反馈生成**：`GantryFeedback.enable` 从 X1/X2 真实使能状态聚合
- [x] **联动持续监测**：每个 tick 检查超差/报警/掉电/急停，自动解耦
- [x] **超差监测**：实时计算 `|X1.absPos - X2.absPos|`，两个阈值（建立前 0.1mm + 建立后 0.5mm）
- [x] **命令拒绝**：非零 errorCode 使 `GantryCouplingController.applyFeedback()` 的错误分支可触发
- [x] **PLC 扫描周期行为**：每个 tick 无条件刷新物理状态→刷新反馈寄存器
- [x] **轴状态聚合**：`GantryPhysicalState` 从 X1/X2 的 `AxisState` + 限位状态聚合
- [x] **联动/分动互斥**：联动态下拒绝 X1/X2 的 Jog 和 Move 命令
- [x] **联动中间态**：从命令到反馈之间，真实反映了"延迟 + 条件检查"

Domain 层（`GantryCouplingController`、`GantryCouplingState`、`GantryPowerController`）**零改动**，仅通过 FakePLC 行为升级即可使已有的状态机框架完整运作。
