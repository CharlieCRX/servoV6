# MoveCommand 耦合问题分析与整改方案

> 状态：设计评估文档（修订版 v3）  
> 日期：2026-05-29  
> 项目：servoV6  
> 触发：v2 修订——明确区分"设置目标"与"触发移动"两条独立路径；Policy 仅编排触发移动流程（使能→触发→等待结束→关闭使能），不含目标写入

---

## 目录

1. [问题定义](#1-问题定义)
2. [当前架构分析](#2-当前架构分析)
3. [核心设计：四寄存器 + Policy 策略层](#3-核心设计四寄存器--policy-策略层)
4. [Domain 层变更](#4-domain-层变更)
5. [Infrastructure 层变更](#5-infrastructure-层变更)
6. [Application 层变更](#6-application-层变更)
7. [Presentation 层变更](#7-presentation-层变更)
8. [影响范围与改动清单](#8-影响范围与改动清单)
9. [实施步骤](#9-实施步骤)
10. [风险控制](#10-风险控制)
11. [附录：备选方案矩阵](#11-附录备选方案矩阵)

---

## 1. 问题定义

### 1.1 问题陈述

当前 `MoveCommand` 将 PLC 的**四个独立寄存器操作**耦合到一个 domain 命令中：

```cpp
struct MoveCommand {
    MoveType type;   // Absolute 或 Relative —— 通过 type 字段二选一
    double target;   // 目标位置或距离
    double startAbs; // domain 层快照，不写入 PLC
};
```

在真实 PLC 中，绝对定位和相对定位使用**四组完全独立的寄存器**：

| 操作 | 寄存器 | 含义 |
|------|--------|------|
| 设置绝对移动位置 | `D20/D24/D28/D32` | **ABS_TARGET** — 仅写目标位置，不触发运动 |
| 触发绝对位置移动 | `M40/M42/M44/M46` | **ABS_MOVE_TRIGGER** — 边沿脉冲触发已设置的目标 |
| 设置相对移动距离 | `D22/D26/D30/D34` | **REL_TARGET** — 仅写移动距离，不触发运动 |
| 触发相对位置移动 | `M41/M43/M45/M47` | **REL_MOVE_TRIGGER** — 边沿脉冲触发已设置的距离 |

> 真实场景允许：写目标寄存器后不做触发，什么都不发生。这要求 Domain 层提供等价的两步操作。

### 1.2 PLC 层的隐含约束

在 PLC 层，触发绝对/相对位置移动（写 M 寄存器）有一个**前置条件**：轴必须处于使能状态（Enabled → Idle）。未使能的轴写 M 触发寄存器是无效操作。

这意味着软件侧需要保证**使能 → 触发**的执行顺序。当前编排器（`AutoAbsMoveOrchestrator` / `AutoRelMoveOrchestrator`）已经在做这件事，但它使用的是合并后的 `MoveCommand`（写目标 + 触发一步完成）。

### 1.3 设计目标

1. **Domain 层**：创建 4 个独立命令，与 PLC 的 4 个寄存器一一对应
2. **彻底分离两条路径**：
   - **设置目标路径**：`setAbsTarget()` / `setRelTarget()` 为独立操作，ViewModel 直调 Domain 校验 → `consumePendingCommands()` 统一消费发送，不涉及任何 Policy 编排；不创建专用 UseCase（与 `setJogVelocity` 等一致）
   - **触发移动路径**：`triggerAbsMove()` / `triggerRelMove()` 需要 Policy 编排（使能 → 触发 → 等待运动结束 → 关闭使能），与设置目标彻底解耦
3. **Infrastructure 层**：每个命令直接映射到对应的寄存器，无需条件判断 MoveType
4. **摒弃旧逻辑**：合并式的 `MoveCommand`（写目标 + 触发）标记为废弃，新功能不再使用

---

## 2. 当前架构分析

### 2.1 现状调用链

```
┌──────────────────────────────────────────────────────────────────┐
│ UI (QML)                                                         │
│   用户操作: 输入目标 → 点击"移动"                                  │
│                        ↓                                         │
│  AxisViewModelCore::moveAbsolute(target)                         │
└──────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────┐
│ Application Layer                                                 │
│   MoveAbsoluteUseCase::execute(manager, group, axisId, target)   │
│     → axis->moveAbsolute(target)          // domain 校验          │
│     → drv->send(AxisCommandWithId{...})    // 下发                │
└──────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────┐
│ Domain Layer                                                      │
│   Axis::moveAbsolute(target)                                      │
│     → 状态校验 → 限位校验                                         │
│     → m_pending_intent = MoveCommand{Absolute, target, ...}       │
│       ↑ 一个命令同时携带"写目标 D"和"触发 M"的语义               │
└──────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────┐
│ Infrastructure Layer                                              │
│   Driver: sendMoveCommand(id, cmd)                                │
│     → writeFloat(ABS_TARGET, cmd.target)    // 步骤①：写 D       │
│     → sendEdgeTrigger(ABS_MOVE_TRIGGER)     // 步骤②：触发 M    │
│       ↑ 两个操作在一个函数中完成，无法独立执行                   │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 当前编排器流程

```
AutoAbsMoveOrchestrator / AutoRelMoveOrchestrator::tick()

  EnsuringEnabled      → EnableUseCase(使能)
       ↓
  IssuingMove          → MoveAbsoluteUseCase / MoveRelativeUseCase (写目标 + 触发 合并在 MoveCommand)
       ↓
  WaitingMotionStart   → 轮询 axis->state() 直到 Moving
       ↓
  WaitingMotionFinish  → 轮询 axis->isMoveCompleted()
       ↓
  Done                 → EnableUseCase(关闭使能)
```

**已存在的问题**：
- `IssuingMove` 步骤中，写目标（D 寄存器）和触发（M 寄存器）是一个不可分割的操作
- 无法实现"先设置目标，稍后再触发"的交互模式
- Infrastructure 层在 `sendMoveCommand` 内部通过 `cmd.type` 判断用哪组寄存器

---

## 3. 核心设计：四寄存器 + 两条独立路径 + Policy 仅编排触发

### 3.1 设计思路

**核心原则：设置目标和触发移动是两条完全独立的操作路径。**

| 操作 | 路径 | 是否走 Policy | 说明 |
|------|------|:---:|------|
| 设置绝对移动位置 | `setAbsTarget()` | ❌ 不 | 直接 Dom → consumePendingCommands → Driver → PLC，写 D 寄存器即可 |
| 触发绝对定位 | `triggerAbsMove()` | ✅ 是 | 需 Policy 编排：使能 → 触发 M → 等运动结束 → 关使能 |
| 设置相对移动距离 | `setRelTarget()` | ❌ 不 | 直接 Dom → consumePendingCommands → Driver → PLC，写 D 寄存器即可 |
| 触发相对定位 | `triggerRelMove()` | ✅ 是 | 需 Policy 编排：使能 → 触发 M → 等运动结束 → 关使能 |

**设计逻辑**：
- 设置目标就是单纯的寄存器写入，不需要任何周边逻辑
- 触发移动需要 PLC 前置使能条件，因此需要 Policy 保证"使能→触发→等结束→关使能"的顺序

### 3.2 两条独立的调用链（完全解耦）

#### 路径 A：设置目标（ViewModel 直调 Domain，不走 UseCase 和 Policy）

```
┌──────────────────────────────────────────────────────────────┐
│ UI：用户输入 150.0 → 点击"设置位置"                           │
│                        ↓                                     │
│   AxisViewModelCore::setAbsTarget(150.0)                     │
│     → axis->setAbsTarget(150.0)   // Domain：状态校验+限位校验│
│     → pending command = SetAbsTargetCommand{150.0}           │
│                                                              │
│   下一个 tick() → consumePendingCommands() 自动消费：         │
│     → drv->send(SetAbsTargetCommand{150.0})                  │
│       → writeFloat(ABS_TARGET, 150.0)  // 仅写 D 寄存器     │
│                                                              │
│   ✅ 完成。轴不运动。用户可以：                                │
│     - 什么也不做（仅设置目标值）                               │
│     - 稍后点击"绝对定位"触发运动（路径 B）                    │
│     - 修改目标值再次"设置位置"                                │
└──────────────────────────────────────────────────────────────┘
```

> **设计决策**：`setAbsTarget()` / `setRelTarget()` 不创建专用 UseCase。它们与 `zeroAbsolutePosition` / `setJogVelocity` 等一样，是"单纯写入寄存器"的简单操作——ViewModel 直调 Domain 校验生成 pending command，由 `consumePendingCommands()` 统一消费发送。UseCase 的价值在于**编排**（如 `EnableUseCase` 涉及状态协调），简单的寄存器写入不需要这一层。

#### 路径 B：触发移动（走 Policy 编排）

```
┌──────────────────────────────────────────────────────────────┐
│ UI：用户点击"绝对定位"                                        │
│                        ↓                                     │
│   AbsMovePolicy::start(id)  // ★ 不传 target，已在 PLC 中    │
│                                                                  │
│   AbsMovePolicy::tick() 逐帧驱动:                                │
│     Step::EnsuringEnabled  → EnableUseCase(使能)                │
│     Step::TriggeringMove   → TriggerAbsMoveUseCase (触发 M)    │
│     Step::WaitingMotionStart → 轮询 axis->state() 直到 Moving   │
│     Step::WaitingMotionFinish → 轮询 axis->isMoveCompleted()   │
│     Step::Disabling        → EnableUseCase(关闭使能)            │
│     Step::Done               ← 无论是否到位，运动结束即完成    │
└──────────────────────────────────────────────────────────────────┘
```

**关键区别**：
- `AbsMovePolicy::start(id)` **不接收 `target` 参数**——目标已在之前的 `setAbsTarget` 操作中写入 PLC
- Policy 只管"触发 M 寄存器 + 等待运动结束"，不管"目标值是否正确/是否到位"
- `WaitingMotionFinish` 只判定 `isMoveCompleted()`，不判定位置

### 3.3 四寄存器与 Domain 命令的映射

| PLC 寄存器 | Domain 命令 | 调用方式 | 需要 Policy？ | Infrastructure 操作 |
|-----------|-------------|---------|:---:|-------------------|
| `ABS_TARGET` (D) | `SetAbsTargetCommand` | ViewModel 直调 → `consumePendingCommands` | ❌ | `writeFloat(ABS_TARGET)` |
| `ABS_MOVE_TRIGGER` (M) | `TriggerAbsMoveCommand` | `TriggerAbsMoveUseCase`（Policy 内调用）| ✅ | `sendEdgeTrigger(ABS_MOVE_TRIGGER)` |
| `REL_TARGET` (D) | `SetRelTargetCommand` | ViewModel 直调 → `consumePendingCommands` | ❌ | `writeFloat(REL_TARGET)` |
| `REL_MOVE_TRIGGER` (M) | `TriggerRelMoveCommand` | `TriggerRelMoveUseCase`（Policy 内调用）| ✅ | `sendEdgeTrigger(REL_MOVE_TRIGGER)` |

**关键设计决策**：
- `SetAbsTargetCommand` / `SetRelTargetCommand` 不创建专用 UseCase——它们与 `ZeroAbsoluteCommand` / `SetJogVelocityCommand` 等一样，是"单纯寄存器写入"，由 ViewModel 直调 Domain + `consumePendingCommands()` 消费。这类操作的 UseCase 只是无价值的转发层。
- `TriggerAbsMoveUseCase` / `TriggerRelMoveUseCase` 保留——它们被 Policy 层调用，Policy 本身是一个独立的非 UI 调用者，UseCase 提供了 ViewModel 之外的复用路径。

**关键设计决策**：绝对和相对使用**完全独立的命令类型**，而不是通过 `MoveType` 字段区分。这消除了 Infrastructure 层的条件分支，每个命令直接对应一个寄存器操作。

---

## 4. Domain 层变更

### 4.1 新增四个命令结构体

```cpp
// domain/entity/Axis.h —— 在现有命令结构体之后追加

/// @brief 设置绝对移动目标位置（对应 PLC ABS_TARGET D 寄存器）
/// 仅写目标值，不触发运动
struct SetAbsTargetCommand {
    double target;
};

/// @brief 触发绝对位置移动（对应 PLC ABS_MOVE_TRIGGER M 寄存器）
/// 边沿脉冲触发，PLC 内部需已有目标值
struct TriggerAbsMoveCommand {};

/// @brief 设置相对移动距离（对应 PLC REL_TARGET D 寄存器）
/// 仅写距离值，不触发运动
struct SetRelTargetCommand {
    double distance;
};

/// @brief 触发相对位置移动（对应 PLC REL_MOVE_TRIGGER M 寄存器）
/// 边沿脉冲触发，PLC 内部需已有距离值
struct TriggerRelMoveCommand {};
```

### 4.2 AxisCommand variant 变更

```cpp
// domain/entity/Axis.h

using AxisCommand = std::variant<
    std::monostate,
    JogCommand,
    MoveCommand,              // ← 保留不动（向后兼容）
    StopCommand,
    ZeroAbsoluteCommand,
    SetRelativeZeroCommand,
    ClearRelativeZeroCommand,
    EnableCommand,
    SetJogVelocityCommand,
    SetMoveVelocityCommand,
    SetAbsTargetCommand,      // ★ 新增
    TriggerAbsMoveCommand,    // ★ 新增
    SetRelTargetCommand,      // ★ 新增
    TriggerRelMoveCommand     // ★ 新增
>;
```

> `MoveCommand` 保留不动，保障现有 `MoveAbsoluteUseCase` / `MoveRelativeUseCase` 及旧编排器继续工作。

### 4.3 Axis 实体新增方法

```cpp
// domain/entity/Axis.h —— class Axis 内新增

class Axis {
public:
    // ... 现有方法保持不变 ...

    /// @brief 设置绝对移动目标（仅写 ABS_TARGET，不触发运动）
    /// @return true 表示校验通过、命令已入队
    bool setAbsTarget(double target);

    /// @brief 触发绝对位置移动（需此前已调用 setAbsTarget）
    /// @return true 表示校验通过、命令已入队
    bool triggerAbsMove();

    /// @brief 设置相对移动距离（仅写 REL_TARGET，不触发运动）
    /// @return true 表示校验通过、命令已入队
    bool setRelTarget(double distance);

    /// @brief 触发相对位置移动（需此前已调用 setRelTarget）
    /// @return true 表示校验通过、命令已入队
    bool triggerRelMove();
};
```

### 4.4 Domain 层新增 feedback 字段（替代缓存方案）

> **设计纠正**：原方案使用 `m_cached_abs_target` / `m_cached_rel_distance` 缓存 `setAbsTarget()` / `setRelTarget()` 的目标值，供 `triggerAbsMove()` / `triggerRelMove()` 做二次限位校验。
>
> **问题**：PLC 可能绕过软件层直接设置 target 寄存器（例如外部 HMI 或 PLC 内部逻辑直接写 `REL_TARGET = 50`），此时软件层从未调用 `setRelTarget()`，缓存为空，`triggerRelMove()` 会因为 `m_has_cached_rel_target == false` 而错误拒绝——但 PLC 内部确实已有合法的 target 值，理应允许触发。
>
> **正确方案**：不在 Domain 层缓存 target 值，而是将 `ABS_TARGET` / `REL_TARGET` 寄存器纳入 PLC feedback 轮询。`triggerAbsMove()` / `triggerRelMove()` 基于 **feedback 中的实时 target 值 + 当前 absolute position** 做限位预判。这保证无论 target 值来自软件层还是外部 PLC 操作，触发时都能正确校验。

#### 4.4.1 AxisFeedback 结构体新增字段

```cpp
// domain/entity/Axis.h —— AxisFeedback 结构体追加

struct AxisFeedback {
    // ... 现有字段保持不变 ...

    // ★ 新增：从 PLC 回读的 target 寄存器值（用于 trigger 时做限位预判）
    double absMoveTarget;   // 对应 ABS_TARGET  D 寄存器
    double relMoveTarget;   // 对应 REL_TARGET  D 寄存器
};
```

#### 4.4.2 Axis 类新增存储字段

```cpp
// domain/entity/Axis.h —— class Axis private 成员追加

private:
    // ★ 新增：从 feedback 中镜像 PLC 的 target 寄存器值
    //         供 triggerAbsMove / triggerRelMove 做实时限位预判
    //         注意：不是"缓存 setAbsTarget 的调用参数"，
    //         而是"从 PLC feedback 中回读的实际寄存器值"
    double m_feedback_abs_target = 0.0;
    double m_feedback_rel_target = 0.0;
```

#### 4.4.3 applyFeedback() 新增 target 镜像逻辑

```cpp
// domain/entity/Axis.cpp —— applyFeedback() 中追加

void Axis::applyFeedback(const AxisFeedback& feedback) {
    // ... 现有代码 ...

    // ★ 新增：镜像 PLC 内部 target 寄存器值
    m_feedback_abs_target = feedback.absMoveTarget;
    m_feedback_rel_target = feedback.relMoveTarget;
}
```

> **关键语义区别**：
> | | 旧缓存方案（❌ 错误） | 新 feedback 方案（✅ 正确） |
> |---|---|---|
> | 数据来源 | `setAbsTarget()` 的调用参数 | PLC feedback 轮询的实际寄存器值 |
> | 覆盖场景 | 仅软件层主动调用时才有值 | 软件层 / 外部 HMI / PLC 逻辑写入都能感知 |
> | `trigger` 时校验 | 依赖"是否曾经调用过 set" | 基于 PLC 真实状态 + 当前位置实时计算 |
> | 如果 PLC 外部设 target 后触发 | 拒绝（无缓存） | ✅ 正常通过校验 |

### 4.5 Domain 方法实现要点

#### setAbsTarget —— 仅校验 + 生成 pending command（不缓存 target）

```cpp
// domain/entity/Axis.cpp

bool Axis::setAbsTarget(double target) {
    // 1. 状态准入：Idle 才允许设置目标
    if (m_state != AxisState::Idle) {
        if (m_state == AxisState::Jogging ||
            m_state == AxisState::MovingAbsolute ||
            m_state == AxisState::MovingRelative) {
            m_last_rejection = RejectionReason::AlreadyMoving;
        } else {
            m_last_rejection = RejectionReason::InvalidState;
        }
        return false;
    }

    // 2. 限位检查（与 moveAbsolute 一致）
    if (target > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (target < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    // ★ 不再缓存 target —— target 值会通过 feedback 从 PLC 回读
    m_pending_intent = SetAbsTargetCommand{target};
    m_last_rejection = RejectionReason::None;
    return true;
}
```

#### triggerAbsMove —— 基于 PLC feedback 实时 target 做限位预判

```cpp
// domain/entity/Axis.cpp

bool Axis::triggerAbsMove() {
    // 1. 状态准入：Idle 才允许触发
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    // 2. 硬限位拦截
    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }
    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // 3. ★ 基于 PLC feedback 回读的 target 值做限位预判
    //    无论 target 来源是软件 setAbsTarget() 还是外部 PLC 操作，
    //    只要 feedback 中有合法的 target 值，就能正确校验
    double target = m_feedback_abs_target;
    if (target > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (target < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    m_pending_intent = TriggerAbsMoveCommand{};
    m_last_rejection = RejectionReason::None;
    return true;
}
```

#### setRelTarget / triggerRelMove —— 对称实现

```cpp
// domain/entity/Axis.cpp

bool Axis::setRelTarget(double distance) {
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }
    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    double expectedTarget = m_current_abs_pos + distance;
    if (expectedTarget > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (expectedTarget < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    // ★ 不再缓存 distance
    m_pending_intent = SetRelTargetCommand{distance};
    m_last_rejection = RejectionReason::None;
    return true;
}

bool Axis::triggerRelMove() {
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    if (m_pos_limit_active) {
        m_last_rejection = RejectionReason::AtPositiveLimit;
        return false;
    }
    if (m_neg_limit_active) {
        m_last_rejection = RejectionReason::AtNegativeLimit;
        return false;
    }

    // ★ 基于 PLC feedback 回读的 rel_target + 当前 absolute position
    //    做预期终点限位预判
    double expectedTarget = m_current_abs_pos + m_feedback_rel_target;
    if (expectedTarget > m_pos_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
        return false;
    }
    if (expectedTarget < m_neg_limit_value) {
        m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
        return false;
    }

    m_pending_intent = TriggerRelMoveCommand{};
    m_last_rejection = RejectionReason::None;
    return true;
}
```

> **限位校验公式总结**：
> - `triggerAbsMove()`：`m_feedback_abs_target` vs `[m_neg_limit_value, m_pos_limit_value]`
> - `triggerRelMove()`：`m_current_abs_pos + m_feedback_rel_target` vs `[m_neg_limit_value, m_pos_limit_value]`

---

## 5. Infrastructure 层变更

### 5.1 寄存器映射（零条件判断）

Infrastructure 层收到每个命令后，直接操作**唯一对应的寄存器**，无需 `if (type == Absolute) ... else ...`：

```cpp
// ModbusSystemDriver / FakePLC 的 sendAxisCommand / processCommand 内部

// ★ 新增分支 1：仅写 ABS_TARGET D 寄存器
if constexpr (std::is_same_v<T, SetAbsTargetCommand>) {
    return m_device.writeFloat(regAbsTarget(id), static_cast<float>(c.target));
}

// ★ 新增分支 2：仅触发 ABS_MOVE_TRIGGER M 寄存器
if constexpr (std::is_same_v<T, TriggerAbsMoveCommand>) {
    return sendEdgeTrigger(regAbsTrigger(id));
}

// ★ 新增分支 3：仅写 REL_TARGET D 寄存器
if constexpr (std::is_same_v<T, SetRelTargetCommand>) {
    return m_device.writeFloat(regRelTarget(id), static_cast<float>(c.distance));
}

// ★ 新增分支 4：仅触发 REL_MOVE_TRIGGER M 寄存器
if constexpr (std::is_same_v<T, TriggerRelMoveCommand>) {
    return sendEdgeTrigger(regRelTrigger(id));
}
```

### 5.2 FakePLC 变更

```cpp
// FakePLC —— 四个独立处理函数

void processCommand(AxisId id, const SetAbsTargetCommand& cmd) {
    auto& axis = m_axes.at(id);
    axis.target_pos = cmd.target;
    // axis.feedback.state 保持 Idle —— 不触发运动
}

void processCommand(AxisId id, const TriggerAbsMoveCommand& /*cmd*/) {
    auto& axis = m_axes.at(id);
    if (axis.feedback.state == AxisState::Idle && !m_emergencyStoppedReg) {
        axis.feedback.state = AxisState::MovingAbsolute;
    }
}

void processCommand(AxisId id, const SetRelTargetCommand& cmd) {
    auto& axis = m_axes.at(id);
    axis.target_pos = axis.feedback.absPos + cmd.distance;
    // axis.feedback.state 保持 Idle
}

void processCommand(AxisId id, const TriggerRelMoveCommand& /*cmd*/) {
    auto& axis = m_axes.at(id);
    if (axis.feedback.state == AxisState::Idle && !m_emergencyStoppedReg) {
        axis.feedback.state = AxisState::MovingRelative;
    }
}
```

---

## 6. Application 层变更

### 6.1 UseCase 新增（仅触发类）

> `SetAbsTarget` / `SetRelTarget` **不创建专用 UseCase**。理由：它们与 `zeroAbsolutePosition` / `setJogVelocity` 等一致，是"单纯寄存器写入"——ViewModel 直调 Domain 校验生成 pending command，由 `consumePendingCommands()` 统一消费。为其创建 UseCase 仅增加无价值的转发层。

仅新建 **2 个** UseCase（header-only），供 Policy 层调用：

#### TriggerAbsMoveUseCase

```cpp
// application/axis/TriggerAbsMoveUseCase.h

#pragma once
#include "application/UseCaseError.h"
#include "domain/entity/SystemContext.h"
#include "application/SystemManager.h"

class TriggerAbsMoveUseCase {
public:
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId) {
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) return mgrReason;

        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) return ctxReason;

        if (!axis->triggerAbsMove()) return axis->lastRejection();

        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                return drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
            }
        }
        return std::monostate{};
    }
};
```

#### TriggerRelMoveUseCase

结构同 `TriggerAbsMoveUseCase`，调用 `axis->triggerRelMove()`。

### 6.2 Policy 策略层：AbsMovePolicy

> **核心变更**：Policy 只负责"触发"流程（使能 → 触发 → 等待运动结束 → 关闭使能）。
> **不包含**目标写入——目标已在 `setAbsTarget()` 的独立路径中写入 PLC。

#### 6.2.1 EnsuringEnabled 步骤的设计关键：防重复发送 + 超时机制

> **问题场景**：`tick()` 第一次进入 `EnsuringEnabled` 时已调用 `EnableUseCase` 发送使能命令。但反馈链路存在延迟（Modbus TCP 轮询周期 + PLC 响应时间），下一个 tick 到来时 `axis->state()` 可能仍为 `Disabled`。如果此时再次发送使能命令，会导致：
> - PLC 收到重复的使能边沿脉冲，可能触发 PLC 内部状态机异常
> - 浪费总线带宽
> - 日志中充满重复的"发送使能"记录，掩盖真实问题
>
> **解决方案**：在 `EnsuringEnabled` 步骤中引入两个关键机制：
>
> ##### 1. 防重复发送标志（`m_enableSent`）
>
> ```
> tick() 进入 EnsuringEnabled
>   ├── axis->state() == Disabled ?
>   │     ├── m_enableSent == false  →  发送使能 + 设置 m_enableSent = true + 记录时间戳
>   │     └── m_enableSent == true   →  跳过发送，检查超时（见机制 2）
>   └── axis->state() == Idle ?
>         └── 清除 m_enableSent → 进入 TriggeringMove
> ```
>
> **关键语义**：
> - `m_enableSent` 仅在**本流程**内有效——`startAbs()` 入口处复位为 `false`
> - 一旦发送过使能命令，后续所有 tick 都**不再重复发送**，直到：
>   - `axis->state()` 变为 `Idle`（使能成功，清除标志，进入下一步）
>   - 或超时（清除标志 + 转入 Error）
> - 即使 feedback 延迟导致多帧内 state 仍为 Disabled，也不会误判为"未发送"而重复下发
>
> ##### 2. 超时保护机制
>
> ```
> tick() 进入 EnsuringEnabled（m_enableSent == true 但 state 仍为 Disabled）
>   ├── elapsed = now() - m_enableSentTime
>   ├── elapsed > m_enableTimeoutSeconds (默认 2.0s) ?
>   │     └── YES → LOG_ERROR + m_step = Step::Error + m_lastError = ErrorCode::Timeout
>   └── elapsed <= m_enableTimeoutSeconds ?
>         └── 静默等待（LOG_TRACE 级别记录等待中状态，不干扰日志）
> ```
>
> **超时参数**：
> | 参数 | 默认值 | 说明 |
> |------|--------|------|
> | `m_enableTimeoutSeconds` | `2.0` | 发送使能后等待 Idle feedback 的最大时长 |
> | `m_enableSentTime` | `std::chrono::steady_clock::time_point` | 使用系统单调时钟，不受系统时间调整影响 |
>
> **设计考量**：
> - 2 秒超时足以覆盖正常场景（Modbus TCP 典型轮询周期 50~200ms，使能响应通常在 2~5 个周期内完成）
> - 使用 `std::chrono::steady_clock` 而非 position delta 作为时间基准——`EnsuringEnabled` 阶段轴尚未运动，position 不会变化，无法用 position 差值做超时判定
> - 超时后转入 `Error` 状态，由上层（ViewModel）通过 `hasError()` / `lastError()` 获取错误信息并呈现给用户
>
> ##### 3. UseCaseError 扩展：Timeout 错误码
>
> `EnsuringEnabled` 中的超时是一个**Policy 策略层**错误——`EnableUseCase` 本身执行成功（命令已送达 PLC），但反馈在预期时间内未到达。因此需要在 `UseCaseError` variant 中新增 `Timeout` 类型的支持：
>
> ```cpp
> // application/UseCaseError.h —— 追加 ErrTimeout 结构体
>
> /// @brief Policy 策略层超时错误
> struct ErrTimeout {
>     std::string step;    // 超时发生的步骤名称（如 "EnsuringEnabled"）
>     double timeoutSec;   // 超时阈值（秒）
> };
>
> using UseCaseError = std::variant<
>     std::monostate,
>     ContextRejection,
>     RejectionReason,
>     CommunicationResult,
>     GantryRejection,
>     SafetyRejection,
>     ErrTimeout            // ★ 新增：策略层超时
> >;
> ```
>
> 在 `AbsMovePolicy` 超时分支中使用：
> ```cpp
> m_lastError = ErrTimeout{"EnsuringEnabled", m_enableTimeoutSeconds};
> ```
>
> > **为什么不复用 CommunicationResult？**  
> > `CommunicationResult` 描述的是"命令已生成但未送达 PLC"的通讯失败。而 `EnsuringEnabled` 超时的场景是"命令已成功送达，但 PLC 反馈未返回"——属于**协议级超时**而非**传输级失败**，语义上应该独立分类，方便 ViewModel 层做差异化错误翻译和 UI 提示。
>
> ##### 4. 完整状态机图
>
> ```
> EnsuringEnabled 状态的内部转换（每 tick 执行）：
>
>                     ┌──────────────────────────────────┐
>                     │        axis->state() == Idle     │
>                     │   → m_enableSent = false         │
>                     │   → step = TriggeringMove        │
>                     └──────────────────────────────────┘
>                                  ▲
>                                  │ (直接转换，无需等待)
>                                  │
>   ┌─────────────────┐    ┌─────────────────┐
>   │ m_enableSent     │    │  m_enableSent   │
>   │ == false         │    │  == true        │
>   │ (首次进入)        │    │  (等待反馈中)    │
>   │                 │    │                 │
>   │ → 发送 Enable    │    │ → 检查 elapsed  │
>   │ → 记录时间戳      │    │                 │
>   │ → m_enableSent   │    │ elapsed > 2.0s? │
>   │   = true         │    │   YES → Error   │
>   │                 │    │   NO  → 继续等待  │
>   └─────────────────┘    └─────────────────┘
>          │                       │
>          │    axis->state() 仍为  │
>          └─────── Disabled ──────┘
>          (下一个 tick 进入右侧分支)
> ```
>
> **此机制同样适用于 `RelMovePolicy` 的 `EnsuringEnabled` 步骤**，实现完全对称。

```cpp
// application/policy/AbsMovePolicy.h

#pragma once

#include "application/SystemManager.h"
#include "application/axis/EnableUseCase.h"
#include "application/axis/TriggerAbsMoveUseCase.h"
#include "domain/entity/SystemContext.h"
#include "domain/entity/Axis.h"
#include "application/UseCaseError.h"
#include "infrastructure/logger/Logger.h"
#include "infrastructure/logger/TraceScope.h"
#include <variant>
#include <string>
#include <cmath>

/**
 * @brief 绝对定位触发策略（Policy 层编排）
 *
 * 职责：编排"使能 → 触发(ABS_MOVE_TRIGGER) → 等待运动开始
 *       → 等待运动结束 → 关闭使能"的完整流程。
 *
 * ★ 不包含目标写入 — 目标已在 setAbsTarget() 的独立路径中写入 PLC。
 *
 * 分层职责：
 *   - EnableUseCase         负责：使能/关闭使能
 *   - TriggerAbsMoveUseCase 负责：仅触发 ABS_MOVE_TRIGGER M 寄存器
 *   - AxisViewModelCore::setAbsTarget 负责：独立写 ABS_TARGET D 寄存器（★ 不在本 Policy 内调用，由 ViewModel 直调 Domain + consumePendingCommands 消费）
 *   - Axis 领域层           负责：状态校验 + 限位校验
 *   - AbsMovePolicy         负责：触发流程编排 + 错误转发
 *
 * 使用示例：
 *   // 1. 先独立设置目标（直接调用，不走 Policy）
 *   AxisViewModelCore::setAbsTarget(150.0);
 *
 *   // 2. 然后触发运动（走 Policy 编排）
 *   AbsMovePolicy policy(manager, "Machine_A");
 *   policy.startAbs(AxisId::Y);  // ★ 不传 target
 *   while (policy.currentStep() != Step::Done && policy.currentStep() != Step::Error) {
 *       policy.tick();
 *   }
 */
class AbsMovePolicy {
public:
    enum class Step {
        Initial,
        EnsuringEnabled,     // 使能
        TriggeringMove,      // 触发 ABS_MOVE_TRIGGER（★ 无 SettingTarget）
        WaitingMotionStart,  // 等待运动开始
        WaitingMotionFinish, // 等待运动完成（只看 isMoveCompleted()，不判定位置）
        Disabling,           // 关闭使能
        Done,
        Error
    };

    AbsMovePolicy(SystemManager& manager, const std::string& groupName)
        : m_manager(manager)
        , m_groupName(groupName)
        , m_step(Step::Initial)
        , m_enableTimeoutSeconds(2.0)   // ★ 使能超时：2 秒后未收到 Idle feedback 则判定失败
    {}

    // ========== 入口 ==========

    /// @brief 启动绝对移动触发流程
    /// @param id 目标轴 ID
    /// ★ 不接收 target 参数 —— 目标已在独立 setAbsTarget() 路径中写入 PLC
    void startAbs(AxisId id) {
        m_targetId = id;
        m_step = Step::EnsuringEnabled;
        m_lastError = std::monostate{};

        m_moveTriggered = false;
        m_motionObserved = false;

        // ★ 使能防重复 & 超时相关标志复位
        m_enableSent       = false;
        m_enableSentTime   = std::chrono::steady_clock::time_point{};

        m_traceId = TraceScope::current().traceId;

        LOG_INFO(LogLayer::APP, "AbsPolicy",
                 "[" + m_groupName + "][" + axisName(m_targetId)
                     + "] START triggerAbsMove (target already in PLC)");
    }

    // ========== 逐帧驱动 ==========

    void tick() {
        TraceScope scope(m_groupName, axisName(m_targetId), m_traceId);

        // Layer 0：分组解析
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!m_manager.tryGetGroup(m_groupName, group, mgrReason)) {
            m_step = Step::Error;
            m_lastError = mgrReason;
            return;
        }

        // 急停安全锁检查
        if (group->emergencyStopController().isSystemLocked()) {
            if (m_step != Step::Initial && m_step != Step::Done && m_step != Step::Error) {
                LOG_INFO(LogLayer::APP, "AbsPolicy",
                         "[" + m_groupName + "][" + axisName(m_targetId)
                             + "] Safety locked -- aborting gracefully");
                m_step = Step::Done;
                m_lastError = std::monostate{};
            }
            return;
        }

        // Layer 1：轴获取
        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(m_targetId, axis, ctxReason)) {
            m_step = Step::Error;
            m_lastError = ctxReason;
            return;
        }

        if (axis->state() == AxisState::Error) {
            LOG_ERROR(LogLayer::APP, "AbsPolicy",
                      "[" + m_groupName + "][" + axisName(m_targetId)
                          + "] Axis Error state -- aborting");
            m_step = Step::Error;
            m_lastError = axis->lastRejection();
            return;
        }

        double pos = axis->currentAbsolutePosition();

        switch (m_step) {
        case Step::Initial:
            break;

        // ============================================================
        // Step 1：EnsuringEnabled —— 先使能
        //
        // ★ 防重复发送 + 超时机制：
        //   - 使能命令仅发送一次（m_enableSent 标志位），后续 tick 等待
        //     feedback 带回 Idle 状态，不重复发送 Enable 命令。
        //   - 若 m_enableTimeoutSeconds 内仍未收到 Idle 状态，判定使能
        //     失败，转入 Error 终止流程。
        //   - feedback 延迟导致 axis->state() 仍为 Disabled 时，不会
        //     误判为"未发送"而重复下发使能。
        // ============================================================
        case Step::EnsuringEnabled:
            if (axis->state() == AxisState::Disabled) {
                if (!m_enableSent) {
                    // 首次进入 Disabled：发送使能命令
                    LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- sending Enable (first time)");
                    auto err = EnableUseCase{}.execute(
                        m_manager, m_groupName, m_targetId, true);
                    if (!std::holds_alternative<std::monostate>(err)) {
                        LOG_ERROR(LogLayer::APP, "AbsPolicy",
                                  "[" + m_groupName + "][" + axisName(m_targetId)
                                      + "] EnsuringEnabled -- EnableUseCase FAILED");
                        m_step = Step::Error;
                        m_lastError = err;
                        break;
                    }
                    m_enableSent       = true;
                    m_enableSentTime   = std::chrono::steady_clock::now();
                    LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- Enable sent, waiting for Idle feedback...");
                } else {
                    // 已发送使能，检查超时
                    auto elapsed = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - m_enableSentTime).count();
                    if (elapsed > m_enableTimeoutSeconds) {
                        LOG_ERROR(LogLayer::APP, "AbsPolicy",
                                  "[" + m_groupName + "][" + axisName(m_targetId)
                                      + "] EnsuringEnabled -- TIMEOUT after "
                                      + std::to_string(m_enableTimeoutSeconds)
                                      + "s, still not Idle. Aborting.");
                        m_step = Step::Error;
                        m_lastError = ErrTimeout{"EnsuringEnabled", m_enableTimeoutSeconds};
                        break;
                    }
                    // 未超时：静默等待，不重复发送使能
                    LOG_TRACE(LogLayer::APP, "AbsPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] EnsuringEnabled -- waiting for Idle feedback... (enable already sent, "
                                  + std::to_string(elapsed) + "s elapsed)");
                }
                break;
            }
            if (axis->state() == AxisState::Idle) {
                LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] EnsuringEnabled -> TriggeringMove  ★ 直接触发，不写目标");
                m_enableSent = false;   // ★ 清除标志，完成使能阶段
                m_step = Step::TriggeringMove;
                break;
            }
            break;

        // ============================================================
        // Step 2：TriggeringMove —— 触发 ABS_MOVE_TRIGGER（★ 无 SettingTarget）
        // ============================================================
        case Step::TriggeringMove:
            if (m_moveTriggered) break;
            {
                LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] TriggeringMove -- sending ABS_MOVE_TRIGGER");
                auto err = TriggerAbsMoveUseCase{}.execute(
                    m_manager, m_groupName, m_targetId);
                if (!std::holds_alternative<std::monostate>(err)) {
                    LOG_ERROR(LogLayer::APP, "AbsPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] TriggerAbsMove rejected");
                    EnableUseCase{}.execute(
                        m_manager, m_groupName, m_targetId, false);
                    m_step = Step::Error;
                    m_lastError = err;
                    break;
                }
                m_moveTriggered = true;
                m_startPos = pos;
                LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] TriggeringMove -> WaitingMotionStart");
                m_step = Step::WaitingMotionStart;
            }
            break;

        // ============================================================
        // Step 3：WaitingMotionStart —— 等待运动开始
        // ============================================================
        case Step::WaitingMotionStart:
            if (!m_motionObserved) {
                if (axis->state() == AxisState::MovingAbsolute ||
                    std::abs(pos - m_startPos) > m_epsilon ||
                    axis->isMoveCompleted()) {
                    m_motionObserved = true;
                    LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                              "[" + m_groupName + "][" + axisName(m_targetId)
                                  + "] WaitingMotionStart -> WaitingMotionFinish");
                    m_step = Step::WaitingMotionFinish;
                }
            }
            break;

        // ============================================================
        // Step 4：WaitingMotionFinish —— 等待运动完成
        // ★ 只看 isMoveCompleted()，不判定是否到位
        //   定位精度由 PLC 保证，软件侧只关心运动是否结束
        // ============================================================
        case Step::WaitingMotionFinish:
            if (!m_motionObserved) break;

            if (axis->isMoveCompleted()) {
                LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] WaitingMotionFinish -- move completed -> Disabling");
                m_step = Step::Disabling;
            }
            break;

        // ============================================================
        // Step 5：Disabling —— 关闭使能
        // ============================================================
        case Step::Disabling:
            {
                LOG_DEBUG(LogLayer::APP, "AbsPolicy",
                          "[" + m_groupName + "][" + axisName(m_targetId)
                              + "] Disabling -- sending Disable");
                EnableUseCase{}.execute(
                    m_manager, m_groupName, m_targetId, false);
                LOG_SUMMARY(LogLayer::APP, "AbsPolicy",
                            "[" + m_groupName + "][" + axisName(m_targetId)
                                + "] triggerAbsMove -> SUCCESS");
                m_step = Step::Done;
            }
            break;

        case Step::Done:
        case Step::Error:
        default:
            break;
        }
    }

    // ========== 状态查询 ==========

    Step currentStep() const { return m_step; }
    bool isDone() const { return m_step == Step::Done; }
    bool hasError() const { return m_step == Step::Error; }
    UseCaseError lastError() const { return m_lastError; }

private:
    static std::string axisName(AxisId id) {
        switch (id) {
            case AxisId::Y:  return "Y";
            case AxisId::Z:  return "Z";
            case AxisId::R:  return "R";
            case AxisId::X:  return "X";
            case AxisId::X1: return "X1";
            case AxisId::X2: return "X2";
        }
        return "?";
    }

    SystemManager& m_manager;
    std::string m_groupName;
    Step m_step = Step::Initial;
    AxisId m_targetId = AxisId::Y;
    UseCaseError m_lastError = std::monostate{};

    // ★ 无 m_target（target 在 PLC 中，不由 Policy 管理）
    // ★ 无 m_targetSet

    bool m_moveTriggered = false;
    double m_startPos = 0.0;

    bool m_motionObserved = false;
    const double m_epsilon = 0.01;

    // ========== ★ 使能防重复发送 + 超时 ==========
    bool m_enableSent = false;                                     // 是否已发送使能命令（防止重复下发）
    std::chrono::steady_clock::time_point m_enableSentTime;        // 发送使能时的系统时钟时间戳
    const double m_enableTimeoutSeconds;                           // 使能超时阈值（秒），默认 2.0

    std::string m_traceId = "N/A";
};
```

### 6.3 Policy 策略层：RelMovePolicy

`RelMovePolicy` 与 `AbsMovePolicy` 结构完全对称，差异仅在于：

| 对比项 | AbsMovePolicy | RelMovePolicy |
|--------|---------------|---------------|
| 入口方法 | `startAbs(id)` ★ 不传 target | `startRel(id)` ★ 不传 distance |
| 触发移动 | `TriggerAbsMoveUseCase` | `TriggerRelMoveUseCase` |
| 运动状态判定 | `AxisState::MovingAbsolute` | `AxisState::MovingRelative` |
| 日志标识 | `AbsPolicy` | `RelPolicy` |

> ★ **`RelMovePolicy` 与 `AbsMovePolicy` 一样**：
> - **不含目标写入步骤**（目标已在独立 `setRelTarget()` 路径中写入 PLC）
> - **不含到位位置判定**（只看 `isMoveCompleted()`，运动结束即完成）
> 
> 实现结构与 `AbsMovePolicy` 完全一致，仅需替换上述 4 处差异，此处不再赘述完整代码。

### 6.4 Policy 层与现有编排器的关系

| 组件 | 状态 | 说明 |
|------|------|------|
| `AutoAbsMoveOrchestrator` | ⚠️ 保留不删 | 旧路径兼容；内部使用合并后的 `MoveCommand`，短期保持可用 |
| `AutoRelMoveOrchestrator` | ⚠️ 保留不删 | 同上 |
| **`AbsMovePolicy`** | ★ 新增 | 新设计：使用解耦后的 `SetAbsTarget + TriggerAbsMove` 两步操作 |
| **`RelMovePolicy`** | ★ 新增 | 新设计：使用解耦后的 `SetRelTarget + TriggerRelMove` 两步操作 |

> **迁移路径**：新 UI 功能优先使用 `AbsMovePolicy` / `RelMovePolicy`。旧编排器可在后续版本中逐步退役（内部改用新 Policy 实现），但不在本次重构范围内。

---

## 7. Presentation 层变更

### 7.1 AxisViewModelCore 新增接口

```cpp
// presentation/viewmodel/AxisViewModelCore.h

class AxisViewModelCore {
public:
    // ... 现存接口不变 ...

    // ★ 新增：设置绝对移动目标（仅写 PLC，不触发运动）
    //         实现方式：直调 axis->setAbsTarget() 生成 pending command，
    //         由 consumePendingCommands() 统一消费发送。
    //         与 setJogVelocity() / zeroAbsolutePosition() 模式一致。
    void setAbsTarget(double target);

    // ★ 新增：触发绝对移动（需此前已调用 setAbsTarget）
    //         走 AbsMovePolicy 编排：使能 → 触发 → 等待结束 → 关使能
    void triggerAbsMove();

    // ★ 新增：设置相对移动距离（仅写 PLC，不触发运动）
    //         同 setAbsTarget，走 consumePendingCommands 消费
    void setRelTarget(double distance);

    // ★ 新增：触发相对移动（需此前已调用 setRelTarget）
    //         走 RelMovePolicy 编排
    void triggerRelMove();
};
```

#### setAbsTarget / setRelTarget 实现（与 zeroAbsolutePosition 模式一致）

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::setAbsTarget(double target) {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " setAbsTarget target=" + std::to_string(target));

    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册，无法设置目标位置",
            logPrefix() + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "SetAbsTarget");
        return;
    }

    if (!axis->setAbsTarget(target)) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetAbsTarget");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setAbsTarget rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " setAbsTarget accepted, pending command queued");
    }
}

void AxisViewModelCore::setRelTarget(double distance) {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " setRelTarget distance=" + std::to_string(distance));

    auto* axis = tryGetAxis(m_manager, m_groupName, m_axisId);
    if (!axis) {
        auto error = ViewModelError{
            "CTX_AXIS_NOT_REGISTERED",
            "轴未注册，无法设置移动距离",
            logPrefix() + " not found in system context",
            ErrorCategory::Modal
        };
        pushError(error, "SetRelTarget");
        return;
    }

    if (!axis->setRelTarget(distance)) {
        auto vmError = translate(UseCaseError{axis->lastRejection()});
        pushError(vmError, "SetRelTarget");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " setRelTarget rejected: " + vmError.code);
    } else {
        LOG_DEBUG(LogLayer::UI, "AxisVM",
            logPrefix() + " setRelTarget accepted, pending command queued");
    }
}
```

#### consumePendingCommands 扩展

```cpp
// 在 consumePendingCommands() 中，将 isZeroOrVelocity 改为 isSimpleWrite，
// 追加 SetAbsTargetCommand / SetRelTargetCommand：

bool isSimpleWrite =
    std::holds_alternative<ZeroAbsoluteCommand>(cmd) ||
    std::holds_alternative<SetRelativeZeroCommand>(cmd) ||
    std::holds_alternative<ClearRelativeZeroCommand>(cmd) ||
    std::holds_alternative<SetJogVelocityCommand>(cmd) ||
    std::holds_alternative<SetMoveVelocityCommand>(cmd) ||
    std::holds_alternative<SetAbsTargetCommand>(cmd) ||     // ★
    std::holds_alternative<SetRelTargetCommand>(cmd);       // ★
```

#### triggerAbsMove / triggerRelMove 实现

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::triggerAbsMove() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " triggerAbsMove requested");

    m_absPolicy->startAbs(m_axisId);  // ★ 不传 target

    if (m_absPolicy->hasError()) {
        auto vmError = translate(m_absPolicy->lastError());
        pushError(vmError, "AbsPolicy");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " triggerAbsMove rejected: " + vmError.code);
    }
}

