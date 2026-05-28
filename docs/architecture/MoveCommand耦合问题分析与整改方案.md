# MoveCommand 耦合问题分析与整改方案

> 状态：设计评估文档  
> 日期：2026-05-28  
> 项目：servoV6  
> 触发：阅读《SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计》时发现的问题

---

## 目录

1. [问题定义](#1-问题定义)
2. [当前架构分析](#2-当前架构分析)
3. [用户方案评估](#3-用户方案评估)
4. [推荐方案：命令语义拆分](#4-推荐方案命令语义拆分)
5. [影响范围与改动清单](#5-影响范围与改动清单)
6. [实施步骤](#6-实施步骤)
7. [风险控制](#7-风险控制)
8. [附录：备选方案矩阵](#8-附录备选方案矩阵)

---

## 1. 问题定义

### 1.1 问题陈述

当前 `MoveCommand` 将 PLC 的两个独立操作耦合到一个 domain 命令中：

```cpp
struct MoveCommand {
    MoveType type;   // Absolute 或 Relative
    double target;   // 目标位置或距离
    double startAbs; // domain 层快照，不写入 PLC
};
```

在真实 PLC 中，绝对定位需要两个独立步骤：

| 步骤 | 操作 | 寄存器 |
|------|------|--------|
| ① 预设目标 | 写入目标位置 | `D20/D24/D28/D32` (ABS_TARGET) 或 `D22/D26/D30/D34` (REL_TARGET) |
| ② 触发执行 | 边沿脉冲 | `M40/M42/M44/M46` (ABS_MOVE_TRIGGER) 或 `M41/M43/M45/M47` (REL_MOVE_TRIGGER) |

> 真实场景允许：步骤①写完后不做步骤②，什么都不发生。

当前设计路径：

```
UI 设置位置 + 触发 → MoveCommand → Infrastructure 一次性执行 ①+②
                                           ↑ 耦合点
```

但 UI 层采用了与 PLC 一致的独立策略——"先设置位置"和"触发运动"是两个独立的用户操作。

### 1.2 重构的利害关系

**重构风险**：
- `MoveCommand` 贯穿 domain → application → infrastructure 三层
- `MoveAbsoluteUseCase` / `MoveRelativeUseCase` 均依赖现有语义
- `AutoAbsMoveOrchestrator` / `AutoRelMoveOrchestrator` 编排器依赖 UseCase
- `AxisViewModelCore` 的 UI 绑定接口依赖 UseCase
- 相关测试文件需同步修改

**不改的风险**：
- UI 无法实现"独立设置目标、独立触发"的交互
- 强行在 infrastructure 层做 hack（区分"只触发不设置"的 MoveCommand）会导致隐式状态 bug
- 与 PLC 硬件模型背离，后续调试困难

### 1.3 设计原则

> **Domain 层的命令语义应当与硬件操作模型对齐，而不是强行将多个硬件操作压缩为一个粗粒度命令。**  
> 当 UI 交互模型就是"预设参数 + 触发"时，domain 层应提供等价的细粒度命令。

---

## 2. 当前架构分析

### 2.1 现状调用链

```
┌──────────────────────────────────────────────────────────────────────┐
│ UI (QML)                                                             │
│   用户操作: 输入目标位置 → 点击"移动"                                  │
│                        ↓                                             │
│  AxisViewModelCore::moveAbsolute(target)                             │
└──────────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────────┐
│ Application Layer                                                     │
│   MoveAbsoluteUseCase::execute(manager, group, axisId, target)       │
│     → axis->moveAbsolute(target)          // domain 校验              │
│     → drv->send(AxisCommandWithId{...})    // 下发到 infrastructure   │
└──────────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────────┐
│ Domain Layer                                                         │
│   Axis::moveAbsolute(target)                                         │
│     → 状态校验 → 限位校验                                            │
│     → m_pending_intent = MoveCommand{Absolute, target, startAbs}     │
│       ↑ 一个命令, 包含"写目标"和"触发"的完整语义                      │
└──────────────────────────────────────────────────────────────────────┘
                         ↓
┌──────────────────────────────────────────────────────────────────────┐
│ Infrastructure Layer                                                  │
│   ModbusSystemDriver::sendMoveCommand(id, cmd)                       │
│     步骤①: m_device.writeFloat(regCmdAbsTarget, cmd.target)  // 写D  │
│     步骤②: sendEdgeTrigger(regCmdAbsTrigger)               // 写M  │
│       ↑ 两个操作在一个函数中完成, 无独立触发路径                      │
│                                                                      │
│   FakePLC::processCommand(id, MoveCommand)                           │
│     axis.target_pos = cmd.target;           // 一步设置目标           │
│     axis.feedback.state = MovingAbsolute;   // 一步触发运动           │
└──────────────────────────────────────────────────────────────────────┘
```

### 2.2 编排器路径（不受影响）

```
AutoAbsMoveOrchestrator::tick()
  步骤 IssuingMove:
    MoveAbsoluteUseCase::execute(...)
      → axis->moveAbsolute(target)    // 领域校验
      → drv->send(MoveCommand)        // 下发设置+触发
```

编排器是"一键自动"模式，一步完成设置+触发是**合理设计**，不需要拆。

### 2.3 问题本质

问题出在 **UI 交互模型 ≠ Domain 命令模型**：

| 维度 | PLC 硬件 | UI 设计 | 当前 Domain |
|------|---------|---------|-------------|
| 操作粒度 | 两步独立 | 两步独立 | **一步耦合** |
| 设置位置 | 写 D 寄存器，不触发 | 写入输入框，不触发 | ❌ 不支持 |
| 触发运动 | 边沿触发 M 寄存器 | 点击按钮触发 | ❌ 无法独立触发 |

---

## 3. 用户方案评估

### 3.1 用户提出的方案

> 在 Axis 中加入新的命令——设置相对/绝对位置移动距离。  
> 之前的接口 MoveCommand 不动。  
> UI 上写入距离后调用设置接口，触发时用 MoveCommand，但 infrastructure 层只做触发，不做设置。

### 3.2 方案评估

| 方面 | 评价 |
|------|------|
| **语义清晰度** | ⚠️ 差 — 同一个 `MoveCommand` 在某些路径要"两步都做"，在另一些路径要"只做第二步"，语义含混 |
| **infrastructure 负担** | ⚠️ 重 — Driver 需要额外状态判断"target 是否已被写过"来决定跳不跳步骤① |
| **时序风险** | ⚠️ 高 — 如果"设置"和"触发"两个 tick 之间 PLC 状态变更（如急停、掉电），会导致触发时使用过期上下文 |
| **维护成本** | ⚠️ 高 — 未来开发者看到 `MoveCommand` 无法直观判断它在特定调用路径上实际执行什么 |
| **改动范围** | ✅ 小 — 只加新命令，不改 MoveCommand |
| **向后兼容** | ✅ 好 — 现存所有编排器和 UseCase 不受影响 |

**结论**：改动量小但引入隐式状态，长期维护成本高。不推荐作为首选方案。

---

## 4. 推荐方案：命令语义拆分

### 4.1 核心思路

将 `MoveCommand` 的两阶段语义显式拆分为两个独立命令，同时保留 `MoveCommand` 作为编排器的便捷命令：

```cpp
// ★ 新增：仅写入目标位置到 PLC D 寄存器，不触发运动
struct SetMoveTargetCommand {
    MoveType type;   // Absolute 或 Relative
    double target;   // 目标位置或距离
};

// ★ 新增：仅对已设置的目标发送边沿触发脉冲
struct TriggerMoveCommand {};

// ★ 保留：编排器一步到位（写目标 + 触发）
// MoveCommand 语义和结构完全不变
struct MoveCommand {
    MoveType type;
    double target;
    double startAbs;
};
```

### 4.2 AxisCommand variant 变更

```cpp
// domain/entity/Axis.h

using AxisCommand = std::variant<
    std::monostate,
    JogCommand,
    MoveCommand,              // ← 保留不动（编排器一键路径）
    SetMoveTargetCommand,     // ★ 新增：仅设置目标
    TriggerMoveCommand,       // ★ 新增：仅触发（需在 SetMoveTarget 之后）
    StopCommand,
    ZeroAbsoluteCommand,
    SetRelativeZeroCommand,
    ClearRelativeZeroCommand,
    EnableCommand,
    SetJogVelocityCommand,
    SetMoveVelocityCommand
>;
```

### 4.3 新增命令语义表

| 命令 | 写入 PLC | 领域校验 | 使用场景 |
|------|----------|---------|---------|
| `SetMoveTargetCommand` | 写 `ABS_TARGET` 或 `REL_TARGET` (D 寄存器) | 状态校验、限位校验 | UI 输入目标位置后写入 PLC |
| `TriggerMoveCommand` | 边沿触发 `ABS_MOVE_TRIGGER` 或 `REL_MOVE_TRIGGER` (M 寄存器) | 状态校验 (Idle)、限位校验、需此前已调用 SetMoveTarget | UI 点击"开始移动"按钮 |
| `MoveCommand` | 写目标 D + 触发 M | 完全校验 | 编排器一键移动 |

### 4.4 完整调用链（新路径）

```
┌──────────────────────────────────────────────────────────────────┐
│ UI 操作 1：用户输入绝对位置 "150" → 失焦/确认                     │
│                        ↓                                         │
│   AxisViewModelCore::setMoveTarget(MoveType::Absolute, 150)     │
│     → SetMoveTargetUseCase::execute(...)                         │
│       → axis->setMoveTarget(Absolute, 150)  // domain 校验       │
│       → drv->send(SetMoveTargetCommand{Absolute, 150})           │
│                                                                  │
│   Infrastructure:                                                │
│       m_device.writeFloat(ABS_TARGET, 150.0f)  // 只写 D，不触发 │
└──────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────┐
│ UI 操作 2：用户点击"绝对定位"按钮                                 │
│                        ↓                                         │
│   AxisViewModelCore::triggerMove()                                │
│     → TriggerMoveUseCase::execute(...)                            │
│       → axis->triggerMove()              // domain 校验          │
│       → drv->send(TriggerMoveCommand{})                           │
│                                                                  │
│   Infrastructure:                                                │
│       sendEdgeTrigger(ABS_MOVE_TRIGGER)    // 只触发 M，不写 D   │
└──────────────────────────────────────────────────────────────────┘
```

### 4.5 Domain 层新增方法

#### Axis 新增方法

```cpp
// domain/entity/Axis.h

class Axis {
public:
    // ... 现有方法保持不变 ...

    /// @brief 设置移动目标位置（仅写 D 寄存器，不触发运动）
    /// @return true 表示校验通过、命令已入队
    bool setMoveTarget(MoveType type, double target);

    /// @brief 触发已设置的移动目标（仅触发 M 寄存器边沿脉冲）
    /// @return true 表示校验通过、命令已入队
    /// @pre 需此前已成功调用 setMoveTarget（目标已写入 PLC）
    bool triggerMove();
};
```

#### Axis::setMoveTarget 实现要点

```cpp
bool Axis::setMoveTarget(MoveType type, double target) {
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

    // 2. 限位检查（与 moveAbsolute/moveRelative 一致）
    if (type == MoveType::Absolute) {
        if (target > m_pos_limit_value) {
            m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
            return false;
        }
        if (target < m_neg_limit_value) {
            m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
            return false;
        }
    } else { // Relative
        double expectedTarget = m_current_abs_pos + target;
        if (expectedTarget > m_pos_limit_value) {
            m_last_rejection = RejectionReason::TargetOutOfPositiveLimit;
            return false;
        }
        if (expectedTarget < m_neg_limit_value) {
            m_last_rejection = RejectionReason::TargetOutOfNegativeLimit;
            return false;
        }
    }

    m_pending_intent = SetMoveTargetCommand{type, target};
    m_last_rejection = RejectionReason::None;

    // ★ 缓存目标类型，供后续 triggerMove 使用
    m_last_set_move_type = type;
    m_last_set_move_target = target;

    return true;
}
```

#### Axis::triggerMove 实现要点

```cpp
bool Axis::triggerMove() {
    // 1. 状态准入：Idle 才允许触发
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }

    // 2. 限位再次确认（避免设置后到触发前状态变化）
    auto type = m_last_set_move_type;
    double target = m_last_set_move_target;

    if (type == MoveType::Absolute) {
        if (target > m_pos_limit_value || target < m_neg_limit_value) {
            m_last_rejection = 
                (target > m_pos_limit_value) ? RejectionReason::TargetOutOfPositiveLimit
                                             : RejectionReason::TargetOutOfNegativeLimit;
            return false;
        }
    } else {
        double expectedTarget = m_current_abs_pos + target;
        if (expectedTarget > m_pos_limit_value || expectedTarget < m_neg_limit_value) {
            m_last_rejection = 
                (expectedTarget > m_pos_limit_value) ? RejectionReason::TargetOutOfPositiveLimit
                                                     : RejectionReason::TargetOutOfNegativeLimit;
            return false;
        }
    }

    // 3. 硬限位拦截
    if (m_pos_limit_active || m_neg_limit_active) {
        m_last_rejection = m_pos_limit_active ? RejectionReason::AtPositiveLimit
                                              : RejectionReason::AtNegativeLimit;
        return false;
    }

    m_pending_intent = TriggerMoveCommand{};
    m_last_rejection = RejectionReason::None;
    return true;
}
```

### 4.6 Infrastructure 层映射

#### ModbusSystemDriver 变更

```cpp
// sendAxisCommand 内部新增分支

else if constexpr (std::is_same_v<T, SetMoveTargetCommand>) {
    // ★ 仅写入目标 D 寄存器，不触发
    if (c.type == MoveType::Absolute) {
        return m_device.writeFloat(regCmdAbsTarget(id), static_cast<float>(c.target));
    } else {
        return m_device.writeFloat(regCmdRelTarget(id), static_cast<float>(c.target));
    }
}
else if constexpr (std::is_same_v<T, TriggerMoveCommand>) {
    // ★ 仅发送边沿触发 M 寄存器
    // 注意：此时依赖 PLC 中已存在的目标值（由之前的 SetMoveTarget 写入）
    if (m_hasSetMoveTarget) {
        // m_hasSetMoveTarget 由上一个 SetMoveTargetCommand 置 true
        return sendEdgeTrigger(regCmdAbsTrigger(id));  // 简化示例
    }
    return CommunicationResult{CommunicationResult::Status::InvalidResponse,
                               0, "TriggerMove without prior SetMoveTarget"};
}
else if constexpr (std::is_same_v<T, MoveCommand>) {
    // ← 不变：编排器路径，同时写 D + 触发 M
    return sendMoveCommand(id, c);
}
```

> **注意**：`TriggerMoveCommand` 本身**不携带 target 信息**。Driver 需要知道触发的是 Absolute 还是 Relative，但这可以通过 Driver 内部状态（由上一个 `SetMoveTargetCommand` 设置）来区分。  
> 或者简化处理：`TriggerMoveCommand` 中可选携带 `MoveType` 字段以消除隐式依赖。

#### 推荐改进：TriggerMoveCommand 携带 MoveType

```cpp
struct TriggerMoveCommand {
    MoveType type;  // ★ 显式指明触发类型，消除对隐式状态的依赖
};
```

这样 infrastructure 层可以直接：
```cpp
if (c.type == MoveType::Absolute) {
    return sendEdgeTrigger(regCmdAbsTrigger(id));
} else {
    return sendEdgeTrigger(regCmdRelTrigger(id));
}
```

#### FakePLC 变更

```cpp
void processCommand(AxisId id, const SetMoveTargetCommand& cmd) {
    auto& axis = m_axes.at(id);
    // ★ 只设置目标，不改变状态、不触发运动
    if (cmd.type == MoveType::Absolute) {
        axis.target_pos = cmd.target;
    } else {
        axis.target_pos = axis.feedback.absPos + cmd.target;
    }
    // axis.feedback.state 保持不变（依然是 Idle）
}

void processCommand(AxisId id, const TriggerMoveCommand& cmd) {
    auto& axis = m_axes.at(id);
    // ★ 基于已设置的目标触发运动
    if (axis.feedback.state == AxisState::Idle && !m_emergencyStoppedReg) {
        axis.feedback.state = (cmd.type == MoveType::Absolute) 
                              ? AxisState::MovingAbsolute 
                              : AxisState::MovingRelative;
    }
}
```

### 4.7 Application 层新增 UseCase

#### SetMoveTargetUseCase

```cpp
// application/axis/SetMoveTargetUseCase.h

class SetMoveTargetUseCase {
public:
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId,
                         MoveType type,
                         double target) {
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return mgrReason;
        }

        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return ctxReason;
        }

        if (!axis->setMoveTarget(type, target)) {
            return axis->lastRejection();
        }

        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                return drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
            }
        }
        return std::monostate{};
    }
};
```

#### TriggerMoveUseCase

```cpp
// application/axis/TriggerMoveUseCase.h

class TriggerMoveUseCase {
public:
    UseCaseError execute(SystemManager& manager,
                         const std::string& groupName,
                         AxisId axisId) {
        SystemContext* group = nullptr;
        ContextRejection mgrReason = ContextRejection::None;
        if (!manager.tryGetGroup(groupName, group, mgrReason)) {
            return mgrReason;
        }

        Axis* axis = nullptr;
        ContextRejection ctxReason = ContextRejection::None;
        if (!group->tryGetAxis(axisId, axis, ctxReason)) {
            return ctxReason;
        }

        if (!axis->triggerMove()) {
            return axis->lastRejection();
        }

        if (axis->hasPendingCommand()) {
            if (auto* drv = group->driver()) {
                return drv->send(AxisCommandWithId{axisId, axis->getPendingCommand()});
            }
        }
        return std::monostate{};
    }
};
```

### 4.8 Presentation 层变更

```cpp
// AxisViewModelCore 新增接口

void setMoveTarget(MoveType type, double target);
void triggerMove();
```

### 4.9 UI 交互流程

```
┌─────────────────────────────────────────────────────────────┐
│ 绝对位置: [__________150___________] mm    ← 用户输入       │
│                                                              │
│ [设置位置] 按钮  → 调用 setMoveTarget(Absolute, 150.0)      │
│                                                              │
│ [绝对定位] 按钮  → 调用 triggerMove()                        │
│                                                              │
│ (相对距离同理)                                               │
└─────────────────────────────────────────────────────────────┘
```

---

## 5. 影响范围与改动清单

### 5.1 改动文件总览

| 层 | 文件 | 改动类型 | 改动量 |
|----|------|----------|--------|
| **Domain** | `domain/entity/Axis.h` | 新增 `SetMoveTargetCommand`、`TriggerMoveCommand` 结构体；variant 中追加两变体 | +30 行 |
| **Domain** | `domain/entity/Axis.h` | Axis 类新增 `setMoveTarget()`、`triggerMove()` 声明；新增缓存字段 | +10 行 |
| **Domain** | `domain/entity/Axis.cpp` | 实现 `setMoveTarget()`、`triggerMove()` | +80 行 |
| **Application** | `application/axis/SetMoveTargetUseCase.h` | **新文件** | +50 行 |
| **Application** | `application/axis/TriggerMoveUseCase.h` | **新文件** | +45 行 |
| **Application** | `application/axis/MoveAbsoluteUseCase.h` | 不变 | — |
| **Application** | `application/axis/MoveRelativeUseCase.h` | 不变 | — |
| **Application** | `application/policy/AutoAbsMoveOrchestrator.h` | 不变 | — |
| **Application** | `application/policy/AutoRelMoveOrchestrator.h` | 不变 | — |
| **Infrastructure** | `infrastructure/ISystemDriver.h` | 不变 | — |
| **Infrastructure** | `infrastructure/FakePLC.h` | 新增两个 `processCommand` 重载 | +40 行 |
| **Infrastructure** | `infrastructure/utils/CommandFormatter.h` | 新增两个格式化分支 | +10 行 |
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.h` | 新增 `setMoveTarget()`、`triggerMove()` 声明 | +5 行 |
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.cpp` | 新增两个方法实现 | +30 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.h` | 新增 Q_INVOKABLE 接口 | +5 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.cpp` | 转发调用 | +10 行 |
| **Tests** | `tests/domain/test_axis.cpp` | 新增测试用例 | +60 行 |
| **Tests** | `tests/application/test_move_absolute_usecase.cpp` | 不变 | — |
| **Tests** | `tests/infrastructure/test_fake_plc.cpp` | 新增测试用例 | +40 行 |
| **Docs** | `docs/architecture/SystemCommand寄存器映射与Domain-Infrastructure对接设计.md` | 更新映射表 | +15 行 |

**总改动量**：约 430 行新增 + 文档更新，无存量代码删除。

### 5.2 不变的部分（零风险）

| 组件 | 原因 |
|------|------|
| `MoveCommand` 结构体 | 结构不变，编排器依然使用 |
| `MoveAbsoluteUseCase` / `MoveRelativeUseCase` | 语义和调用链不变 |
| `AutoAbsMoveOrchestrator` / `AutoRelMoveOrchestrator` | 编排器一键路径不变 |
| `JogOrchestrator` | 完全无关 |
| `ISystemDriver::send()` | 接口不变，variant 自动扩展 |
| 所有龙门相关代码 | 完全无关 |
| 所有急停相关代码 | 完全无关 |

---

## 6. 实施步骤

### 阶段 1：Domain 层扩展（无外部依赖）

```
□ 1. 在 Axis.h 中定义 SetMoveTargetCommand / TriggerMoveCommand 结构体
□ 2. 在 AxisCommand variant 中追加两变体
□ 3. Axis 类新增 setMoveTarget() / triggerMove() 声明 + 缓存字段
□ 4. 实现 setMoveTarget() / triggerMove()
□ 5. 编写 domain 层单元测试（test_axis.cpp）
```

### 阶段 2：Infrastructure 层适配

```
□ 6. FakePLC 新增 processCommand(SetMoveTargetCommand) / processCommand(TriggerMoveCommand)
□ 7. CommandFormatter 新增格式化分支
□ 8. 编写 FakePLC 单元测试（test_fake_plc.cpp）
```

### 阶段 3：Application 层新增 UseCase

```
□ 9. 新建 SetMoveTargetUseCase.h
□ 10. 新建 TriggerMoveUseCase.h
□ 11. CMakeLists.txt 无需修改（header-only）
```

### 阶段 4：Presentation 层桥接

```
□ 12. AxisViewModelCore 新增 setMoveTarget() / triggerMove()
□ 13. QtAxisViewModel 新增 Q_INVOKABLE 接口
□ 14. QML 端绑定新增接口（按 UI 实际需求）
```

### 阶段 5：文档更新

```
□ 15. 更新 SystemCommand 寄存器映射设计文档
□ 16. 更新架构总览文档（如有）
```

---

## 7. 风险控制

### 7.1 零破坏性变更

- `MoveCommand` 在 variant 中的位置不变，所有存量 `std::get_if<MoveCommand>` 和 `std::holds_alternative<MoveCommand>` 继续工作
- 新增 variant 变体位于末尾，不影响已有索引
- `AxisCommand` variant 的 `std::visit` 在 infrastructure 层使用 `if constexpr` 编译期分派，新增变体会在编译期被检测到（编译器会警告未处理的 variant 分支），不会产生运行时静默错误

### 7.2 编译期安全保障

```cpp
// infrastructure 层使用 std::visit + if constexpr
return std::visit([this, id](auto&& c) -> CommunicationResult {
    using T = std::decay_t<decltype(c)>;

    if constexpr (std::is_same_v<T, EnableCommand>) { ... }
    else if constexpr (std::is_same_v<T, JogCommand>) { ... }
    else if constexpr (std::is_same_v<T, MoveCommand>) { ... }
    else if constexpr (std::is_same_v<T, SetMoveTargetCommand>) { ... }  // ← 新增
    else if constexpr (std::is_same_v<T, TriggerMoveCommand>) { ... }     // ← 新增
    // ...
}, cmd);
```

如果遗漏新增变体的处理，`if constexpr` 链会走到末尾，编译器会要求所有分支有统一返回类型，从而暴露遗漏。

### 7.3 回归测试

| 测试范围 | 验证点 |
|----------|--------|
| `test_axis.cpp` | `moveAbsolute()` / `moveRelative()` 行为不变 |
| `test_move_absolute_usecase.cpp` | `MoveAbsoluteUseCase` 行为不变 |
| `test_move_relative_usecase.cpp` | `MoveRelativeUseCase` 行为不变 |
| `test_fake_plc.cpp` | `MoveCommand` 处理行为不变 |
| `test_system_integration.cpp` | 编排器完整流程不变 |

---

## 8. 附录：备选方案矩阵

| 方案 | 改动量 | 语义清晰 | 维护性 | 推荐度 |
|------|--------|---------|--------|--------|
| **A: 命令拆分（推荐）** | 中 (~430行) | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ 首选 |
| **B: 用户方案（只加设置命令）** | 小 (~200行) | ⭐⭐ | ⭐⭐ | ⚠️ 备选 |
| **C: 拆分 MoveCommand 为两个独立命令并废弃旧命令** | 大 (~800行) | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ❌ 过度 |
| **D: 在 UI 层缓存目标，触发时合并下发** | 小 (~100行) | ⭐ | ⭐ | ❌ 违背分层 |

**方案 D 说明**：UI 层缓存目标值，触发时拼装 MoveCommand 下发。缺点是将业务逻辑泄漏到 Presentation 层，且 domain 层无法感知"设置目标"的独立操作。

---

## 总结

推荐采用 **命令语义拆分方案**：
1. 新增 `SetMoveTargetCommand` （仅写 D 寄存器）
2. 新增 `TriggerMoveCommand` （仅触发 M 寄存器）
3. 保留 `MoveCommand` （编排器一步到位）

此方案的**核心优势**：
- Domain 命令语义与 PLC 硬件操作模型对齐
- 每个命令职责单一、无歧义
- Infrastructure 层不需要隐式状态判断
- 编排器不受影响，存量 UseCase 零改动
- 编译期类型系统保障不会遗漏分支处理

**总改动量约 430 行新增代码，无存量代码删除，风险可控。**
