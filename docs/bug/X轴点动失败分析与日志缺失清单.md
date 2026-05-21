# X 轴点动操作失败分析与日志缺失清单

## 1. 日志回顾

```
[08:49:40.689] === Machine_A === 
  Y: Disabled  Z: Disabled  R: Disabled  
  X: pos=+0.0 Unknown errs=31  X1: Disabled  X2: Disabled

[08:49:41.377] Machine_A/X jog Forward pressed
[08:49:41.377] [JogOrch][Machine_A][X] START Jog Forward(+)

[08:49:41.688] === Machine_A ===
  X: pos=+0.0 Unknown errs=31   ← 点动下发后，状态未变，errs 未增长

[08:49:41.945] Machine_A/X jog Forward released

[08:49:42.685] === Machine_A ===
  X: pos=+0.0 Unknown errs=131  ← errs 暴增至 131
```

## 2. 失败原因分析

### 根本原因：GantryCouplingController 处于 NotSynchronized 状态，导致 X 轴被拦截

根据代码分析，X 轴属于**龙门轴组**，在 `SystemContext::tryGetAxisInternal()` 中有专门的拦截逻辑：

```cpp
// domain/entity/SystemContext.h 第 105-110 行
if (id == AxisId::X || id == AxisId::X1 || id == AxisId::X2) {
    if (m_gantryCouplingController->isNotSynchronized()) {
        reason = ContextRejection::GantryNotSynchronized;  // ← 拦截点
        outAxis = nullptr;
        return false;
    }
    // ... 龙门联动/解耦语义检查
}
```

**`GantryCouplingController` 初始化为 `NotSynchronized` 状态**，在接收到 PLC 的 `applyFeedback()` 之前，`isNotSynchronized()` 始终返回 `true`。这意味着：

> **在 PLC 反馈注入龙门同步状态之前，所有龙门轴（X、X1、X2）的控制操作全部被拦截。**

### 失败链路时序

```
时间轴:
08:49:40.689 — 遥测：X 轴状态 Unknown, errs=31（初始已有31次tick失败积累）
                └─ GantryCouplingController 仍是 NotSynchronized
                └─ tryGetAxis(X) 始终返回 false, reason=GantryNotSynchronized

08:49:41.377 — UI 按下点动正向
                └─ jogForwardPressed() → JogOrchestrator::startJog(X, Forward)
                └─ JogOrchestrator 从 Idle → EnsuringEnabled

08:49:41.377 — JogOrchestrator::tick() 首次执行
    路径: tick()
        → tryGetGroup("Machine_A") → OK
        → 安全检查(emergencyStopController.isSystemLocked()) → false (通过)
        → tryGetAxis(X) → ★ 失败，reason=GantryNotSynchronized ★
        → m_step = Error, m_lastError = GantryNotSynchronized

08:49:41.688 — 遥测：errs 仍为 31（此时 JogOrch 刚进入 Error，但 ViewModelCore::tick()
               中 collectOrchError 尚未将错误计入 errorHistory？或初始就计数了）
               └─ 注意：此时 JogOrchestrator 已经进入 Error 状态

08:49:41.945 — UI 松开按钮 → jogReleased()
                └─ stopJog(X, Forward) 被调用
                └─ 但由于 m_step = Error，stopJog 中的条件不匹配，静默忽略
                └─ [日志中无任何 stopJog 的响应输出]

08:49:42.685 — 遥测：errs=131（增长了 100 个错误）
                └─ 1 秒内 tick() 约执行了 100 帧（每帧约10ms）
                └─ 每帧 JogOrch.tick() 都重复失败并产生错误
                └─ AxisViewModelCore::collectOrchError() 不断 pushError
```

### 为什么 errs=31 起步？

系统运行初期就已经有 31 次错误积累，说明在用户点动之前，`JogOrchestrator` 或其他编排器已经在反复尝试访问 X 轴并失败。每次 `tryGetAxis(X)` 都返回 `GantryNotSynchronized`，错误被不断追加到 `errorHistory` 中。

### 为什么 errs 从 31 增长到 131？

