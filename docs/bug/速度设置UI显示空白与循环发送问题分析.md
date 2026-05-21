# 速度设置 UI 显示空白、循环发送与 SetJogVelocityCommand 丢失问题分析

## 现象

1. **UI 速度显示空白**：在 UI 上设置点动速度或定位速度后，UI 上不展示当前设置的速度值。
2. **循环发送**：设置速度后，日志显示 `AxisVM` 持续在每次 tick 中重复发送 `SetMoveVelocityCommand` / `SetJogVelocityCommand`，永不停歇。
3. **SetJogVelocityCommand 丢失**：日志中 `SetJogVelocityCommand` 从未被 `AxisVM` 发送到 HAL 层（PLC），只有 `SetMoveVelocityCommand` 被发送。

## 对应日志片段

```
[08:54:34.706][DEBUG][DOM][Axis][N/A][N/A][N/A] setJogVelocity(v=15.000000) entry: state=1 pending=Empty
[08:54:34.706][DEBUG][DOM][Axis][N/A][N/A][N/A] setJogVelocity: PASS -> pending=SetJogVelocityCommand(velocity=15.000000)
[08:54:34.706][DEBUG][DOM][Axis][N/A][N/A][N/A] setMoveVelocity(v=20.000000) entry: state=1 pending=SetJogVelocityCommand(...)
[08:54:34.706][DEBUG][DOM][Axis][N/A][N/A][N/A] setMoveVelocity: PASS -> pending=SetMoveVelocityCommand(velocity=20.000000)
[08:54:34.707][DEBUG][UI][AxisVM][N/A][N/A][N/A] Machine_A/Y jogVelocity set to 15.000000
[08:54:34.707][DEBUG][UI][AxisVM][N/A][N/A][N/A] Machine_A/Y moveVelocity set to 20.000000

[08:54:34.716][DEBUG][UI][AxisVM][N/A][N/A][N/A] Machine_A/Y sending: SetMoveVelocityCommand(velocity=20.000000)
[08:54:34.716][DEBUG][HAL][PLC][N/A][N/A][N/A] process: axis=Y cmd=SetMoveVelocityCommand(velocity=20.000000) state=1
[08:54:34.725][DEBUG][UI][AxisVM][N/A][N/A][N/A] Machine_A/Y sending: SetMoveVelocityCommand(velocity=20.000000)
[08:54:34.725][DEBUG][HAL][PLC][N/A][N/A][N/A] process: axis=Y cmd=SetMoveVelocityCommand(velocity=20.000000) state=1
... (无限重复)
```

**关键观察**：日志中 `setJogVelocity` 将 pending 设为 `SetJogVelocityCommand`，但紧接着 `setMoveVelocity` 又将其覆盖为 `SetMoveVelocityCommand`。最终只有 `SetMoveVelocityCommand` 被发送出去，`SetJogVelocityCommand` 永远不见天日。

## 根因分析

### 问题 1：VelocitySettingsPopup 保存时同时调用两种 setVelocity

**根因**：旧的 `VelocitySettingsPopup.qml` 在点击"保存"按钮时，**同时调用**了 `viewModel.setJogVelocity(value)` 和 `viewModel.setMoveVelocity(value)`。

由于 `Axis.setJogVelocity` 和 `Axis.setMoveVelocity` **共用同一个 `m_pending_intent` 槽位**（类型为 `AxisCommand = std::variant<std::monostate, ...>`），后调用的 `setMoveVelocity` 会**覆盖**前者的 `SetJogVelocityCommand`。导致 `SetJogVelocityCommand` **永远无法到达 PLC**。

**后果**：
- Jog 速度的 pending 命令被静默覆盖
- `consumePendingCommands()` 只发送 `SetMoveVelocityCommand`
- `pollFeedback()` 后，`m_jog_velocity` 没有被 feedback 更新（因为 jog speed 从未发送到 PLC）
- 下一次 UI 刷新时，`jogVelocity` 显示回旧值（如 0.0）

修复后预期：
- Jog 面板的"⚙️"按钮只打开 `speedType="jog"` 的弹窗
- POS 面板的"⚙️"按钮只打开 `speedType="move"` 的弹窗
- 每个弹窗的"保存"按钮只调用对应的 setter

