# Phase 5：Presentation 层重构实现文档（v2 — 独立按钮映射版）

> 状态：实施方案文档  
> 日期：2026-05-29  
> 项目：servoV6  
> 依赖：Domain 层新增 4 个 Command、Application 层新增 Policy（见《MoveCommand耦合问题分析与整改方案》阶段 1~4 已实施）

---

## 目录

1. [设计原则](#1-设计原则)
2. [ViewModel 层变更（AxisViewModelCore）](#2-viewmodel-层变更axisviewmodelcore)
3. [桥接层变更（QtAxisViewModel）](#3-桥接层变更qtaxisviewmodel)
4. [QML 层变更（ActionControlBlock）](#4-qml-层变更actioncontrolblock)
5. [完整调用链](#5-完整调用链)
6. [改动文件清单](#6-改动文件清单)
7. [实施步骤](#7-实施步骤)
8. [回归验证](#8-回归验证)

---

## 1. 设计原则

### 1.1 核心纠正

> **原方案错误**（已废弃）：  
> QML 仍调用 `moveAbsolute(target)` / `moveRelative(distance)`，ViewModel 内部拆为两步。

> **v2 正确方案**：  
> 界面上每个操作按钮**独立映射**到对应的 ViewModel 函数，不做统一封装。  
> `moveAbsolute()` / `moveRelative()` **移除**，替换为 4 个独立函数：
>
> | 用户操作 | QML 调用的 ViewModel 函数 | 含义 |
> |----------|--------------------------|------|
> | 输入 target → 点击"设置绝对目标" | `setAbsTarget(target)` | 仅写 ABS_TARGET D 寄存器 |
> | 点击"绝对定位 GO" | `triggerAbsMove()` | 触发 ABS_MOVE_TRIGGER M 寄存器（走 Policy 编排） |
> | 输入 distance → 点击"设置相对距离" | `setRelTarget(distance)` | 仅写 REL_TARGET D 寄存器 |
> | 点击"相对定位 GO" | `triggerRelMove()` | 触发 REL_MOVE_TRIGGER M 寄存器（走 Policy 编排） |

### 1.2 设计目标

| 目标 | 说明 |
|------|------|
| **按钮独立映射** | 每个按钮对应一个 Q_INVOKABLE 函数，无统一封装 |
| **界面风格最小改动** | 保持现有布局结构（输入框 + 按钮），仅将"执行 GO"拆为"设置目标"+"触发"两个按钮组 |
| **绝对/相对独立组** | 绝对定位一组（设置 + 触发），相对定位一组（设置 + 触发），控件各自独立 |
| **Loading 状态** | 暴露 `isLoading` / `moveStep` 属性，Policy 运行期间 QML 展示状态 |
| **Feedback 自动更新** | 位置数据通过现有 feedback 轮询链路自动更新，无需额外处理 |

### 1.3 与现有架构的一致性

`setAbsTarget()` / `setRelTarget()` 与项目现有的 `setJogVelocity()` / `setMoveVelocity()` / `zeroAbsolutePosition()` 采用**完全相同的模式**——ViewModel 直调 Domain 校验 → 生成 pending command → `consumePendingCommands()` 统一消费发送。

---

## 2. ViewModel 层变更（AxisViewModelCore）

### 2.1 头文件变更

```cpp
// presentation/viewmodel/AxisViewModelCore.h

// ★ 新增前向声明
class AbsMovePolicy;
class RelMovePolicy;

class AxisViewModelCore {
public:
    // ... 现存接口保持不变 ...

    // ── ⚠️ 移除的接口 ──
    // void moveAbsolute(double targetPos);   // ★ 删除
    // void moveRelative(double distance);    // ★ 删除

    // ── ★ 新增：4 个独立操作入口 ──
    void setAbsTarget(double target);      // ★ 设置绝对移动目标（仅写 PLC，不触发运动）
    void triggerAbsMove();                 // ★ 触发绝对移动（走 AbsMovePolicy 编排）
    void setRelTarget(double distance);    // ★ 设置相对移动距离（仅写 PLC，不触发运动）
    void triggerRelMove();                 // ★ 触发相对移动（走 RelMovePolicy 编排）

    // ── ★ 新增：Loading 状态查询（供 QML 显示加载指示器）──
    bool isLoading() const;                // ★ Policy 运行中返回 true
    std::string moveStep() const;          // ★ 当前编排步骤的可读字符串（调试/日志用）

    // ... 其它现存接口保持不变 ...

private:
    // ... 现存成员不变 ...

    // ★ 新增：Policy 策略层成员
    std::unique_ptr<AbsMovePolicy> m_absPolicy;
    std::unique_ptr<RelMovePolicy> m_relPolicy;

    // ⚠️ 旧编排器保留（向后兼容，逐步退役）
    std::unique_ptr<AutoAbsMoveOrchestrator>  m_absOrch;  // 保留不动
    std::unique_ptr<AutoRelMoveOrchestrator>  m_relOrch;  // 保留不动

    // ★ 模板方法扩展
    template<typename PolicyOrOrch>
    void collectPolicyOrOrchError(PolicyOrOrch& p, const std::string& source);
};
```

### 2.2 构造函数变更

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

AxisViewModelCore::AxisViewModelCore(SystemManager& manager,
                                     const std::string& groupName,
                                     AxisId axisId)
    : m_manager(manager)
    , m_groupName(groupName)
    , m_axisId(axisId)
    , m_enableUc(std::make_unique<EnableUseCase>())
    , m_jogUc(std::make_unique<JogAxisUseCase>())
    , m_stopUc(std::make_unique<StopAxisUseCase>())
    , m_jogOrch(std::make_unique<JogOrchestrator>(manager, groupName))
    , m_absOrch(std::make_unique<AutoAbsMoveOrchestrator>(manager, groupName))   // ⚠️ 保留
    , m_relOrch(std::make_unique<AutoRelMoveOrchestrator>(manager, groupName))   // ⚠️ 保留
    , m_absPolicy(std::make_unique<AbsMovePolicy>(manager, groupName))           // ★ 新增
    , m_relPolicy(std::make_unique<RelMovePolicy>(manager, groupName))           // ★ 新增
{
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " ViewModel created");
}
```

> **注意**：构造函数移除 `m_moveAbsUc` / `m_moveRelUc` 的初始化，这两个 UseCase 不再需要。相应地，移除头文件中对应的 `std::unique_ptr<MoveAbsoluteUseCase>` 和 `std::unique_ptr<MoveRelativeUseCase>` 成员声明。

### 2.3 新增方法实现

#### 2.3.1 setAbsTarget —— 与 setJogVelocity 模式一致

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
```

#### 2.3.2 setRelTarget —— 对称实现

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

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

#### 2.3.3 triggerAbsMove —— 走 Policy 编排

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::triggerAbsMove() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " triggerAbsMove requested");

    m_absPolicy->startAbs(m_axisId);  // ★ 不传 target（PLC 已有）

    if (m_absPolicy->hasError()) {
        auto vmError = translate(m_absPolicy->lastError());
        pushError(vmError, "AbsPolicy");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " triggerAbsMove rejected: " + vmError.code);
    }
}
```

#### 2.3.4 triggerRelMove —— 对称实现

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::triggerRelMove() {
    TraceScope scope(m_groupName, axisIdToString(m_axisId), generateTraceId());
    LOG_INFO(LogLayer::UI, "AxisVM",
        logPrefix() + " triggerRelMove requested");

    m_relPolicy->startRel(m_axisId);  // ★ 不传 distance（PLC 已有）

    if (m_relPolicy->hasError()) {
        auto vmError = translate(m_relPolicy->lastError());
        pushError(vmError, "RelPolicy");
        LOG_WARN(LogLayer::UI, "AxisVM",
            logPrefix() + " triggerRelMove rejected: " + vmError.code);
    }
}
```

### 2.4 consumePendingCommands 扩展

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::consumePendingCommands() {
    // ... 前面代码不变 ...

    // ★ 将 isZeroOrVelocity 重命名为 isSimpleWrite，
    //    追加 SetAbsTargetCommand / SetRelTargetCommand：
    bool isSimpleWrite =
        std::holds_alternative<ZeroAbsoluteCommand>(cmd) ||
        std::holds_alternative<SetRelativeZeroCommand>(cmd) ||
        std::holds_alternative<ClearRelativeZeroCommand>(cmd) ||
        std::holds_alternative<SetJogVelocityCommand>(cmd) ||
        std::holds_alternative<SetMoveVelocityCommand>(cmd) ||
        std::holds_alternative<SetAbsTargetCommand>(cmd) ||     // ★ 新增
        std::holds_alternative<SetRelTargetCommand>(cmd);       // ★ 新增

    if (!isSimpleWrite) {
        return;  // 触发类命令由 Policy 处理
    }

    // ... 后面消费发送逻辑不变 ...
}
```

### 2.5 tick() 方法变更

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

void AxisViewModelCore::tick() {
    // Step 1: 消费 pending commands（★ 此处消费 setAbsTarget / setRelTarget 等简单写入）
    consumePendingCommands();

    // Step 2: 驱动 Policy tick（移动触发编排）
    if (m_absPolicy && !m_absPolicy->isDone() && !m_absPolicy->hasError()) {
        m_absPolicy->tick();
        if (m_absPolicy->hasError()) {
            auto vmError = translate(m_absPolicy->lastError());
            pushError(vmError, "AbsPolicy");
        }
    }

    if (m_relPolicy && !m_relPolicy->isDone() && !m_relPolicy->hasError()) {
        m_relPolicy->tick();
        if (m_relPolicy->hasError()) {
            auto vmError = translate(m_relPolicy->lastError());
            pushError(vmError, "RelPolicy");
        }
    }

    // Step 3: 驱动旧编排器 tick（向后兼容）
    m_jogOrch->tick();
    m_absOrch->tick();
    m_relOrch->tick();

    // Step 4: 收集旧编排器错误
    collectOrchError(*m_jogOrch, "JogOrch");
    collectOrchError(*m_absOrch, "AbsOrch");
    collectOrchError(*m_relOrch, "RelOrch");

    // Step 5: 日志摘要
    LOG_TRACE_EVERY_N(100, LogLayer::UI, "AxisVM",
        logPrefix()
        + " tick: state=" + std::to_string(static_cast<int>(state()))
        + " errors=" + std::to_string(m_errorHistory.size()));
}
```

### 2.6 isLoading / moveStep 辅助方法

```cpp
// presentation/viewmodel/AxisViewModelCore.cpp

bool AxisViewModelCore::isLoading() const {
    // Policy 处于活动编排中（从 EnsuringEnabled 到 Disabling），视为 loading
    bool absActive = m_absPolicy &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Initial &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Done &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Error;

    bool relActive = m_relPolicy &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Initial &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Done &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Error;

    return absActive || relActive;
}

std::string AxisViewModelCore::moveStep() const {
    if (m_absPolicy &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Initial &&
        m_absPolicy->currentStep() != AbsMovePolicy::Step::Done) {
        switch (m_absPolicy->currentStep()) {
            case AbsMovePolicy::Step::EnsuringEnabled:     return "EnsuringEnabled";
            case AbsMovePolicy::Step::TriggeringMove:      return "TriggeringMove";
            case AbsMovePolicy::Step::WaitingMotionStart:  return "WaitingMotionStart";
            case AbsMovePolicy::Step::WaitingMotionFinish: return "WaitingMotionFinish";
            case AbsMovePolicy::Step::Disabling:           return "Disabling";
            case AbsMovePolicy::Step::Error:               return "Error";
            default: return "Unknown";
        }
    }
    if (m_relPolicy &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Initial &&
        m_relPolicy->currentStep() != RelMovePolicy::Step::Done) {
        switch (m_relPolicy->currentStep()) {
            case RelMovePolicy::Step::EnsuringEnabled:     return "EnsuringEnabled";
            case RelMovePolicy::Step::TriggeringMove:      return "TriggeringMove";
            case RelMovePolicy::Step::WaitingMotionStart:  return "WaitingMotionStart";
            case RelMovePolicy::Step::WaitingMotionFinish: return "WaitingMotionFinish";
            case RelMovePolicy::Step::Disabling:           return "Disabling";
            case RelMovePolicy::Step::Error:               return "Error";
            default: return "Unknown";
        }
    }
    return "Idle";
}
```

---

## 3. 桥接层变更（QtAxisViewModel）

### 3.1 头文件变更

```cpp
// presentation/viewmodel/QtAxisViewModel.h

class QtAxisViewModel : public QObject {
    Q_OBJECT

    // ... 现有 Q_PROPERTY 保持不变 ...

    // ★ 新增 Q_PROPERTY（供 QML 绑定 loading 状态）
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY loadingChanged)
    Q_PROPERTY(QString moveStep READ moveStep NOTIFY moveStepChanged)

public:
    // ... 现有 Getters 保持不变 ...

    // ── ⚠️ 移除的 Q_INVOKABLE ──
    // Q_INVOKABLE void moveAbsolute(double targetPos);   // ★ 删除
    // Q_INVOKABLE void moveRelative(double distance);    // ★ 删除

    // ── ★ 新增：4 个独立操作入口 ──
    Q_INVOKABLE void setAbsTarget(double target);
    Q_INVOKABLE void triggerAbsMove();
    Q_INVOKABLE void setRelTarget(double distance);
    Q_INVOKABLE void triggerRelMove();

    // ── ★ 新增：Loading 状态查询 ──
    bool isLoading() const;
    QString moveStep() const;

signals:
    // ... 现有 signals 保持不变 ...
    void loadingChanged();      // ★ 新增
    void moveStepChanged();     // ★ 新增

    // ... 其余代码不变 ...
};
```

### 3.2 实现文件变更

```cpp
// presentation/viewmodel/QtAxisViewModel.cpp

// ── ⚠️ 删除以下两个方法的实现 ──
// void QtAxisViewModel::moveAbsolute(double targetPos) { m_core->moveAbsolute(targetPos); }
// void QtAxisViewModel::moveRelative(double distance) { m_core->moveRelative(distance); }

// ── ★ 新增 4 个独立方法实现 ──

void QtAxisViewModel::setAbsTarget(double target) {
    m_core->setAbsTarget(target);
}

void QtAxisViewModel::triggerAbsMove() {
    m_core->triggerAbsMove();
    emit loadingChanged();
}

void QtAxisViewModel::setRelTarget(double distance) {
    m_core->setRelTarget(distance);
}

void QtAxisViewModel::triggerRelMove() {
    m_core->triggerRelMove();
    emit loadingChanged();
}

// ── ★ 新增 Loading 状态查询实现 ──

bool QtAxisViewModel::isLoading() const {
    return m_core->isLoading();
}

QString QtAxisViewModel::moveStep() const {
    return QString::fromStdString(m_core->moveStep());
}
```

---

## 4. QML 层变更（ActionControlBlock）

### 4.1 设计说明

> **界面布局最小改动原则**：
> - 保留现有的"绝对/相对 RadioButton 切换"
> - 保留现有的输入框
> - 将原来的单个"执行 GO"按钮**拆为两个按钮**："设置目标"和"触发移动"
> - 绝对定位和相对定位各有一组独立控件（输入框 + 设置按钮 + 触发按钮）
> - 布局结构与原来相近，仅在定位面板中增加一行按钮

### 4.2 新增 QML 属性（绑定 ViewModel 状态）

```qml
// ActionControlBlock.qml —— 新增属性

// ★ 定位模式下触发是否就绪：
//    - 系统未锁定
//    - 无故障
//    - 非运动中（state ≤ Idle）
//    - 非龙门操作锁定
property bool isReadyForTrigger: !systemLocked && !gantryOperationLocked && viewModel ? 
    (!viewModel.hasError && viewModel.state <= 2 && !viewModel.isLoading) : false

// ★ 设置目标是否就绪（同触发条件，但 loading 时仍可设置新目标覆盖旧目标）：
property bool isReadyForSetTarget: !systemLocked && !gantryOperationLocked && viewModel ? 
    (!viewModel.hasError && viewModel.state <= 2) : false
```

### 4.3 定位控制面板（完整替换）

> **替换范围**：将原 `// --- B. 定位控制面板 ---` 整段替换为以下内容。

```qml
// ==========================================
// B. 定位控制面板（★ v2 重新设计：独立按钮映射）
// ==========================================
ColumnLayout {
    anchors.fill: parent
    spacing: 8 * Theme.scale
    visible: root.currentMode === 1

    // 顶部留空
    Item { Layout.preferredHeight: 4 * Theme.scale }

    // 定位速度设定
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 8 * Theme.scale

        Text {
            text: "定位速度: " + (viewModel ? viewModel.moveVelocity.toFixed(1) : "0.0") + " mm/s"
            color: Theme.textMain
            font.pixelSize: Theme.fontNormal
            font.family: "Monospace"
        }

        IndustrialButton {
            text: "⚙️"
            buttonSize: 30 * Theme.scale
            isCircle: true
            baseColor: Theme.panelBg
            enabled: root.isReadyForSetTarget
            onClicked: moveVelocityPopup.open()
        }
    }

    // 绝对/相对 单选（紧凑，紧贴速度行）
    RowLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 4 * Theme.scale

        RadioButton {
            text: "绝对"
            checked: root.isAbsolute
            enabled: root.isReadyForSetTarget
            opacity: enabled ? 1.0 : 0.5
            onClicked: root.isAbsolute = true
            contentItem: Text {
                text: parent.text
                color: Theme.textMain
                font.pixelSize: Theme.fontSmall
                leftPadding: parent.indicator.width + 2
            }
        }

        RadioButton {
            text: "相对"
            checked: !root.isAbsolute
            enabled: root.isReadyForSetTarget
            opacity: enabled ? 1.0 : 0.5
            onClicked: root.isAbsolute = false
            contentItem: Text {
                text: parent.text
                color: Theme.textMain
                font.pixelSize: Theme.fontSmall
                leftPadding: parent.indicator.width + 2
            }
        }
    }

    // 上半弹簧
    Item { Layout.fillHeight: true }

    // ── ★ 绝对定位组 ──
    ColumnLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 6 * Theme.scale
        visible: root.isAbsolute

        // 目标值输入
        TextField {
            id: absTargetInput
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 140 * Theme.scale
            text: "100.0"
            enabled: root.isReadyForSetTarget
            opacity: enabled ? 1.0 : 0.5
            font.pixelSize: Theme.fontLarge
            font.family: "Monospace"
            color: Theme.textMain
            horizontalAlignment: TextInput.AlignHCenter
            background: Rectangle {
                color: Theme.bgDark
                border.color: absTargetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                border.width: 2
                radius: 6 * Theme.scale
            }
            validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
        }

        // 按钮组：设置目标 + 触发移动
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8 * Theme.scale

            // ★ 按钮 1：设置绝对目标
            IndustrialButton {
                text: root.isReadyForSetTarget ? "设置目标" : "不可用"
                isCircle: false
                buttonSize: 110 * Theme.scale
                enabled: root.isReadyForSetTarget
                baseColor: root.isReadyForSetTarget ? Theme.panelBg : Theme.colorDisabled
                onClicked: {
                    if (!root.isReadyForSetTarget) return
                    let target = parseFloat(absTargetInput.text)
                    if (!isNaN(target) && viewModel) {
                        viewModel.setAbsTarget(target)
                    }
                }
            }

            // ★ 按钮 2：触发绝对定位
            IndustrialButton {
                text: root.isReadyForTrigger ? "绝对定位 GO" : (
                    viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                )
                isCircle: false
                buttonSize: 110 * Theme.scale
                enabled: root.isReadyForTrigger
                baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                onClicked: {
                    if (!root.isReadyForTrigger) return
                    if (viewModel) {
                        viewModel.triggerAbsMove()
                    }
                }
            }
        }
    }

    // ── ★ 相对定位组 ──
    ColumnLayout {
        Layout.alignment: Qt.AlignHCenter
        spacing: 6 * Theme.scale
        visible: !root.isAbsolute

        // 距离值输入
        TextField {
            id: relTargetInput
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 140 * Theme.scale
            text: "50.0"
            enabled: root.isReadyForSetTarget
            opacity: enabled ? 1.0 : 0.5
            font.pixelSize: Theme.fontLarge
            font.family: "Monospace"
            color: Theme.textMain
            horizontalAlignment: TextInput.AlignHCenter
            background: Rectangle {
                color: Theme.bgDark
                border.color: relTargetInput.activeFocus ? Theme.colorMoving : Theme.borderMain
                border.width: 2
                radius: 6 * Theme.scale
            }
            validator: DoubleValidator { bottom: -9999.9; top: 9999.9; decimals: 2 }
        }

        // 按钮组：设置距离 + 触发移动
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8 * Theme.scale

            // ★ 按钮 1：设置相对距离
            IndustrialButton {
                text: root.isReadyForSetTarget ? "设置距离" : "不可用"
                isCircle: false
                buttonSize: 110 * Theme.scale
                enabled: root.isReadyForSetTarget
                baseColor: root.isReadyForSetTarget ? Theme.panelBg : Theme.colorDisabled
                onClicked: {
                    if (!root.isReadyForSetTarget) return
                    let distance = parseFloat(relTargetInput.text)
                    if (!isNaN(distance) && viewModel) {
                        viewModel.setRelTarget(distance)
                    }
                }
            }

            // ★ 按钮 2：触发相对定位
            IndustrialButton {
                text: root.isReadyForTrigger ? "相对定位 GO" : (
                    viewModel && viewModel.isLoading ? "运行中..." : "不可用"
                )
                isCircle: false
                buttonSize: 110 * Theme.scale
                enabled: root.isReadyForTrigger
                baseColor: root.isReadyForTrigger ? Theme.colorIdle : Theme.colorDisabled
                onClicked: {
                    if (!root.isReadyForTrigger) return
                    if (viewModel) {
                        viewModel.triggerRelMove()
                    }
                }
            }
        }
    }

    // ── ★ Loading 状态指示（可选，调试用）──
    Text {
        Layout.alignment: Qt.AlignHCenter
        text: viewModel ? viewModel.moveStep : ""
        visible: viewModel && viewModel.isLoading
        color: "gray"
        font.pixelSize: Theme.fontSmall
        font.family: "Monospace"
    }

    // 下半弹簧
    Item { Layout.fillHeight: true }
}
```

### 4.4 布局对比：旧 vs 新

| 对比项 | 旧（原方案） | 新（v2） |
|--------|------------|---------|
| 绝对定位输入框 | 1 个 `targetInput` | 1 个 `absTargetInput`（仅绝对组可见时显示） |
| 相对定位输入框 | 共用 `targetInput` | 1 个 `relTargetInput`（仅相对组可见时显示） |
| 执行按钮 | 1 个"执行 GO" → `moveAbsolute()` / `moveRelative()` 统一 | 2 个按钮：`设置目标` → `setAbsTarget()` + `绝对定位 GO` → `triggerAbsMove()` |
| 相对定位按钮 | 复用上述 | 2 个按钮：`设置距离` → `setRelTarget()` + `相对定位 GO` → `triggerRelMove()` |
| Loading 状态 | 无 | `moveStep` 文字指示器（可选） |
| 按钮尺寸 | 140 * Theme.scale | 110 * Theme.scale × 2（总宽 ≈ 原宽度） |

---

## 5. 完整调用链

### 5.1 绝对定位两步操作

```
┌────────────────────────────────────────────────────────────────┐
│ 用户操作 1：输入 150.0 → 点击"设置目标"                          │
│                        ↓                                       │
│ QML: viewModel.setAbsTarget(150.0)                             │
│   → QtAxisViewModel::setAbsTarget(150.0)                       │
│     → AxisViewModelCore::setAbsTarget(150.0)                   │
│       → axis->setAbsTarget(150.0)     // Domain：状态+限位校验   │
│       → pending = SetAbsTargetCommand{150.0}                   │
│                                                                  │
│   下一个 tick() → consumePendingCommands() 自动消费：             │
│       → drv->writeFloat(ABS_TARGET, 150.0)  // ★ 仅写 D 寄存器 │
│                                                                  │
│   ✅ 完成。轴不运动。                                             │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│ 用户操作 2：点击"绝对定位 GO"                                     │
│                        ↓                                       │
│ QML: viewModel.triggerAbsMove()                                │
│   → QtAxisViewModel::triggerAbsMove()                          │
│     → emit loadingChanged()                                    │
│     → AxisViewModelCore::triggerAbsMove()                      │
│       → m_absPolicy->startAbs(m_axisId)  // ★ 不传 target       │
│                                                                  │
│   tick() 每帧驱动 m_absPolicy->tick():                           │
│     EnsuringEnabled → TriggeringMove → WaitingMotionStart       │
│     → WaitingMotionFinish → Disabling → Done                   │
│                                                                  │
│   Policy 到达 Done → isLoading() 返回 false                      │
│   → QML 隐藏 loading 指示器                                      │
└────────────────────────────────────────────────────────────────┘
```

### 5.2 相对定位两步操作

对称于绝对定位，替换为 `setRelTarget()` → `triggerRelMove()`。

---

## 6. 改动文件清单

| 层 | 文件 | 改动类型 | 改动量 |
|----|------|----------|--------|
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.h` | 移除 2 个旧方法声明 + 新增 6 个方法声明 + 2 个 Policy 成员 + 前向声明 | +20 行 / -4 行 |
| **Presentation** | `presentation/viewmodel/AxisViewModelCore.cpp` | 删除 moveAbsolute/moveRelative 实现；新增 setAbsTarget/setRelTarget/triggerAbsMove/triggerRelMove 实现；consumePendingCommands 扩展；tick() Policy 驱动；isLoading/moveStep 实现；移除无用 include | +120 行 / -25 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.h` | 移除 2 个 Q_INVOKABLE 声明 + 新增 4 个 Q_INVOKABLE 声明 + 2 个 Q_PROPERTY + 2 个 signal | +12 行 / -4 行 |
| **Presentation** | `presentation/viewmodel/QtAxisViewModel.cpp` | 删除 moveAbsolute/moveRelative 转发实现；新增 4 个 Q_INVOKABLE 实现 + 2 个属性 getter | +30 行 / -8 行 |
| **QML** | `presentation/qml/blocks/ActionControlBlock.qml` | 替换定位控制面板整段（绝对组 + 相对组独立控件）；新增 isReadyForTrigger / isReadyForSetTarget 属性 | +75 行 / -25 行 |

**总改动量**：约 +237 行新增，-66 行删除，净增约 171 行。

---

## 7. 实施步骤

### 前置条件

- [x] 阶段 1：Domain 层 4 个 Command 已实施
- [x] 阶段 2：Infrastructure 层 4 个寄存器映射已实施
- [x] 阶段 3：TriggerAbsMoveUseCase / TriggerRelMoveUseCase 已新建
- [x] 阶段 4：AbsMovePolicy / RelMovePolicy 已新建

### 阶段 5 实施步骤

```
□ 1. AxisViewModelCore.h 修改：
     - 移除 moveAbsolute / moveRelative 声明
     - 新增 setAbsTarget / triggerAbsMove / setRelTarget / triggerRelMove 声明
     - 新增 isLoading / moveStep 声明
     - 新增 AbsMovePolicy / RelMovePolicy 前向声明
     - 新增 m_absPolicy / m_relPolicy 成员
     - 移除不再需要的 include 声明

□ 2. AxisViewModelCore.cpp 修改：
     - 删除 moveAbsolute / moveRelative 实现
     - 新增 setAbsTarget / setRelTarget / triggerAbsMove / triggerRelMove 实现
     - consumePendingCommands 扩展（isZeroOrVelocity → isSimpleWrite + 2 个新 command）
     - tick() 方法重构（先 consume → Policy tick → 旧编排器 tick）
     - 新增 isLoading / moveStep 实现
     - 构造函数新增 m_absPolicy / m_relPolicy 初始化
     - 移除 m_moveAbsUc / m_moveRelUc 初始化

□ 3. QtAxisViewModel.h 修改：
     - 移除 moveAbsolute / moveRelative Q_INVOKABLE 声明
     - 新增 setAbsTarget / triggerAbsMove / setRelTarget / triggerRelMove Q_INVOKABLE 声明
     - 新增 isLoading / moveStep Q_PROPERTY 声明
     - 新增 loadingChanged / moveStepChanged signal 声明

□ 4. QtAxisViewModel.cpp 修改：
     - 删除 moveAbsolute / moveRelative 转发实现
     - 新增 setAbsTarget / triggerAbsMove / setRelTarget / triggerRelMove 转发实现
     - 新增 isLoading() / moveStep() getter 实现

□ 5. ActionControlBlock.qml 修改：
     - 新增 isReadyForTrigger / isReadyForSetTarget 属性
     - 替换定位控制面板（B 段），将单个输入框+按钮替换为：
       绝对组（absTargetInput + "设置目标"按钮 + "绝对定位 GO"按钮）
       相对组（relTargetInput + "设置距离"按钮 + "相对定位 GO"按钮）
     - 新增 Loading 状态指示文字（可选）

□ 6. 编译验证：
     cd build && cmake --build . && cd .. && tests.exe
```

---

## 8. 回归验证

### 8.1 功能回归 Checklist

| # | 测试场景 | 预期结果 | 通过? |
|---|---------|---------|------|
| 1 | 输入 150 → 点击"设置目标" → 不点 GO | ABS_TARGET 寄存器写入 150，轴不运动，位置不变 | |
| 2 | 点击"设置目标"后 → 点击"绝对定位 GO" | 轴运动到 150 绝对位置，运动完成后 isLoading 变为 false | |
| 3 | 直接点"绝对定位 GO"（未设置目标） | ABS_TARGET = 0 触发运动（或 Policy 报错），不崩溃 | |
| 4 | 输入 -50 → 点击"设置距离" → 点击"相对定位 GO" | 轴相对移动 -50，完成后 isLoading 变为 false | |
| 5 | 运动期间点击"绝对定位 GO" | isReadyForTrigger = false，按钮禁用，不会重复触发 | |
| 6 | 急停后输入目标 → 设置目标 → GO | 按钮禁用（state > Idle），操作被阻止 | |
| 7 | 切换绝对/相对 RadioButton | 输入框和按钮组正确切换显示，值各自独立保留 | |
| 8 | Loading 期间切换绝对/相对 | RadioButton 启用，可切换查看但"设置"按钮禁用 | |
| 9 | 龙门联动轴触发 move | 被龙门锁定的轴按钮禁用（gantryOperationLocked），操作被阻止 | |
| 10 | 错误发生后清除错误 | move 按钮恢复可用 | |

### 8.2 检查要点

- **tick() 调用顺序**：必须先 `consumePendingCommands()`（发送设置类命令），再 `m_absPolicy->tick()`（执行编排），确保 PLC 收到目标值后才触发。
- **Policy 不传参数**：`startAbs(m_axisId)` 无需传 `target`，Policy 内部从 PLC 寄存器读取已有目标值。
- **旧编排器保留**：`m_absOrch` / `m_relOrch` 保留在 tick 循环中但不再有新入口调用，确保向后兼容直至完全确认可移除。
- **setAbsTarget / setRelTarget 模式一致性**：与 `setJogVelocity` / `setMoveVelocity` / `zeroAbsolutePosition` 完全一致（Domain校验 → pending command → consume 发送）。

---

> **文档版本**：v2.0（独立按钮映射版）  
> **上一版本**：v1.0（《MoveCommand耦合问题分析与整改方案》阶段 5 内容 —— 已废弃）  
> **下一阶段**：阶段 6 — 联调 & 端到端测试
