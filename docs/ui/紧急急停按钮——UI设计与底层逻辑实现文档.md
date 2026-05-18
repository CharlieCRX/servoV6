# 紧急急停按钮 — UI设计与底层逻辑实现

> 状态：设计文档 + 实施指南  
> 适用范围：`servoV6` 项目 `presentation/qml/blocks/ActionControlBlock.qml` 急停按钮重构  
> 创建时间：2026-05-18

---

## 一、当前问题诊断

### 1.1 错误映射

当前 `ActionControlBlock.qml` 中急停按钮：

```qml
IndustrialButton {
    text: "急 停"
    isCircle: false
    buttonSize: 140 * Theme.scale
    baseColor: Theme.colorError
    activeColor: "#FF8A80"
    Layout.alignment: Qt.AlignHCenter
    onClicked: if(viewModel) viewModel.stop()  // ❌ 错误：这调用了 Axis Stop，不是紧急急停
}
```

**`viewModel.stop()` 是轴的 STOP 指令**（对应 `StopAxisUseCase`），用于停止单个轴的运动，走的是：

```
UI → QtAxisViewModel::stop() → AxisViewModelCore::stop() → StopAxisUseCase
```

这完全不是急停。急停的**正确链路**应该是：

```
UI → EmergencyStopUseCase.execute(manager, groupName)
   → EmergencyStopController::requestEmergencyStop()
   → ISystemDriver::send(EmergencyStopCommand{true})
   → PLC 接收"设备急停"命令
   → PLC 硬掉电所有轴 + 设置"设备急停中"=TRUE
   → 下一帧 applyFeedback(true) → EmergencyStopped
```

### 1.2 根本原因

| 问题 | 说明 |
|------|------|
| **ViewModel 粒度不匹配** | 当前只有**单轴 ViewModel**，急停是**分组级操作**，两者不匹配 |
| **缺少安全状态 ViewModel** | 没有暴露 `EmergencyStopController` 给 QML 层的桥接器 |
| **GroupName 上下文丢失** | QML 层知道 `currentGroup` 但单轴 ViewModel 不知道它所属的分组 |

---

## 二、解决方案设计

### 2.1 新增：EmergencyStopViewModel

需要在 Presentation 层新增一个**分组级安全 ViewModel**，作为 UI ↔ 安全域之间的桥梁。

```
                           QML Layer
                              │
              ┌───────────────┼───────────────┐
              │               │               │
       AxisSelectorBlock  TelemetryBlock  ActionControlBlock
              │               │               │
              │               │        ┌──────┴──────┐
              │               │        │ 急停按钮 UI  │
              │               │        │ + 状态条    │
              │               │        └──────┬──────┘
              │               │               │
              │               │   EmergencyStopViewModel  ← 新增
              │               │   ├─ triggerEmergencyStop()
              │               │   ├─ releaseEmergencyStop()
              │               │   ├─ safetyState (Q_PROPERTY)
              │               │   └─ isSystemLocked (Q_PROPERTY)
              │               │               │
              ▼               ▼               ▼
          AxisViewModelCore          EmergencyStopUseCase /
                                     ReleaseEmergencyStopUseCase
                                              │
                                     SystemManager::tryGetGroup()
                                              │
                                     EmergencyStopController
                                   (request / applyFeedback)
```

### 2.2 文件变更清单

| 操作 | 文件 | 说明 |
|------|------|------|
| **新建** | `presentation/viewmodel/EmergencyStopViewModel.h` | 急停 ViewModel（QObject 桥接器） |
| **修改** | `presentation/qml/blocks/ActionControlBlock.qml` | 替换急停按钮逻辑 + 添加状态感知 |
| **修改** | `Main.qml` | 注入 `emergencyViewModel` 属性 |
| **修改** | `main.cpp` | 创建 `EmergencyStopViewModel` 实例 |

---

## 三、EmergencyStopViewModel 设计

### 3.1 类设计