### 问题 2：FakePLC 未同步速度到 feedback 寄存器

**根因**：`FakePLC::processCommand(SetJogVelocityCommand)` 和 `processCommand(SetMoveVelocityCommand)` 只更新了内部引擎字段 `axis.jog_velocity` / `axis.move_velocity`，但没有同步更新反馈寄存器 `axis.feedback.getjogVelocity` / `axis.feedback.getMoveVelocity`。

导致：
1. **UI 速度显示空白**：`AxisViewModelCore::jogVelocity()` 通过 `axis->getjogVelocity()` 读取，而 `getjogVelocity()` 返回 `m_jog_velocity`，该值来自 `applyFeedback` 中的 `feedback.getjogVelocity`。因 feedback 从未更新，所以 `m_jog_velocity` 始终为 0。
2. **循环发送**：`Axis::applyFeedback` 中 SetVelocity 闭环检测逻辑是 `m_jog_velocity == cmd->velocity`。因 `m_jog_velocity` 始终为旧值，条件永远不成立，`m_pending_intent` 永不关闭。`consumePendingCommands()` 每次 tick 都检测到 pending → 无限循环发送。

### 根因示意图

```
问题 1（UI 层）:
  VelocitySettingsPopup 保存
    → viewModel.setJogVelocity(15)    // pending = SetJogVelocityCommand(15)
    → viewModel.setMoveVelocity(20)   // pending = SetMoveVelocityCommand(20) ⚡ 覆盖
    → SetJogVelocityCommand 丢失 ❌

问题 2（HAL 层）:
  FakePLC::processCommand(SetMoveVelocityCommand(20))
    → axis.move_velocity = 20          // 内部字段 ✅
    → axis.feedback.getMoveVelocity = 0 // 未同步 ❌
      → pollFeedback()
        → Axis::applyFeedback()
          → m_move_velocity = 0        // 从 feedback 镜像到 0 ❌
          → m_move_velocity(0) == cmd->velocity(20) → false
          → pending_intent 永不关闭
          → 下次 tick 继续发送 → 无限循环
```

## 修改方案

### 文件 1：`presentation/qml/components/VelocitySettingsPopup.qml`

新增 `speedType` 属性，区分点动/定位模式：

```qml
// ⭐ 速度类型选择："jog" = 点动速度, "move" = 定位速度
property string speedType: "jog"
```

`onOpened` 时根据 `speedType` 选择合适的标题和初始值：

```qml
onOpened: {
    if (speedType === "jog") {
        popupTitle.text = "⚙️ 点动速度设置"
        speedInput.text = viewModel.jogVelocity.toString()
    } else {
        popupTitle.text = "⚙️ 定位速度设置"
        speedInput.text = viewModel.moveVelocity.toString()
    }
}
```

`onClicked` 保存时只调用对应的 setter：

```qml
if (speedType === "jog") {
    viewModel.setJogVelocity(value)
} else {
    viewModel.setMoveVelocity(value)
}
```

### 文件 2：`presentation/qml/blocks/ActionControlBlock.qml`

创建两个独立的弹窗实例，分别绑定不同的 `speedType`：

```qml
// Jog 模式下使用的点动速度弹窗
VelocitySettingsPopup {
    id: jogVelocityPopup
    viewModel: root.viewModel
    speedType: "jog"
}

// POS 模式下使用的定位速度弹窗
VelocitySettingsPopup {
    id: moveVelocityPopup
    viewModel: root.viewModel
    speedType: "move"
}
```

Jog 面板的⚙️按钮 → `jogVelocityPopup.open()`
POS 面板的⚙️按钮 → `moveVelocityPopup.open()`

### 文件 3：`infrastructure/FakePLC.h`

在速度命令处理中同步 feedback 寄存器：