void AxisViewModelCore::triggerRelMove() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " triggerRelMove requested");

    m_relPolicy->startRel(m_axisId);  // ★ 不传 distance

    if (m_relPolicy->hasError()) {
        auto vmError = translate(m_relPolicy->lastError());
        pushError(vmError, "RelPolicy");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " triggerRelMove rejected: " + vmError.code);
    }
}
```

> **架构一致性**：`setAbsTarget()` / `setRelTarget()` 与项目现有的 `setJogVelocity()` / `setMoveVelocity()` / `zeroAbsolutePosition()` 采用完全相同的模式——ViewModel 直调 Domain 校验，pending command 由 `consumePendingCommands()` 统一消费发送。`triggerAbsMove()` / `triggerRelMove()` 则走 Policy 编排，与现有 `moveAbsolute()` / `moveRelative()` 的结构一致（区别在于 Policy 不写目标、不判定到位）。

### 7.2 QML 层交互设计

```
┌──────────────────────────────────────────────────────────────┐
│  Jog 操作区                                                   │
│  [◀] [■] [▶]                                                │
├──────────────────────────────────────────────────────────────┤
│  Move 操作区                                                  │
│                                                              │
│  绝对位置: [____________150.0____________] mm   ← 输入框     │
│            [设置位置]  [绝对定位]                              │
│              ↓              ↓                                 │
│        setAbsTarget()  triggerAbsMove()                       │
│                                                              │
│  相对距离: [____________50.0_____________] mm   ← 输入框     │
│            [设置距离]  [相对定位]                              │
│              ↓              ↓                                 │
│        setRelTarget()  triggerRelMove()                       │
└──────────────────────────────────────────────────────────────┘
```

**交互流程**：
1. 用户在"绝对位置"输入框输入 `150.0`
2. 点击"设置位置"按钮 → `AxisViewModelCore::setAbsTarget(150.0)` → `axis->setAbsTarget()` 生成 pending command → `consumePendingCommands()` 消费 → 仅写 `ABS_TARGET` D 寄存器
3. 用户点击"绝对定位"按钮 → `AxisViewModelCore::triggerAbsMove()` → `AbsMovePolicy::tick()` 编排 → `TriggerAbsMoveUseCase` → 仅触发 `ABS_MOVE_TRIGGER` M 寄存器

> **注意**：UI 上的"设置位置"和"绝对定位"是两个独立的用户操作，对应两步独立的 PLC 写入。如果不点击"触发"，则仅写目标值，轴不会运动。这完全对齐 PLC 硬件的两步操作模型。

### 7.3 QtAxisViewModel 桥接

```cpp
// presentation/viewmodel/QtAxisViewModel.h