```cpp
class EmergencyStopViewModel : public QObject {
    Q_OBJECT

    // ────── 状态投影 ──────
    Q_PROPERTY(int safetyStateInt READ safetyStateInt NOTIFY safetyStateChanged)
    Q_PROPERTY(bool isSystemLocked READ isSystemLocked NOTIFY safetyStateChanged)
    Q_PROPERTY(bool isEmergencyStopped READ isEmergencyStopped NOTIFY safetyStateChanged)
    Q_PROPERTY(bool isTransitioning READ isTransitioning NOTIFY safetyStateChanged)
    Q_PROPERTY(bool isNotSynchronized READ isNotSynchronized NOTIFY safetyStateChanged)
    Q_PROPERTY(QString safetyStateText READ safetyStateText NOTIFY safetyStateChanged)
    
    // ────── 急停操作 ──────
    Q_INVOKABLE void triggerEmergencyStop();
    Q_INVOKABLE void releaseEmergencyStop();
};
```

### 3.2 属性语义

| 属性 | 类型 | 说明 |
|------|------|------|
| `safetyStateInt` | int | 0=NotSynchronized, 1=Running, 2=EmergencyStopping, 3=EmergencyStopped, 4=ReleasingEmergencyStop |
| `isSystemLocked` | bool | 系统锁定（含未同步、急停中、已急停、解除中） |
| `isEmergencyStopped` | bool | 是否处于已急停锁定 |
| `isTransitioning` | bool | 是否正处于过渡中 |
| `isNotSynchronized` | bool | 是否尚未与 PLC 同步 |
| `safetyStateText` | QString | 当前安全状态描述文本 |

### 3.3 错误反馈

当 `triggerEmergencyStop()` / `releaseEmergencyStop()` 执行失败时，通过 `lastError` 属性返回错误（如 "系统尚未同步PLC状态" / "系统未处于急停状态"）。

---

## 四、UI 设计原则

### 4.1 工业软件设计原则（"状态感" 比 "漂亮" 更重要）

```
╔══════════════════════════════════════════════════════════╗
║  工业惯例 — 急停按钮视觉指引：                          ║
║  ✅ 红色底（大红色 #D32F2F）                           ║
║  ✅ 白色文字（高对比度）                                ║
║  ✅ 圆角少（硬朗风格）                                  ║
║  ✅ 面积大（140*scale 保持）                            ║
║  ✅ 按下时有视觉反馈                                    ║
║  ✅ 有状态标识（不仅是一个按钮）                        ║
╚══════════════════════════════════════════════════════════╝
```

### 4.2 各状态下急停按钮行为

| 安全状态 | 急停按钮 | 按钮文字 | 按钮颜色 | 可点击 | 
|---------|---------|---------|---------|--------|
| NotSynchronized | 置灰 | "急 停" | 灰色 | ❌ |
| Running | 可用 | "急 停" | 红色 #D32F2F | ✅ 点击触发急停 |
| EmergencyStopping | 置灰 | "急停处理中..." | 橙红 | ❌ |
| EmergencyStopped | 可用 | "解除急停" | 橙红色 #FF5252 | ✅ 点击解除急停 |
| ReleasingEmergencyStop | 置灰 | "急停解除中..." | 黄色 | ❌ |

### 4.3 危险状态视觉反馈

当系统进入急停状态后，直接在 `ActionControlBlock` 区域展示：

1. **顶部红条闪烁**（可选）
2. **状态文本**：如 "⚠ SYSTEM EMERGENCY STOPPED"
3. **所有运动按钮自动灰**（通过 `isSystemLocked` 联动）

---

## 五、实现计划

### Step 1: 创建 `EmergencyStopViewModel.h`

创建 `presentation/viewmodel/EmergencyStopViewModel.h`，实现：
- 持有 `SystemManager&` + `groupName`
- `tick()` 方法在每帧读取 `EmergencyStopController` 状态并发射信号
- `triggerEmergencyStop()` 调用 `EmergencyStopUseCase`
- `releaseEmergencyStop()` 调用 `ReleaseEmergencyStopUseCase`

### Step 2: 修改 `ActionControlBlock.qml`

- 移除 `viewModel.stop()` 调用
- 接收 `emergencyViewModel` 属性
- 根据 `safetyState` 动态切换按钮行为
- 展示危险状态标识

### Step 3: 修改 `Main.qml`

- 向 `ActionControlBlock` 传递 `emergencyViewModel`

### Step 4: 修改 `main.cpp`

- 创建两个 `EmergencyStopViewModel` 实例（对应 Machine_A 和 Machine_B）
- 在 tick loop 中调用 `emergencyVM.tick()`
- 暴露给 QML 上下文

