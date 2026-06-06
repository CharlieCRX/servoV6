# 龙门点动停止命令被 applyFeedback 误清除分析

## 问题摘要

龙门 X 轴点动停止时，`GantryJogPolicy::checkMotionCompleted()` 正确调用了 `axis.stopJog()` 产生停止意图，但该停止命令从未被下发到 PLC 寄存器，导致电机一直保持点动前进（Jogging）状态，无法停止。

## 日志证据

```
[10:45:11.920][INFO][APP][GantryJog]  Stop requested by UI
[10:45:11.927][DEBUG][APP][GantryJog] User released, requesting Stop
[10:45:11.927][DEBUG][DOM][Axis]      stopJog(dir=Forward) entry: state=Jogging(3)
[10:45:11.927][DEBUG][DOM][Axis]      stopJog: PASS -> pending=JogCommand(dir=Forward, active=false)
[10:45:11.936][DEBUG][DOM][Axis]      applyFeedback: axis=Jogging(3) -> clearing Jog intent
```

关键：`stopJog` 产生的 `JogCommand(dir=Forward, active=false)` 在 9ms 内就被 `applyFeedback` 清除了。此后轴一直保持 Jogging 状态，X 位置从 78.0 持续增加到 113.0+。

## 根因分析

### 时间线

```
tick N (Monitoring):
  1. hasPendingCommand()? → false（还没有停止命令）
  2. checkMotionCompleted(axis):
     - axis.stopJog(m_dir)  →  m_pending_intent = JogCommand{Forward, false}
     - return false  →  保持在 Monitoring 状态
  ★ 停止意图已产生，但本 tick 不再消费它

--- 反馈循环（两次 tick 之间）---
feedback loop:
  - axis.applyFeedback(...):
      state == Jogging
      holds_alternative<JogCommand>(m_pending_intent) → true
      → m_pending_intent = monostate{}  // ★ 清除！

tick N+1 (Monitoring):
  1. hasPendingCommand()? → false  // ★ 已被清除
  2. checkMotionCompleted(axis):
     - m_stopIssued == true → 跳过 stopJog
     - state != Idle → return false
  ★ 停止命令永远丢失
```

### 涉及代码

#### 1. `GantryMotionOrchestrator::tick()` — Monitoring 步骤（第 305-340 行）

```cpp
case Step::Monitoring: {
    // ... 获取 axis ...
    
    // ★ 先消费已有待执行命令（此时还没有停止命令）
    if (axis->hasPendingCommand() && drv) {
        drv->send(AxisCommandWithId{m_axisId, axis->getPendingCommand()});
    }

    // ★ 再调用钩子（checkMotionCompleted 可能产生新命令）
    if (checkMotionCompleted(*axis)) {
        // ... -> PostMotionDelay
    }
    break;
}
```

**问题**：消费待执行命令发生在调用 `checkMotionCompleted` **之前**。当 `checkMotionCompleted` 产生新的 `JogCommand{active=false}` 时，本 tick 已经没有机会再消费它。

#### 2. `GantryJogPolicy::checkMotionCompleted()` — 第 73-92 行

```cpp
bool checkMotionCompleted(Axis& axis) override {
    if (!m_userPressing && !m_stopIssued) {
        axis.stopJog(m_dir);    // ← 产生 JogCommand{dir, false}
        m_stopIssued = true;
        return false;  // 停止命令已发出，等待轴停稳
    }
    // ...
}
```

注释写道"父类 Monitoring 步骤会自动消费待执行命令并发送到驱动"，但这个假设在当前代码顺序下不成立——Monitoring 已经先消费过了。

#### 3. `Axis::applyFeedback()` — 第 175-185 行

```cpp
if (m_state == AxisState::Jogging ||
    m_state == AxisState::MovingAbsolute ||
    m_state == AxisState::MovingRelative)
{
    if (std::holds_alternative<JogCommand>(m_pending_intent)) {
        m_pending_intent = std::monostate{};  // ★ 不区分 active=true/false
    }
}
```

**问题**：此处的意图是"轴已进入运动状态，清理掉启动命令"，但它不区分 `JogCommand{active=true}`（启动）和 `JogCommand{active=false}`（停止）。当停止命令还在 pending 中等待下一个 tick 消费时，被反馈循环误判为"已完成的启动命令"而清除。