class QtAxisViewModel : public QObject {
    Q_OBJECT
public:
    // ... 现有 Q_INVOKABLE 不变 ...

    Q_INVOKABLE void setAbsTarget(double target);
    Q_INVOKABLE void triggerAbsMove();
    Q_INVOKABLE void setRelTarget(double distance);
    Q_INVOKABLE void triggerRelMove();
};
```

---

## 8. 影响范围与改动清单

### 8.1 改动文件总览

| 层 | 文件 | 改动类型 | 改动量 |
|----|------|----------|--------|
| **Domain** | `domain/entity/Axis.h` | 新增 4 个 Command 结构体；variant 追加 4 个变体；AxisFeedback 新增 2 个字段（absMoveTarget/relMoveTarget）；Axis 类新增 4 个方法声明 + 2 个存储字段（m_feedback_abs_target/m_feedback_rel_target） | +50 行 |
| **Domain** | `domain/entity/Axis.cpp` | 实现 `setAbsTarget()` / `triggerAbsMove()` / `setRelTarget()` / `triggerRelMove()`；applyFeedback() 追加 target 镜像逻辑 | +125 行 |
| **Application** | `application/axis/TriggerAbsMoveUseCase.h` | **新文件** | +45 行 |
| **Application** | `application/axis/TriggerRelMoveUseCase.h` | **新文件** | +45 行 |
| **Application** | `application/policy/AbsMovePolicy.h` | **新文件** | +220 行 |
| **Application** | `application/policy/RelMovePolicy.h` | **新文件** | +220 行 |
| **Application** | `application/policy/AutoAbsMoveOrchestrator.h` | **不变** | — |
| **Application** | `application/policy/AutoRelMoveOrchestrator.h` | **不变** | — |
| **Application** | `application/axis/MoveAbsoluteUseCase.h` | **不变** | — |
| **Application** | `application/axis/MoveRelativeUseCase.h` | **不变** | — |
| **Infrastructure** | `infrastructure/FakePLC.h` | 新增 4 个 `processCommand` 重载 | +50 行 |
| **Infrastructure** | `infrastructure/utils/CommandFormatter.h` | 新增 4 个格式化分支 | +20 行 |
| **Infrastructure** | ModbusSystemDriver（如存在） | 新增 4 个 `if constexpr` 分支 | +30 行 |
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.h` | 新增 4 个方法声明 | +10 行 |
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.cpp` | 新增 4 个方法实现 | +40 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.h` | 新增 4 个 Q_INVOKABLE | +10 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.cpp` | 转发调用 | +20 行 |
| **Tests** | `tests/domain/test_axis.cpp` | 新增 4 组测试用例 | +80 行 |
| **Tests** | `tests/infrastructure/test_fake_plc.cpp` | 新增 4 组测试用例 | +60 行 |
| **Tests** | `tests/application/policy/` | **新目录** `test_abs_move_policy.cpp` / `test_rel_move_policy.cpp` | +160 行 |

**总改动量**：约 1,180 行新增（从 1,280 减少 100 行——移除 2 个不必要的 UseCase），无存量代码删除。

### 8.2 不变的部分（零风险）

| 组件 | 原因 |
|------|------|
| `MoveCommand` 结构体 | 结构不变，旧编排器依然使用 |
| `MoveAbsoluteUseCase` / `MoveRelativeUseCase` | 语义和调用链不变 |
| `AutoAbsMoveOrchestrator` / `AutoRelMoveOrchestrator` | 旧编排器一键路径不变 |
| `JogOrchestrator` / `GantryOrchestrator` | 完全无关 |
| `ISystemDriver::send()` | 接口不变，variant 自动扩展 |
| 所有龙门相关代码 | 完全无关 |
| 所有急停相关代码 | 完全无关 |

---

## 9. 实施步骤

### 阶段 1：Domain 层扩展（无外部依赖）

```
□ 1. 在 Axis.h 中定义 SetAbsTargetCommand / TriggerAbsMoveCommand / SetRelTargetCommand / TriggerRelMoveCommand
□ 2. 在 AxisCommand variant 中追加 4 个变体
□ 3. Axis 类新增 setAbsTarget() / triggerAbsMove() / setRelTarget() / triggerRelMove() 声明
□ 4. AxisFeedback 结构体新增 2 个字段（absMoveTarget / relMoveTarget）
□ 5. Axis 类新增 2 个存储字段（m_feedback_abs_target / m_feedback_rel_target）
□ 6. 实现 4 个方法；applyFeedback() 追加 target 镜像逻辑
□ 6. 编写 domain 层单元测试（test_axis.cpp）
```

### 阶段 2：Infrastructure 层适配

```
□ 7. FakePLC 新增 4 个 processCommand 重载
□ 8. CommandFormatter 新增 4 个格式化分支
□ 9. ModbusSystemDriver 新增 4 个 if constexpr 分支（如有）
□ 10. 编写 FakePLC 单元测试（test_fake_plc.cpp）
```

### 阶段 3：Application 层新增 UseCase

```
□ 11. 新建 TriggerAbsMoveUseCase.h
□ 12. 新建 TriggerRelMoveUseCase.h
□ 13. CMakeLists.txt 无需修改（header-only）