这是 **100 帧 × 每次 tick 收集 1 个错误** 的结果：
- tick 频率约 100Hz（10ms/帧）
- 08:49:41.688 → 08:49:42.685 约 1 秒
- 100 帧 × 1 错误/帧 = 100 个新错误
- 31 + 100 = 131

### 为什么松开按钮后依然报错

`stopJog()` 调用了但编排器已经是 Error 状态：
```cpp
// JogOrchestrator::stopJog()
if (m_step == Step::EnsuringEnabled ||   // ✗ 不是
    m_step == Step::IssuingJog ||        // ✗ 不是
    m_step == Step::Jogging) {           // ✗ 已经是 Error
    m_step = Step::IssuingStop;           // 永远不会执行
}
```

所以停止指令被**静默忽略**，JogOrchestrator 停留在 Error 状态继续产生错误。

---

## 3. 日志缺失清单

根据上述分析，现有日志**不足以定位问题**。以下是缺失的关键日志项：

### 缺失项 1：龙门同步状态 —— 无法判断 GantryCouplingController 是否已同步

**严重性**：⭐ 关键（根本原因锁定）

**原因**：遥测日志只输出轴状态 `Unknown`，但没有输出龙门控制器的 `Synchronized/NotSynchronized` 状态，导致无法从日志直接判断 GantryCouplingController 是否收到了 PLC 反馈。

**建议新增日志位置**：
- `GantryCouplingController::applyFeedback()` 入口和出口
- 遥测日志中应包含龙门同步状态标志

**示例日志**：
```
[APPLY_FEEDBACK][GantryCoupling] before: NotSynchronized → after: NotSynchronized (no PLC feedback yet)
[TELEMETRY][Machine_A] GantrySync: NotSynchronized
```

### 缺失项 2：tryGetAxis/tryReadAxis 拒绝原因 —— 看不到龙门语义拦截

**严重性**：⭐ 关键（无法确认拦截类型）

**原因**：`SystemContext::tryGetAxisInternal()` 在拒绝访问时，没有输出被拒绝的 AxisId 和拒绝原因。

**建议新增位置**：`SystemContext::tryGetAxisInternal()` 中每个 return false 的分支

**示例日志**：
```
[CONTEXT][Machine_A] tryGetAxis(X) REJECTED: GantryNotSynchronized (couplingState=NotSynchronized)
[CONTEXT][Machine_A] tryGetAxis(X1) REJECTED: PhysicalAxisLockedByGantry (isCoupled=true)
[CONTEXT][Machine_A] tryGetAxis(X) REJECTED: LogicalAxisUnavailableWhenDecoupled (isCoupled=false)
```

### 缺失项 3：JogOrchestrator Error 状态进入日志 —— 看不到编排器失败原因

**严重性**：⭐ 关键（无法确认编排器级别错误）

**原因**：JogOrch 的 `tick()` 中，当 `tryGetAxis` 失败进入 Error 时，没有输出足够详细的错误日志。

**建议新增位置**：`JogOrchestrator::tick()` 中每个设置 `m_step = Error` 的位置

**示例日志**（基于当前代码）：
```cpp
// JogOrchestrator 现有代码 → 增强日志
case Step::EnsuringEnabled:
    if (!group->tryGetAxis(m_targetId, axis, ctxReason)) {
        LOG_ERROR(LogLayer::APP, "JogOrch",
            "[" + m_groupName + "][" + axisName(m_targetId) + "] "
            "tryGetAxis FAILED: " + contextRejectionName(ctxReason)
            + " (couplingSync=" + (coupling controller sync status)
            + ")");
        m_step = Step::Error;
        m_lastError = ctxReason;
        return;
    }
```

### 缺失项 4：collectOrchError 收集到的具体错误内容 —— 看不到错误详情

**严重性**：⭐⭐ 重要（无法追踪到 ViewModel 级别）

**原因**：`AxisViewModelCore::collectOrchError()` 只有 `LOG_TRACE_EVERY_N(100)` 的输出，没有输出具体的错误内容和来源。

**建议新增位置**：`AxisViewModelCore::collectOrchError()` 中 pushError 之前

**示例日志**：
```
[COLLECT_ERR][AxisVM][Machine_A/X] Collected error from JogOrch: 
  type=ContextRejection, code=GantryNotSynchronized, 
  orchStep=EnsuringEnabled
```