```cpp
void processCommand(AxisId id, const SetJogVelocityCommand& cmd) {
    auto& axis = m_axes.at(id);
    // ... 日志 ...
    axis.jog_velocity = std::abs(cmd.velocity);
    axis.feedback.getjogVelocity = std::abs(cmd.velocity);  // ← 新增
}

void processCommand(AxisId id, const SetMoveVelocityCommand& cmd) {
    auto& axis = m_axes.at(id);
    // ... 日志 ...
    axis.move_velocity = std::abs(cmd.velocity);
    axis.feedback.getMoveVelocity = std::abs(cmd.velocity);  // ← 新增
}
```

同步修改测试辅助方法：

```cpp
void setSimulatedJogVelocity(AxisId id, double v) {
    m_axes.at(id).jog_velocity = std::abs(v);
    m_axes.at(id).feedback.getjogVelocity = std::abs(v);
}

void setSimulatedMoveVelocity(AxisId id, double v) {
    m_axes.at(id).move_velocity = std::abs(v);
    m_axes.at(id).feedback.getMoveVelocity = std::abs(v);
}
```

## 修复后数据流

```
场景：用户在 Jog 面板设置点动速度 15 mm/s

  VelocitySettingsPopup (speedType="jog") 保存
    → viewModel.setJogVelocity(15)
      → Axis::setJogVelocity(15)
        → pending_intent = SetJogVelocityCommand(15)
          → tick()
            → consumePendingCommands()
              → drv->send(SetJogVelocityCommand(15))
                → FakePLC::processCommand()  ← 同时更新 internal + feedback ✅
                  → pollFeedback()
                    → Axis::applyFeedback()
                      → m_jog_velocity = 15 ✅
                      → m_jog_velocity(15) == cmd->velocity(15) → true
                      → pending_intent CLOSED ✅
                      → 下次 tick 不再发送 ✅
                      → UI jogVelocity 显示 15 ✅
```

## 相关文件

| 文件 | 角色 |
|------|------|
| `presentation/qml/components/VelocitySettingsPopup.qml` | **修改点** — 新增 speedType 属性，按类型只调用对应 setter |
| `presentation/qml/blocks/ActionControlBlock.qml` | **修改点** — Jog/POS 分别绑定不同 speedType 的弹窗实例 |
| `infrastructure/FakePLC.h` | **修改点** — 速度命令处理中同步 feedback 寄存器 |
| `domain/entity/Axis.cpp` | 闭环检测逻辑（已正确，无需修改） |
| `presentation/viewmodel/AxisViewModelCore.cpp` | consumePendingCommands（已正确，无需修改） |
| `domain/entity/Axis.h` | Axis 定义（已正确，无需修改） |

## 结构问题总结

本次暴露了两个层级的耦合问题：

### 层级 1：UI 层（QML）— 弹窗实例复用导致的意图冲突

**问题**：两个不同的业务操作（设置点动速度、设置定位速度）**共用一个弹窗实例**，且该弹窗的保存按钮同时调用了两个 setter，导致意图覆盖。

**解决方案**：创建独立的弹窗实例，每个实例只绑定一种速度模式。

**原则**：当两个 UI 操作在 domain 层使用**不同的 command type**（`SetJogVelocityCommand` vs `SetMoveVelocityCommand`）时，UI 组件也应当保持隔离，避免通过同一个交互入口同时触发两个不同类型的命令。

### 层级 2：HAL 层（FakePLC）— 仿真层数据一致性问题

**问题**：`FakePLC` 内部维护了两套速度状态：
- **内部引擎字段** `move_velocity` / `jog_velocity`：用于 `updateKinematics()` 的运动学推演  
- **反馈寄存器字段** `feedback.getMoveVelocity` / `feedback.getjogVelocity`：用于 `getFeedback()` 返回给领域层

修改内部引擎字段时未同步更新反馈寄存器，导致下游 `applyFeedback` 永远无法验证数值收敛。

**解决方案**：任何修改内部引擎字段的地方，必须同步更新对应的反馈寄存器字段。

**本质**：这是一个在**仿真层**容易出现的**数据一致性问题**（真实 PLC 中反馈寄存器由硬件自动更新，不存在此问题）。对于仿真设备，**命令处理（command handler）**和**反馈生成（feedback producer）**之间的数据同步需要手动维护，设计时应警惕。