注意：SetAbsTargetUseCase / SetRelTargetUseCase 不创建——设置目标的简单寄存器写入
由 ViewModel 直调 Domain + consumePendingCommands() 消费，与 setJogVelocity 等模式一致。
```

### 阶段 4：Policy 层新建策略

```
□ 14. 新建 AbsMovePolicy.h（完整编排逻辑）
□ 15. 新建 RelMovePolicy.h（完整编排逻辑）
□ 16. 编写 Policy 层单元测试（tests/application/policy/test_abs_move_policy.cpp, test_rel_move_policy.cpp）
```

### 阶段 5：Presentation 层桥接

```
□ 17. AxisViewModelCore 新增 setAbsTarget() / triggerAbsMove() / setRelTarget() / triggerRelMove()
□ 18. QtAxisViewModel 新增 Q_INVOKABLE 接口
□ 19. QML 端绑定新增接口，UI 布局加入"设置位置"/"绝对定位"分离按钮
```

### 阶段 6：回归验证

```
□ 20. 运行全量单元测试，确保旧路径无回归
□ 21. FakePLC 集成测试：验证 setAbsTarget → triggerAbsMove 分步流程
□ 22. FakePLC 集成测试：验证 setRelTarget → triggerRelMove 分步流程
□ 23. 验证 MoveAbsoluteUseCase / MoveRelativeUseCase 旧路径行为不变
```

---

## 10. 风险控制

### 10.1 零破坏性变更

- `MoveCommand` 在 variant 中的位置不变，所有存量 `std::get_if<MoveCommand>` 和 `std::holds_alternative<MoveCommand>` 继续工作
- 新增 4 个 variant 变体位于末尾，不影响已有索引
- `AxisCommand` variant 在 Infrastructure 层使用 `std::visit` + `if constexpr` 编译期分派，新增变体会在编译期被检测到（未处理的 variant 分支导致编译错误），不会产生运行时静默错误

### 10.2 编译期安全保障

```cpp
// Infrastructure 层统一使用 std::visit + if constexpr
return std::visit([this, id](auto&& c) -> CommunicationResult {
    using T = std::decay_t<decltype(c)>;

    if constexpr (std::is_same_v<T, EnableCommand>) { ... }
    else if constexpr (std::is_same_v<T, JogCommand>) { ... }
    else if constexpr (std::is_same_v<T, MoveCommand>) { ... }   // 旧路径
    else if constexpr (std::is_same_v<T, StopCommand>) { ... }
    // ★ 新增 4 个分支（每个直接映射唯一寄存器，零条件判断）
    else if constexpr (std::is_same_v<T, SetAbsTargetCommand>) {
        return m_device.writeFloat(regAbsTarget(id), static_cast<float>(c.target));
    }
    else if constexpr (std::is_same_v<T, TriggerAbsMoveCommand>) {
        return sendEdgeTrigger(regAbsTrigger(id));
    }
    else if constexpr (std::is_same_v<T, SetRelTargetCommand>) {
        return m_device.writeFloat(regRelTarget(id), static_cast<float>(c.distance));
    }
    else if constexpr (std::is_same_v<T, TriggerRelMoveCommand>) {
        return sendEdgeTrigger(regRelTrigger(id));
    }
    // ... 其他命令 ...
}, cmd);
```

如果遗漏新增变体的处理，`if constexpr` 链会走到末尾，编译器报错 `no return statement`，暴露遗漏。

### 10.3 回归测试清单

| 测试范围 | 验证点 |
|----------|--------|
| `test_axis.cpp` | `moveAbsolute()` / `moveRelative()` 行为不变 |
| `test_move_absolute_usecase.cpp` | `MoveAbsoluteUseCase` 行为不变 |
| `test_move_relative_usecase.cpp` | `MoveRelativeUseCase` 行为不变 |
| `test_fake_plc.cpp` | `MoveCommand` 处理行为不变 |
| `test_system_integration.cpp` | 编排器完整流程不变 |
| `test_abs_move_policy.cpp` | ★ `AbsMovePolicy` 完整编排流程 |
| `test_rel_move_policy.cpp` | ★ `RelMovePolicy` 完整编排流程 |

---

## 11. 附录：备选方案矩阵

| 方案 | 改动量 | 解耦程度 | 维护性 | 推荐度 |
|------|--------|---------|--------|--------|
| **A: 四命令 + Policy（本次推荐）** | 中 (~1,180行) | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ 首选 |
| **B: 四命令无 Policy，编排在 UI 层** | 中 (~800行) | ⭐⭐⭐ | ⭐⭐ | ❌ 业务逻辑泄漏到 UI |
| **C: 二命令 + MoveType 字段区分** | 小 (~500行) | ⭐⭐⭐ | ⭐⭐⭐ | ❌ Infrastructure 需 if-else |
| **D: 保持 MoveCommand + 在 Driver 判断"只写不触发"** | 小 (~200行) | ⭐ | ⭐ | ❌ 隐式状态 bug 高风险 |

**方案 B 说明**：在 UI 层手动编排"设置目标 → 触发"流程，不新建 Policy。缺点是将业务流程逻辑泄漏到 Presentation 层，且无法复用编排逻辑。

**方案 C 说明**：仅创建 2 个命令（`SetTargetCommand` + `TriggerMoveCommand`），通过 `MoveType type` 字段区分绝对/相对。Infrastructure 层仍需 `if (type == Absolute)` 判断使用哪组寄存器，解耦不彻底。

**方案 D 说明**：不新增命令，UI 先调一次 `MoveCommand`（只写不触发），再调一次（只触发不写）。Driver 内部用标志位判断。高风险，不推荐。

---

## 总结

推荐采用 **四寄存器命令 + Policy 策略层** 方案：

1. **Domain 层**：创建 `SetAbsTargetCommand` / `TriggerAbsMoveCommand` / `SetRelTargetCommand` / `TriggerRelMoveCommand` 四个独立命令，与 PLC 的 `ABS_TARGET` / `ABS_MOVE_TRIGGER` / `REL_TARGET` / `REL_MOVE_TRIGGER` 四个寄存器一一对应
2. **设置目标直接路径**：`setAbsTarget()` / `setRelTarget()` 为独立操作，ViewModel 直调 Domain 校验 → `consumePendingCommands()` 消费发送（与 `setJogVelocity()` / `zeroAbsolutePosition()` 模式一致），不创建专用 UseCase，不经过任何 Policy 编排
3. **Policy 层**：新建 `AbsMovePolicy` / `RelMovePolicy`，仅编排"使能 → 触发（M 寄存器）→ 等待运动结束 → 关闭使能"的触发流程，**不包含目标写入**（目标已在独立的 `setTarget` 路径中写入 PLC）
4. **Infrastructure 层**：每个命令直接映射到唯一寄存器操作，**零条件判断**（不需要 `if type == Absolute`）

**此方案的核心优势**：
- Domain 命令与 PLC 寄存器一对一映射，语义完全透明
- 设置目标与触发移动彻底分离为两条独立路径——设置目标无需 Policy，触发移动才走编排
- Policy 只关心"使能→触发→等待结束→关使能"，不含目标写入，不含到位判定
- Infrastructure 层每个命令直接操作唯一寄存器，消除 `MoveType` 条件分支
- UI 可自由实现"先设置目标，稍后再触发"的两步交互，对齐 PLC 原生操作模型
- 旧 `MoveCommand` / `MoveAbsoluteUseCase` / `AutoAbsMoveOrchestrator` 完整保留，零破坏
- 编译期类型系统保障不会遗漏分支处理

**总改动量约 1,180 行新增代码，无存量代码删除，风险可控。**