### 缺失项 5：PLC 反馈轮询（applyFeedback）的龙门同步日志

**严重性**：⭐⭐ 重要（无法确认 PLC 状态机推进）

**原因**：没有日志输出 `SystemContext::pollFeedback()` 或 `applyFeedback()` 的调用情况和 GantryCouplingController 的同步状态变化。

**建议新增位置**：
- `GantryCouplingController::applyFeedback()` 调用者
- `ISystemDriver::pollFeedback()` 的返回值

### 缺失项 6：轴状态从 Unknown→Disabled→Idle 的转换日志

**严重性**：⭐⭐ 重要（无法验证反馈链路是否正常）

**原因**：X 轴长时间保持 `Unknown` 状态，说明 `Axis::applyFeedback()` 要么从未被调用，要么反馈数据无效。日志没有记录这一现象。

**建议新增位置**：`Axis::applyFeedback()` 中状态变化时

**示例日志**：
```
[AXIS_FB][Machine_A/X] applyFeedback: state=Unknown(0)→Unknown(0) 
  absPos=0.0  (no PLC feedback received yet)
[AXIS_FB][Machine_A/X] applyFeedback: state=Unknown(0)→Disabled(1)
  absPos=0.0  (PLC feedback arrived)
```

### 缺失项 7：stopJog 静默忽略的日志

**严重性**：⭐⭐ 重要（导致用户感觉按钮无响应但无反馈）

**原因**：当编排器处于 Error/Done 时，`stopJog()` 被静默忽略，UI 用户松开了按钮但没有任何日志表明停止操作被忽略了。

**建议新增位置**：`JogOrchestrator::stopJog()` 中所有 return 隐式忽略的分支

**示例日志**：
```
[STOP_JOG][Machine_A/X] stopJog ignored: currentStep=Error 
  (expected EnsuringEnabled/IssuingJog/Jogging)
```

### 缺失项 8：错误推送（pushError）的摘要日志

**严重性**：⭐ 一般辅助（帮助理解 errs 计数量级）

**原因**：`errs=131` 只能看到累加数量，无法判断错误的内容类型比例。

**建议新增位置**：`AxisViewModelCore::pushError()` 中带错误类型统计

**示例日志**：
```
[PUSH_ERR][AxisVM][Machine_A/X] error pushed: 
  source=JogOrch, code=GANTRY_NOT_SYNCHRONIZED, 
  totalErrors=32 (ContextRejection:32, RejectionReason:0, Comm:0)
```

### 缺失项 9：ErrorTranslator 转换过程的调试日志

**严重性**：⭐ 一般辅助

**原因**：`translate(UseCaseError)` 的转换结果没有日志，无法确认 ViewModelError 中的显示信息是否正确。

---

## 4. 结论总结

| 维度 | 结论 |
|------|------|
| **操作是否成功** | ❌ 失败 |
| **根本原因** | `GantryCouplingController` 处于 `NotSynchronized` 状态，导致 `SystemContext::tryGetAxisInternal()` 拒绝所有龙门轴（X）的访问，拒绝原因为 `GantryNotSynchronized` |
| **直接原因** | PLC 的龙门同步反馈（`applyFeedback`）未及时注入，或从未注入 |
| **错误累积** | JogOrchestrator 进入 Error 后无法退出，每帧 tick() 都在产生新错误，1 秒内从 31 增长到 131 |
| **停止响应缺失** | 松开按钮后 stopJog 被静默忽略（编排器已 Error），导致用户感觉完全无响应 |
| **日志覆盖度** | ❌ 严重不足 — 9 项关键日志缺失，无法快速诊断龙门同步问题 |

### 修复建议

1. **短期修复**：确保系统启动后尽快向 `GantryCouplingController` 注入 `applyFeedback()` 以将状态从 `NotSynchronized` 推进到 `Decoupled`（或 `Coupled`）
2. **日志增强**：按缺失清单补充 9 项日志
3. **防错误风暴**：在 `collectOrchError()` 中增加相同错误去重机制，防止单个问题导致错误计数暴增
4. **stopJog 容错**：允许从 Error 状态也能发起停止，以让编排器优雅退出