---

## 六、完整命令流模拟

### 6.1 触发急停完整链路

```
用户点击 "急停" 按钮
    │
    ▼
EmergencyStopViewModel::triggerEmergencyStop()
    │
    ├─ ① EmergencyStopUseCase::execute(m_manager, m_groupName)
    │     ├─ manager.tryGetGroup(groupName, ctx, reason)
    │     │    └─ 找到 Machine_A 的 SystemContext
    │     │
    │     ├─ controller.requestEmergencyStop()
    │     │    ├─ 当前状态: Running → 通过
    │     │    ├─ 生成 pending_intent = EmergencyStopCommand{true}
    │     │    ├─ 本地状态 Running → EmergencyStopping
    │     │    └─ 返回 SafetyRejection::None
    │     │
    │     └─ controller.hasPendingCommand() → true
    │          └─ driver->send(controller.popPendingCommand())
    │               └─ FakePLC::onEmergencyStopCommand(true)
    │                    ├─ 所有轴 → AxisState::Disabled
    │                    ├─ 清空所有运动指令
    │                    └─ m_emergencyStoppedState = true
    │
    ▼
下一帧 Tick Loop（10ms 后）
    │
    ├─ driver->pollFeedback(*ctx)
    │    └─ controller.applyFeedback(true)
    │         └─ 状态 EmergencyStopping → EmergencyStopped
    │
    ├─ emergencyVM.tick()
    │    ├─ 检测到 state() 变化
    │    ├─ emit safetyStateChanged()
    │    └─ QML 更新：按钮文字 → "解除急停"，状态文本 → "⚠ SYSTEM EMERGENCY STOPPED"
    │
    └─ 所有 AxisViewModel::tick()
         └─ tryGetAxis() → ContextRejection::SystemSafetyLocked
              └─ 所有轴运动按钮自动灰掉
```

### 6.2 解除急停完整链路

```
用户点击 "解除急停" 按钮
    │
    ▼
EmergencyStopViewModel::releaseEmergencyStop()
    │
    ├─ ① ReleaseEmergencyStopUseCase::execute(m_manager, m_groupName)
    │     ├─ controller.requestReleaseEmergencyStop()
    │     │    ├─ 当前状态: EmergencyStopped → 通过
    │     │    ├─ 生成 pending_intent = EmergencyStopCommand{false}
    │     │    ├─ 本地状态 EmergencyStopped → ReleasingEmergencyStop
    │     │    └─ 返回 SafetyRejection::None
    │     │
    │     └─ driver->send(controller.popPendingCommand())
    │          └─ FakePLC::onEmergencyStopCommand(false)
    │               ├─ m_emergencyStoppedState = false
    │               └─ 轴保持 Disabled（servoV6 默认 Disabled）
    │
    ▼
下一帧 Tick Loop
    │
    ├─ controller.applyFeedback(false)
    │    └─ 状态 ReleasingEmergencyStop → Running
    │
    ├─ emergencyVM.tick()
    │    └─ QML 更新：按钮文字 → "急 停"，状态文本清除
    │
    └─ 轴全部 Disabled，用户发起运动时 EnsureEnabled 自动使能
```

### 6.3 物理急停按钮按下（外部事件）

```
物理急停按钮被按下（PLC 层面）
    │
    ▼
FakePLC::m_emergencyStoppedState 被硬件置为 true
    │
    ▼
下一帧 Tick Loop
    │
    ├─ controller.applyFeedback(true)
    │    └─ 当前状态 Running → EmergencyStopped（直接从 1 → 3，不经过 EmergencyStopping）
    │
    └─ QML 更新：按钮变为 "解除急停"，所有运动灰掉
```

---

## 七、总结

| 关键点 | 说明 |
|-------|------|
| **ViewModel 新增** | `EmergencyStopViewModel` — 分组级安全桥接器 |
| **按钮逻辑替换** | `viewModel.stop()` → `emergencyViewModel.triggerEmergencyStop()` / `releaseEmergencyStop()` |
| **状态驱动 UI** | 按钮文字、颜色、可用性由 `safetyState` 驱动 |
| **工业视觉** | 红底白字、硬朗风格、状态标识、危险警示 |
| **命令路径** | UI → UseCase → EmergencyStopController → Driver → PLC → Feedback → Controller |