### 根本原因总结

| 层面 | 问题 |
|------|------|
| **时序问题** | `Monitoring` 步骤先消费 pending 再调用 `checkMotionCompleted`，导致 `checkMotionCompleted` 产生的命令要等到下一个 tick 才能消费 |
| **竞态条件** | 反馈循环（`applyFeedback`）在两个 tick 之间运行，会无条件清除 `Jogging` 状态下的 `JogCommand`，不分 `active` 标志 |
| **设计假设不成立** | `checkMotionCompleted` 注释假设"父类 Monitoring 会自动消费"，但实际代码顺序是消费在前、钩子调用在后 |

## 影响范围

- **龙门 X 轴点动**：用户松手后，停止命令丢失，轴持续 Jogging，无法正常停止
- 龙门绝对定位（`GantryAbsMovePolicy`）和相对定位（`GantryRelMovePolicy`）不受影响，因为它们的 `checkMotionCompleted` 不产生新命令，只做状态判断

## 修复方案

### 已实施方案：Monitoring 步骤中消费顺序调整

**选择理由**：不修改钩子签名（`checkMotionCompleted(Axis&)` 保持不变），不修改 Axis 实体，只在基类 `GantryMotionOrchestrator::tick()` 中调整 pending command 消费顺序。对所有子类（GantryJogPolicy、GantryAbsMovePolicy、GantryRelMovePolicy）均无副作用。

### 修改文件

**1. `application/policy/GantryMotionOrchestrator.h` — Monitoring 步骤（约第 328 行）**

**修改前**（消费在前，钩子在后）：
```cpp
// ★ 消费子类钩子可能产生的 Axis 待执行命令
if (axis->hasPendingCommand() && drv) {
    drv->send(AxisCommandWithId{m_axisId, axis->getPendingCommand()});
}

// 调用子类钩子判断运动是否完成
if (checkMotionCompleted(*axis)) {
    // ...
}
```

**修改后**（钩子在前，消费在后）：
```cpp
// 调用子类钩子判断运动是否完成
// 注意：钩子（如 GantryJogPolicy::checkMotionCompleted）可能产生
// 新的待执行命令（如点动停止 JogCommand{active=false}），必须
// 在钩子调用后立即消费，否则在两个 tick 之间 applyFeedback 会
// 在 Jogging 状态下无条件清除 JogCommand，导致停止命令丢失。
if (checkMotionCompleted(*axis)) {
    // ...
}

// ★ 消费子类钩子产生的 Axis 待执行命令（如点动停止命令）
if (axis->hasPendingCommand() && drv) {
    drv->send(AxisCommandWithId{m_axisId, axis->getPendingCommand()});
}
```

**2. `application/policy/GantryJogPolicy.h` — checkMotionCompleted 注释更新**

```cpp
// 修改前: （父类 Monitoring 步骤会自动消费待执行命令并发送到驱动）
// 修改后: 停止命令由父类 Monitoring 步骤在钩子返回后消费发送到驱动
```

### 修复后执行时序

```
tick N (Monitoring):
  1. checkMotionCompleted(axis):
     - axis.stopJog(m_dir)  →  m_pending_intent = JogCommand{Forward, false}
  2. hasPendingCommand()? → true
     → drv->send(JogCommand{Forward, false})  // ★ 立即发送到 PLC
  3. checkMotionCompleted 返回 false → 保持 Monitoring

--- 反馈循环 ---
applyFeedback:
  - Jogging 状态下清除 JogCommand ← 但 pending 已为空，无影响

tick N+1 (Monitoring):
  - PLC 反馈 Jog active=false → 轴减速 → Idle
  - checkMotionCompleted: state == Idle → return true → PostMotionDelay
```

### 备选方案（未采用）

- **方案 B**：修改 `checkMotionCompleted` 签名为 `checkMotionCompleted(Axis&, SystemContext&)` 让子类直接调用 `sendPendingCommand`。未采用原因：需修改所有子类（3 个），改动面大。
- **方案 C**：Axis 层区分 `JogCommand.active` 标志，仅清除 `active=true`。未采用原因：用户指示不修改 Axis 实体，且实体层不应感知上层时序。

## 日期

2026-06-06
