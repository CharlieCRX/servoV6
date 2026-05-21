# Jog 完成后 `setRelativeZero` 失败原因分析文档

## 问题现象

用户完成 Machine_A/Y 轴的点动操作后，尝试设置相对零点 (`setRelativeZero`) 失败，日志输出：

```
[16:27:47.284][WARN][UI][AxisVM][Machine_A][Y][1063979541136300_3] Machine_A/Y error from SetRelZero: AXIS_INVALID_STATE - Axis is in an invalid state for this operation
[16:27:47.284][WARN][UI][AxisVM][Machine_A][Y][1063979541136300_3] Machine_A/Y setRelativeZero rejected: AXIS_INVALID_STATE
```

用户期望：点动停止后，当前位置可被设置为相对零点，使后续相对坐标 = 绝对坐标 - 新的相对零点位置。

## 根因分析

### 根因：JogOrchestrator 在点动完成后将轴置于 Disabled 状态

JogOrchestrator 的完整流程是：

```
Idle -> EnsuringEnabled -> IssuingJog -> Jogging -> IssuingStop -> WaitingForIdle -> EnsuringDisabled -> Done
```

关键在最后一个阶段：`EnsuringDisabled`。该阶段会下发 `EnableCommand{false}`（即掉电指令），将轴从 `Idle` 状态切换到 `Disabled` 状态。这是设计意图----点动完成后自动掉电。

### 根因：Axis::setRelativeZero() 的状态准入过于严格

`Axis::setRelativeZero()` 的实现：

```cpp
bool Axis::setRelativeZero() {
    // 必须为 Idle 状态
    if (m_state != AxisState::Idle) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }
    // ...
}
```

该方法严格要求轴必须处于 `Idle` 状态才能执行。但点动完成后轴处于 `Disabled` 状态，因此被拒绝。

### 完整调用链

```
用户按下 setRelativeZero 按钮
  -> AxisViewModelCore::setRelativeZero()
    -> axis->setRelativeZero()
      -> 检查 m_state == AxisState::Idle -> 失败（当前为 Disabled）
      -> 返回 false, lastRejection = InvalidState
    -> pushError("AXIS_INVALID_STATE")
```

## 日志佐证

点动完成后遥测信息显示轴状态为 `Disabled`：

```
[16:27:46.925][SUMMARY][UI][Telemetry][N/A][N/A][N/A] === Machine_A === Y: pos=+33.8 Disabled
```

设置相对零点时日志显示：

```
[16:27:47.284][WARN][UI][AxisVM][Machine_A][Y][1063979541136300_3] Machine_A/Y error from SetRelZero: AXIS_INVALID_STATE
```

此后连续多次尝试均因轴仍为 `Disabled` 状态而失败（用户连续点击了多次按钮），每次都被同一原因拒绝：

```
[16:27:51.244][WARN][UI][AxisVM][Machine_A][Y][1063983501408900_4] Machine_A/Y error from SetRelZero: AXIS_INVALID_STATE
[16:27:51.940][WARN][UI][AxisVM][Machine_A][Y][1063984197271900_5] Machine_A/Y error from SetRelZero: AXIS_INVALID_STATE
...
```

遥测中 `errs=6` 即累积了 6 次拒绝。

## 修复思路

### 方案 A：扩展 setRelativeZero/clearRelativeZero 的状态准入范围（推荐）

`setRelativeZero` 和 `clearRelativeZero` 是**纯坐标操作**----它们不涉及任何物理运动，只是记录/清除一个基准位置值。即使在 `Disabled` / `Unknown` 状态下，轴同样拥有有效的绝对位置读数，可以用来设置相对零点。

修复：将状态约束从 `== Idle` 放宽为 `!= Unknown`（即排除初始未知状态即可，`Disabled` 也允许）：

```cpp
bool Axis::setRelativeZero() {
    // 必须不是 Unknown（即所有已知状态都可设置）
    if (m_state == AxisState::Unknown) {
        m_last_rejection = RejectionReason::InvalidState;
        return false;
    }
    // ...
}
```

同理 `clearRelativeZero()` 和 `zeroAbsolutePosition()` 也可考虑放宽。

### 方案 B：JogOrchestrator 不自动掉电

修改 JogOrchestrator 去掉 `EnsuringDisabled` 阶段，点动停止后停在 `Done` 状态且轴保持在 `Idle` 状态。

但此方案改变了编排器的设计意图（点动完成后自动掉电以保证安全），且会影响所有使用 JogOrchestrator 的场景，修改范围较大。

### 方案 C：在 ViewModel 层加入自动使能

在 `setRelativeZero()` 调用前，如果检测到轴处于 `Disabled` 状态，先自动使能，完成操作后再决定是否掉电。

此方案会引入副作用（使能-操作-掉电的完整流程），且使能/掉电本身有延迟，用户体验不佳。

### 推荐：方案 A

理由：
1. 修改范围最小，仅影响领域层一个方法的状态检查逻辑
2. 与真实 PLC/伺服系统的行为一致----即使轴处于掉电状态，操作面板上也可以设置相对零点
3. 不影响安全，因为 `setRelativeZero` 不产生任何物理运动
4. 不改变 JogOrchestrator 的现有行为（安全掉电逻辑保持不变）

## 涉及的文件

| 文件 | 角色 |
|------|------|
| `domain/entity/Axis.cpp` | 修改 `setRelativeZero()` 和 `clearRelativeZero()` 的状态准入逻辑 |
| `domain/entity/Axis.h` | 无修改（接口不变） |
| `application/policy/JogOrchestrator.h` | 无修改（工作原理不变） |
| `presentation/viewmodel/AxisViewModelCore.cpp` | 无修改 |
