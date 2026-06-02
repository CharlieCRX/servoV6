# JogOrch 错误残留导致按钮锁定问题

## 日期
2026-06-02

## 现象
当点动触发限位错误后，轴进入 Error 状态，JogOrch 处理 ErrorDisabling → Error 流程。轴恢复到 Disabled 状态后，**定位模式的按钮（绝对定位/相对定位 GO 按钮）展示为不可用状态**。

日志片段：
```
[17:40:45.669][WARN][HAL][ModbusSystemDriver] pollFeedback: Y soft limit alarm detected | absPos=-99.9979 | negLimit=true
[17:40:45.669][DEBUG][DOM][Axis][Machine_A][Y] applyFeedback: state Jogging(3) -> Error(6)
[17:40:45.670][ERROR][APP][JogOrch][Machine_A][Y] Axis Error state detected -- entering ErrorDisabling
[17:40:45.691][INFO][APP][JogOrch][Machine_A][Y] ErrorDisabling -- axis is now Disabled, transitioning to Error
[17:40:46.580][SUMMARY][UI][Telemetry] === Machine_A === Y: pos=-100.0 Disabled errs=1
```

可见 Telemetry 显示 `errs=1`（错误残留），且误数为 1 不消失。

## 根因

### 1. JogOrch 错误未被 auto-clear 逻辑覆盖

`AxisViewModelCore::tick()` 中的 auto-clear 逻辑（cpp 第 561-573 行）：

```cpp
// Error 自动恢复：当轴已从 Error 状态恢复...
{
    auto s = state();
    if (s != AxisState::Error && s != AxisState::Unknown) {
        if (!m_errorHistory.empty()) {
            auto& last = m_errorHistory.back();
            if (last.source == "AbsPolicy" || last.source == "RelPolicy") {  // ← 只覆盖 AbsPolicy/RelPolicy
                ...
                m_errorHistory.clear();
            }
        }
    }
}
```

- **JogOrch 来源** (`"JogOrch"`) 不在清除范围
- 当 JogOrch 从 `ErrorDisabling` → `Error` 后，`collectOrchError` 将错误推入 `m_errorHistory`
- 轴状态恢复后（Disabled/Idle），此错误永久残留

### 2. JogOrch tick 的驱动条件排除了 Error 终态

```cpp
if (m_jogOrch->currentStep() != JogOrchestrator::Step::Idle
    && m_jogOrch->currentStep() != JogOrchestrator::Step::Done
    && m_jogOrch->currentStep() != JogOrchestrator::Step::Error) {
    m_jogOrch->tick();
    collectOrchError(*m_jogOrch, "JogOrch");
}
```

- JogOrch 进入 Step::Error 后不再被 tick
- 但错误已经在此之前被收集，且不会再被清除

### 3. 错误类别判断

`JogOrch` 的 `m_lastError` 在检测到 Axis Error 时被设置为 `axis->lastRejection()`。软限位触发时可能为 `RejectionReason::AtNegativeLimit` → ErrorTranslator 翻译为 `ErrorCategory::Inline`。但无论 Inline 还是 Modal，错误残留本身就是问题——UI 状态不干净 / 错误计数不归零。

## 影响
- **点动模式**：JOG+ / JOG- 按钮仅检查 `jogEnabled`（= `!systemLocked && viewModel != null`），不受影响
- **定位模式**：`isReadyForTrigger` / `isReadyForSetTarget` 检查 `hasBlockingError`，若错误类别为 Modal 则按钮永久禁用；即使为 Inline，错误计数 `errs=1` 也不消失
- 用户感知：定位模式按钮不可用，且 Telemetry 中 Y 轴持续显示 `errs=1`

## 修复方案

在 `AxisViewModelCore::tick()` 的 auto-clear 逻辑中扩展覆盖范围：

1. **将 `"JogOrch"` 来源纳入清除范围**
2. **更稳健的做法：轴状态恢复后清除所有来源的错误**（因为无论哪个编排器产生的错误，轴已恢复意味着操作流程已终止）

### 修改文件
- `presentation/viewmodel/AxisViewModelCore.cpp`：修改 auto-clear 逻辑

### 修改前
```cpp
if (last.source == "AbsPolicy" || last.source == "RelPolicy") {
```

### 修改后
```cpp
if (last.source == "AbsPolicy" || last.source == "RelPolicy" || last.source == "JogOrch") {
