# X 轴使能关闭跳过逻辑 —— 临时方案与长期重构计划

## 问题背景

X 轴与 Y/Z/R 轴不同：X 轴由龙门（Gantry）系统控制，其使能由 `JogOrchestrator` 点动流程持续管理。X 轴在点动期间需要始终保持使能状态（Enable ON），即使中间的 AbsMove 或 RelMove 子流程结束也不能关闭使能。

当前现状是 `AbsMovePolicy`、`RelMovePolicy`、`JogOrchestrator` 三个编排器中均内嵌了对 `AxisId::X` 的硬编码判断，在以下场景跳过 `EnableUseCase` 的关闭使能调用：

1. **正常流程结束时**（Disabling 步骤）
2. **触发命令失败时**（TriggeringMove 失败熔断）
3. **Error 错误处理时**（Error 步骤的保护性掉电）

## 当前临时解决方案（已实现）

### JogOrchestrator.h

```cpp
// IssuingJog 阶段，发送命令失败
if (m_targetId != AxisId::X) {
    EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
}

// EnsuringDisabled 阶段，正常流程结束
if (m_targetId == AxisId::X) {
    m_step = Step::Done;
    return;
}
```

### AbsMovePolicy.h & RelMovePolicy.h

三处相同的 `if (m_targetId != AxisId::X)` 守卫：

| 位置 | 场景 | 行为 |
|------|------|------|
| `TriggeringMove` 失败 | 触发运动命令被拒绝 | 跳过 EnableUseCase(false) |
| `Disabling` | 运动完成，正常关使能 | 跳过 Disabling，直接 → Done |
| `Error` | 任意错误发生 | 跳过保护性掉电 |

## 问题分析（为什么需要重构）

1. **耦合严重**：轴类型判断（`AxisId::X`）散布在三个编排器的多个 switch-case 分支中，违反单一职责原则。
2. **扩展困难**：如果将来 X1/X2 也需要类似行为，需要在每个编排器中追加 `|| m_targetId == AxisId::X1` 判断。
3. **职责不清**：编排器（Orchestrator/Policy）不应关心"某根轴的使能由谁管理"这类所有权问题。
4. **测试困难**：X 轴特殊逻辑与正常流程交织，无法独立单测。

## 长期重构方案

### 方案：引入 `EnableOwnership` 策略 + 独立 X 轴编排器

#### 1. 在 `Axis` 领域实体中增加使能所有权标记

```cpp
// domain/entity/Axis.h
enum class EnableOwnership {
    SelfManaged,     // 轴自己管理使能（Y/Z/R），编排器负责 Enable ON/OFF
    ExternallyManaged // 使能由外部管理（X 轴，由 Gantry/Jog 流程控制）
};
```

#### 2. 各编排器在关使能前查询所有权

在 `EnableUseCase` 内部或各编排器的 Disabling/Error 分支中，通过 Axis 的 `enableOwnership()` 判断是否执行关闭：

```cpp
// 伪代码
if (axis->enableOwnership() == EnableOwnership::SelfManaged) {
    EnableUseCase{}.execute(m_manager, m_groupName, m_targetId, false);
}
```

这样编排器不再需要知道 `AxisId::X`，只需查询轴自身的策略。

#### 3. 终极方案：独立的 X 轴编排器

为 X 轴创建专用的编排器（如 `GantryAxisOrchestrator`），完全接管 X 轴的运动编排，不包含使能 ON/OFF 逻辑。使能由上层龙门流程统一管理。

```
现有:
  AbsMovePolicy  ──(含 X 轴特殊判断)──→  Y/Z/R/X
  
重构后:
  AbsMovePolicy  ──→  Y/Z/R（标准流程）
  GantryAxisOrchestrator ──→  X（无使能管理，由 Gantry 控制）
```

### 推荐实施路径

| 阶段 | 内容 | 优先级 |
|------|------|--------|
| Phase 1 | `Axis` 实体增加 `EnableOwnership` 字段，默认 `SelfManaged` | 高 |
| Phase 2 | 三个编排器中将 `AxisId::X` 判断替换为 `enableOwnership()` 查询 | 高 |
| Phase 3 | 为 X 轴设计独立的 `GantryAxisOrchestrator`，逐步迁移逻辑 | 中 |
| Phase 4 | 清理旧编排器中的 X 轴分支，统一由新编排器接管 | 低 |

## 相关文件

| 文件 | 角色 |
|------|------|
| `application/policy/JogOrchestrator.h` | 点动编排器（已有 X 轴跳过逻辑） |
| `application/policy/AbsMovePolicy.h` | 绝对定位策略（已添加 X 轴跳过逻辑） |
| `application/policy/RelMovePolicy.h` | 相对定位策略（已添加 X 轴跳过逻辑） |
| `domain/entity/Axis.h` | 轴实体（待增加 EnableOwnership） |
| `domain/gantry/` | 龙门相关逻辑（待扩展） |

## 修改记录

| 日期 | 内容 |
|------|------|
| 2026-06-03 | AbsMovePolicy / RelMovePolicy 增加 X 轴使能关闭跳过（3 处） |
| 2026-06-03 | 创建本文档，记录临时方案与重构计划 |