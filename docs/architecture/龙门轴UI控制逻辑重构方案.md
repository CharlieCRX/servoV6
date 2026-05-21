# 龙门轴 UI 控制逻辑重构方案

> **版本**: v1.0
> **日期**: 2026-05-19
> **关联文件**:
> - `domain/gantry/GantryCouplingController.h`
> - `domain/gantry/GantryPowerController.h`
> - `domain/gantry/GantryCouplingState.h`
> - `domain/gantry/GantryFeedback.h`
> - `domain/entity/SystemContext.h`
> - `application/policy/GantryOrchestrator.h`
> - `presentation/viewmodel/EmergencyStopViewModel.h`
> - `presentation/qml/blocks/AxisSelectorBlock.qml`
> - `presentation/qml/blocks/ActionControlBlock.qml`
> - `presentation/qml/blocks/TelemetryBlock.qml`
> - `Main.qml`

---

## 一、现状概述

### 1.1 当前架构已具备的能力

领域层和应用层的龙门控制基础设施已经完善：

| 组件 | 职责 | 状态 |
|------|------|------|
| `GantryCouplingController` | 五态耦合状态机，管理联动/解耦意图与反馈 | ✅ 已完成 |
| `GantryPowerController` | 五态电机使能状态机 | ✅ 已完成 |
| `GantryCouplingState` | 五态内核 | ✅ 已完成 |
| `GantryOrchestrator` | 流程编排：`startCoupling()` / `startDecoupling()` / `tick()` | ✅ 已完成 |
| `SystemContext::tryGetAxis()` | 龙门语义拦截（Coupled→锁X1/X2，Decoupled→锁X） | ✅ 已完成 |

### 1.2 UI 层现状

当前 UI 层**完全没有龙门控制的相关 UI 元素**：

- `AxisSelectorBlock.qml`：X/X1/X2 轴选择仅受 `locked`（急停锁定）控制，没有龙门语义感知
- `TelemetryBlock.qml`：数据看板没有联动使能/解除联动等按钮
- `ActionControlBlock.qml`：点动/定位面板仅检查 `systemLocked` 和 ViewModel 状态，不感知龙门联动状态
- `Main.qml`：`currentViewModel` 动态绑定所有 6 个轴，但没有区分哪些轴在当前龙门状态下可操作

**核心缺失**：UI 层缺少一个**龙门状态 ViewModel**，用于桥接 Domain 层的 `GantryCouplingController` / `GantryPowerController` 状态到 QML。

---

## 二、需求分析——龙门操作状态机设计

### 2.1 完整状态定义

从 UI 视角，龙门轴系统存在以下 **4 个操作状态**：

| 状态 | 使能 | 联动 | X 逻辑轴 | X1/X2 物理轴 | 联动使能按钮 | 解除联动按钮 |
|------|------|------|----------|-------------|-------------|-------------|
| **A: 未使能未联动** | OFF | OFF | 锁定 | 不显示 | ✅ 可用 | ❌ 不可用 |
| **B: 联动使能** | ON | ON | 可控制 | 锁定不显示 | ✅ 可用（反向） | ✅ 可用 |
| **C: 解除联动 & 使能** | ON | OFF | 锁定 | 显示且可控制 | ❌ 不可用 | ✅ 可用（反向） |
| **D: 解除联动 & 未使能** | OFF | OFF | 锁定 | 不显示 | ✅ 可用 | ✅ 可用（反向） |

### 2.2 状态转换图

```text
                    ┌──────────────────────────────────┐
                    │                                  │
                    ▼                                  │
              ┌──────────┐   联动使能按钮(startCoupling)  │
              │  状态 A   │──────────────────────────────┘
              │ 未使能    │
              │ 未联动    │◄─────────────────────────────┐
              │ X: 锁定   │   联动使能按钮反向             │
              │ X1/X2: 隐藏│  (disable + decouple)        │
              └─────┬────┘                               │
                    │                                    │
                    │ 联动使能按钮(startCoupling)          │
                    │ = Enable + Couple                  │
                    ▼                                    │
              ┌──────────┐   解除联动按钮                  │
              │  状态 B   │──(密码验证)──►                │
              │ 使能+联动  │                               │
              │ X: 可控制  │◄──────────────────┐          │
              │ X1/X2: 隐藏│                    │          │
              └──────────┘                     │          │
                                               │          │
                    ┌──────────────────────────┘          │
                    ▼                                    │
              ┌──────────┐   解除联动按钮反向             │
              │  状态 C   │──(关闭使能)──►                │
              │ 使能+解耦  │                               │
              │ X: 锁定   │◄──────────────────┐          │
              │ X1/X2: 可控│                    │          │
              └──────────┘   解除联动按钮       │          │
                           ──(重新使能)──►      │          │
                    ┌──────────────────────────┘          │
                    ▼                                    │
              ┌──────────┐   联动使能按钮                 │
              │  状态 D   │──(startCoupling)─────────────┘
              │ 未使能    │   = Enable + Couple
              │ 解耦      │
              │ X: 锁定   │
              │ X1/X2: 隐藏│
              └──────────┘
```

### 2.3 操作语义详解

#### 2.3.1 联动使能按钮

| 当前状态 | 按下后行为 | 目标状态 |
|---------|-----------|---------|
| A / D | `GantryOrchestrator::startCoupling()` → 使能 + 联动 | B |
| B | 关闭使能 + 解耦（`GantryPowerController::requestEnable(false)` + `GantryCouplingController::requestCouple(false)`） | A |

> **关键问题**：`GantryOrchestrator` 当前没有"一键解除联动+使能"的接口（`startCoupling` 的逆操作）。有两种方案：
> - **方案一（推荐）**：在 `GantryOrchestrator` 新增 `stopCouplingAndDisable()` 方法，编排"解耦 → 等待解耦完成 → 掉电"的完整流程
> - **方案二**：UI 层直接调用 `GantryCouplingController::requestCouple(false)` + `GantryPowerController::requestEnable(false)`
>
> 推荐方案一，因为 `GantryOrchestrator` 已经封装了异步等待逻辑，直接使用底层的 Controller 会丢失编排保证。

#### 2.3.2 解除联动按钮

| 当前状态 | 按下后行为 | 目标状态 |
|---------|-----------|---------|
| B | 密码验证 → 使能保持 + 解耦（`requestEnable(true)` + `requestCouple(false)`） | C |
| C | 关闭使能（`requestEnable(false)`） | D |
| D | 重新使能（`requestEnable(true)`），但保持解耦 | C |

> **关键问题**：从状态 B→C 的操作（使能 + 解耦）在 `GantryOrchestrator` 中也没有现成接口。`startDecoupling()` 只做解耦不做使能保证。需要新增接口或组合调用。

---

## 三、领域层/应用层重构

### 3.1 GantryOrchestrator 新增接口

在 `GantryOrchestrator` 中新增两个编排方法：

```cpp
/**
 * @brief 一键解除联动并关闭使能（startCoupling 的逆操作）
 *
 * 流程：EnsuringDecoupled → WaitingDecoupled → Disabling → WaitingDisabled → Done
 *
 * 对应 UI「联动使能按钮」在状态 B 下的反向操作。
 */
void stopCouplingAndDisable() {
    m_step = Step::Decoupling;
    m_disableAfterDecouple = true;  // 标记：解耦完成后自动掉电
}

/**
 * @brief 使能并解除联动（保持电机使能，仅断联动）
 *
 * 流程：EnsuringEnabled → WaitingEnabled → Decoupling → WaitingDecoupled → Done
 *
 * 对应 UI「解除联动按钮」在状态 B 下的操作（密码验证后）。
 */
void enableAndDecouple() {
    m_step = Step::EnsuringEnabled;
    m_decoupleAfterEnable = true;  // 标记：使能完成后自动解耦
}
```

新增私有成员与 Step：

```cpp
enum class Step {
    // ... 现有 ...
    Disabling,          // 新增：下发龙门电机掉电命令
    WaitingDisabled,    // 新增：等待掉电完成
};

private:
    bool m_disableAfterDecouple = false;  // stopCouplingAndDisable 标记
    bool m_decoupleAfterEnable = false;   // enableAndDecouple 标记
```

`tick()` 中新增分支（在 `WaitingDecoupled` 之后）：

```cpp
case Step::WaitingDecoupled:
    if (!coupling.isDecouplingRequested()) {
        if (m_disableAfterDecouple) {
            // 解耦完成 → 进入掉电流程
            m_disableAfterDecouple = false;
            m_step = Step::Disabling;
        } else {
            m_step = Step::Done;
        }
    }
    break;

case Step::Disabling: {
    auto result = power.requestEnable(false);
    if (result == GantryRejection::None) {
        if (power.hasPendingCommand() && drv) {
            auto commResult = drv->send(power.popPendingCommand());
            if (!commResult.ok()) {
                m_step = Step::Error;
                m_lastError = commResult;
                return;
            }
        }
        m_step = Step::WaitingDisabled;
    } else {
        m_step = Step::Error;
        m_lastError = result;
    }
    break;
}

case Step::WaitingDisabled:
    if (!power.isEnabled()) {
        m_step = Step::Done;
    }
    break;
```

同时在 `WaitingEnabled` 之后增加解耦分支：

```cpp
case Step::WaitingEnabled:
    if (power.isEnabled()) {
        if (m_decoupleAfterEnable) {
            m_decoupleAfterEnable = false;
            m_step = Step::Decoupling;
        } else {
            m_step = Step::Coupling;
        }
    }
    break;
```

### 3.2 新增 GantryViewModel（表现层桥梁）

创建 `presentation/viewmodel/GantryViewModel.h`，作为 UI 与龙门控制器的桥梁：

```cpp
#ifndef GANTRY_VIEW_MODEL_H
#define GANTRY_VIEW_MODEL_H

#include <QObject>
#include <QString>
#include "application/SystemManager.h"
#include "application/policy/GantryOrchestrator.h"
#include "domain/gantry/GantryCouplingController.h"
#include "domain/gantry/GantryPowerController.h"

/**
 * @brief 龙门 ViewModel -- UI ↔ 龙门控制域的桥梁
 *
 * 职责：
 *   1. 向 QML 暴露龙门状态（使能状态、联动状态、Orchestrator 步骤）
 *   2. 接收 UI 指令 → 启动/推进 GantryOrchestrator
 *   3. 每帧 tick() 推进编排器并发射状态变更信号
 *
 * 设计原则：
 *   - 一个实例对应一个分组（Machine_A / Machine_B）
 *   - 封装 GantryOrchestrator 的生命周期
 *   - 密码验证逻辑内置于此 ViewModel
 */
class GantryViewModel : public QObject {
    Q_OBJECT

    // ──────────────── 状态投影 ────────────────
    
    /// @brief 电机是否使能
    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY gantryStateChanged)
    
    /// @brief 是否联动中
    Q_PROPERTY(bool isCoupled READ isCoupled NOTIFY gantryStateChanged)
    
    /// @brief 是否处于解除联动 + 使能状态 (状态 C)
    Q_PROPERTY(bool isDecoupledAndEnabled READ isDecoupledAndEnabled NOTIFY gantryStateChanged)
    
    /// @brief Orchestrator 是否正在运行
    Q_PROPERTY(bool isOrchestratorBusy READ isOrchestratorBusy NOTIFY orchestratorStateChanged)
    
    /// @brief Orchestrator 当前步骤描述
    Q_PROPERTY(QString orchestratorStepText READ orchestratorStepText NOTIFY orchestratorStateChanged)
    
    /// @brief 是否已同步（NotSynchronized 时一切操作不可用）
    Q_PROPERTY(bool isSynchronized READ isSynchronized NOTIFY gantryStateChanged)

public:
    explicit GantryViewModel(SystemManager& manager,
                              const std::string& groupName,
                              QObject* parent = nullptr)
        : QObject(parent)
        , m_manager(manager)
        , m_groupName(groupName)
    {
    }

    // ──────────────── Getters ────────────────

    bool isEnabled() const { return m_cachedEnabled; }
    bool isCoupled() const { return m_cachedCoupled; }
    bool isDecoupledAndEnabled() const {
        // 状态 C：使能 ON + 联动 OFF
        return m_cachedSynchronized && m_cachedEnabled && !m_cachedCoupled;
    }
    bool isOrchestratorBusy() const {
        return m_orchestrator && !m_orchestrator->isDone() && !m_orchestrator->hasError();
    }
    bool isSynchronized() const { return m_cachedSynchronized; }

    QString orchestratorStepText() const { return m_orchestratorStepText; }

    // ──────────────── 操作接口 ────────────────

    /**
     * @brief 联动使能（状态 A/D → B）
     * 调用 GantryOrchestrator::startCoupling()
     */
    Q_INVOKABLE void startCoupling() {
        ensureOrchestrator();
        if (m_orchestrator) {
            m_orchestrator->startCoupling();
        }
    }

    /**
     * @brief 解除联动并去使能（状态 B → A）
     * 调用新接口 stopCouplingAndDisable()
     */
    Q_INVOKABLE void stopCouplingAndDisable() {
        ensureOrchestrator();
        if (m_orchestrator && m_orchestrator->currentStep() == GantryOrchestrator::Step::Initial) {
            m_orchestrator->stopCouplingAndDisable();
        }
    }

    /**
     * @brief 解除联动但保持使能（状态 B → C，需密码验证）
     * 调用新接口 enableAndDecouple()
     */
    Q_INVOKABLE void enableAndDecouple() {
        ensureOrchestrator();
        if (m_orchestrator && m_orchestrator->currentStep() == GantryOrchestrator::Step::Initial) {
            m_orchestrator->enableAndDecouple();
        }
    }

    /**
     * @brief 关闭使能（状态 C → D）
     * 直接调用 GantryPowerController
     */
    Q_INVOKABLE void disable() {
        SystemContext* ctx = getContext();
        if (ctx) {
            ctx->gantryPowerController().requestEnable(false);
        }
    }

    /**
     * @brief 重新使能（状态 D → C）
     * 直接调用 GantryPowerController
     */
    Q_INVOKABLE void enable() {
        SystemContext* ctx = getContext();
        if (ctx) {
            ctx->gantryPowerController().requestEnable(true);
        }
    }

    /**
     * @brief 密码验证
     * @param password 用户输入的密码
     * @return true 验证通过
     */
    Q_INVOKABLE bool verifyPassword(const QString& password) const {
        return password == QStringLiteral("123456");
    }

    // ──────────────── 帧驱动 ────────────────

    void tick() {
        SystemContext* ctx = getContext();
        if (!ctx) return;

        auto& power = ctx->gantryPowerController();
        auto& coupling = ctx->gantryCouplingController();

        bool newSynchronized = power.isSynchronized() && !coupling.isNotSynchronized();
        bool newEnabled = power.isEnabled();
        bool newCoupled = coupling.isCoupled();

        bool changed =
            (newSynchronized != m_cachedSynchronized) ||
            (newEnabled != m_cachedEnabled) ||
            (newCoupled != m_cachedCoupled);

        if (changed) {
            m_cachedSynchronized = newSynchronized;
            m_cachedEnabled = newEnabled;
            m_cachedCoupled = newCoupled;
            emit gantryStateChanged();
        }

        // 推进编排器
        if (m_orchestrator && m_orchestrator->currentStep() != GantryOrchestrator::Step::Done
            && m_orchestrator->currentStep() != GantryOrchestrator::Step::Error) {
            m_orchestrator->tick();
            QString newStepText = stepToString(m_orchestrator->currentStep());
            if (newStepText != m_orchestratorStepText) {
                m_orchestratorStepText = newStepText;
                emit orchestratorStateChanged();
            }
            if (m_orchestrator->isDone() || m_orchestrator->hasError()) {
                emit orchestratorStateChanged();
            }
        }
    }

signals:
    void gantryStateChanged();
    void orchestratorStateChanged();

private:
    SystemContext* getContext() {
        SystemContext* ctx = nullptr;
        ContextRejection reason = ContextRejection::None;
        m_manager.tryGetGroup(m_groupName, ctx, reason);
        return ctx;
    }

    void ensureOrchestrator() {
        if (!m_orchestrator) {
            m_orchestrator = std::make_unique<GantryOrchestrator>(m_manager, m_groupName);
        }
    }

    static QString stepToString(GantryOrchestrator::Step step) {
        switch (step) {
        case GantryOrchestrator::Step::Initial:           return QStringLiteral("就绪");
        case GantryOrchestrator::Step::EnsuringEnabled:   return QStringLiteral("使能中...");
        case GantryOrchestrator::Step::WaitingEnabled:    return QStringLiteral("等待使能...");
        case GantryOrchestrator::Step::Coupling:          return QStringLiteral("联动中...");
        case GantryOrchestrator::Step::WaitingCoupled:    return QStringLiteral("等待联动...");
        case GantryOrchestrator::Step::Decoupling:        return QStringLiteral("解耦中...");
        case GantryOrchestrator::Step::WaitingDecoupled:  return QStringLiteral("等待解耦...");
        case GantryOrchestrator::Step::Disabling:         return QStringLiteral("掉电中...");
        case GantryOrchestrator::Step::WaitingDisabled:   return QStringLiteral("等待掉电...");
        case GantryOrchestrator::Step::Done:              return QStringLiteral("完成");
        case GantryOrchestrator::Step::Error:             return QStringLiteral("错误");
        default:                                          return QStringLiteral("未知");
        }
    }

    SystemManager& m_manager;
    std::string m_groupName;
    std::unique_ptr<GantryOrchestrator> m_orchestrator;

    bool m_cachedSynchronized = false;
    bool m_cachedEnabled = false;
    bool m_cachedCoupled = false;
    QString m_orchestratorStepText;
};

#endif // GANTRY_VIEW_MODEL_H
```

---

## 四、UI 层重构

### 4.1 Main.qml — 新增 GantryViewModel 绑定

在 `Main.qml` 中新增 `GantryViewModel` 属性绑定，并传递给各个子面板：

```qml
// Main.qml 新增
property var currentGantryViewModel: {
    if (currentGroup === "Machine_A") return gantryVM_A;
    if (currentGroup === "Machine_B") return gantryVM_B;
    return gantryVM_A;
}

// 在 ColumnLayout 中将 gantryViewModel 传递给 TelemetryBlock 和 AxisSelectorBlock
AxisSelectorBlock {
    // ... 现有属性 ...
    gantryViewModel: currentGantryViewModel
}

TelemetryBlock {
    // ... 现有属性 ...
    gantryViewModel: currentGantryViewModel
}

ActionControlBlock {
    // ... 现有属性 ...
    currentAxis: currentAxis       // 新增：需要知道当前选中的轴
    gantryViewModel: currentGantryViewModel
}
```

### 4.2 AxisSelectorBlock.qml — 龙门轴显示逻辑

**核心变更**：轴列表项需要根据龙门状态动态控制可见性和可操作性。

```qml
// AxisSelectorBlock.qml 中新增属性
property var gantryViewModel: null

// 龙门相关派生属性
readonly property bool isGantryDecoupledAndEnabled: 
    gantryViewModel && gantryViewModel.isDecoupledAndEnabled

// --- X 轴（逻辑龙门轴）---
AxisItemDelegate {
    name: "X 轴 (龙门逻辑)"
    isActive: root.currentAxisName === "X"
    visible: gantryViewModel ? gantryViewModel.isCoupled : true  
    // 只在联动状态下可见（Coupled）
    enabled: !root.locked 
             && gantryViewModel 
             && gantryViewModel.isCoupled
             && !gantryViewModel.isOrchestratorBusy
    // ... onClicked 不变 ...
}

// --- X1 轴（物理龙门轴1）---
AxisItemDelegate {
    name: "X1 轴 (物理)"
    isActive: root.currentAxisName === "X1"
    visible: gantryViewModel ? gantryViewModel.isDecoupledAndEnabled : false
    // 只在解除联动+使能状态下可见（状态 C）
    enabled: !root.locked && gantryViewModel && gantryViewModel.isDecoupledAndEnabled
    // ... onClicked 不变 ...
}

// --- X2 轴（物理龙门轴2）---
AxisItemDelegate {
    name: "X2 轴 (物理)"
    isActive: root.currentAxisName === "X2"
    visible: gantryViewModel ? gantryViewModel.isDecoupledAndEnabled : false
    enabled: !root.locked && gantryViewModel && gantryViewModel.isDecoupledAndEnabled
    // ... onClicked 不变 ...
}
```

### 4.3 TelemetryBlock.qml — 新增龙门控制区

在数据看板中，当选中 X 龙门轴时，显示龙门专用控制按钮：

```qml
// TelemetryBlock.qml 中新增属性
property var gantryViewModel: null
property string selectedAxis: ""  // 从外部传入当前选中的轴名

// 在内容区（使能状态行下方）新增龙门控制区
// 仅当选中 X 轴时显示
Rectangle {
    Layout.fillWidth: true
    visible: root.selectedAxis === "X" && root.gantryViewModel
    height: visible ? 80 * Theme.scale : 0
    color: "transparent"

    RowLayout {
        anchors.fill: parent
        spacing: 10 * Theme.scale

        // ===== 联动使能按钮 =====
        IndustrialButton {
            text: {
                if (!gantryViewModel) return "联动使能"
                if (gantryViewModel.isOrchestratorBusy) return gantryViewModel.orchestratorStepText
                if (gantryViewModel.isCoupled) return "⛔ 解除联动使能"
                return "🔗 联动使能"
            }
            buttonSize: 120 * Theme.scale
            enabled: gantryViewModel 
                     && !gantryViewModel.isOrchestratorBusy
                     && (gantryViewModel.isCoupled || !gantryViewModel.isDecoupledAndEnabled)
                     && gantryViewModel.isSynchronized
            baseColor: {
                if (!gantryViewModel) return Theme.colorDisabled
                if (gantryViewModel.isCoupled) return "#FF5252"  // 红色（反向操作）
                return Theme.colorIdle
            }
            onClicked: {
                if (!gantryViewModel) return
                if (gantryViewModel.isCoupled) {
                    // 状态 B → A：解除联动 + 去使能
                    gantryViewModel.stopCouplingAndDisable()
                } else {
                    // 状态 A/D → B：联动使能
                    gantryViewModel.startCoupling()
                }
            }
        }

        // ===== 解除联动按钮（仅用图标符号）=====
        IndustrialButton {
            text: "🔓"  // 解锁符号，不用文字
            buttonSize: 40 * Theme.scale
            isCircle: true
            visible: gantryViewModel && gantryViewModel.isCoupled && !gantryViewModel.isOrchestratorBusy
            enabled: visible
            baseColor: Theme.colorWarning
            onClicked: {
                // 弹出密码验证对话框
                passwordDialog.open()
            }
        }
    }

    // 密码验证弹窗
    Dialog {
        id: passwordDialog
        title: "解除联动验证"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            spacing: 8 * Theme.scale
            Label {
                text: "请输入解除联动密码"
                font: Theme.fontBody
                color: Theme.colorTextPrimary
            }
            TextField {
                id: passwordInput
                echoMode: TextInput.Password
                Layout.fillWidth: true
                placeholderText: "请输入密码"
            }
        }
        onAccepted: {
            if (gantryViewModel && gantryViewModel.verifyPassword(passwordInput.text)) {
                // 验证通过 → 执行 B → C：使能 + 解耦
                gantryViewModel.enableAndDecouple()
            } else {
                // 密码错误提示
                passwordErrorDialog.open()
            }
            passwordInput.text = ""
        }
        onRejected: {
            passwordInput.text = ""
        }
    }

    // 密码错误提示
    Dialog {
        id: passwordErrorDialog
        title: "错误"
        modal: true
        standardButtons: Dialog.Ok
        Label {
            text: "密码错误，解除联动操作已取消"
            color: Theme.colorError
        }
    }
}
```

### 4.4 ActionControlBlock.qml — 联动状态下操作锁定

**核心变更**：在"联动 + 使能"（状态 B）下，X 逻辑轴可正常控制；在"解除联动+使能"（状态 C）下，X1/X2 物理轴可正常单轴控制。其余状态下 X/X1/X2 的所有操作均被锁定。

```qml
// ActionControlBlock.qml 中新增属性
property var gantryViewModel: null
property string currentAxis: ""  // 从外部传入当前选中的轴名

// 派生属性：当前轴是否被龙门锁定
readonly property bool isGantryLocked: {
    if (!gantryViewModel) return false
    
    var axis = root.currentAxis
    var isXAxis = (axis === "X")
    var isX1X2Axis = (axis === "X1" || axis === "X2")
    
    if (isXAxis) {
        // X 逻辑轴：只在联动状态下可操作（状态 B）
        return !gantryViewModel.isCoupled
    }
    if (isX1X2Axis) {
        // X1/X2 物理轴：只在解除联动+使能状态下可操作（状态 C）
        return !gantryViewModel.isDecoupledAndEnabled
    }
    // 非龙门轴（Y/Z）：不受龙门限制
    return false
}

// 原有操作面板的 enabled 条件增加 isGantryLocked 检查
// 例如：
IndustrialButton {
    id: jogPositiveBtn
    text: "正转"
    enabled: !root.systemLocked 
             && !root.isGantryLocked
             && !root.isOrchestratorBusy   // 编排器执行中也不允许操作
             && root.viewModel 
             && root.viewModel.isEnabled
    // ... 其余不变 ...
}

// 同理应用于：
// - jogNegativeBtn（反转）
// - stopBtn（停止）
// - ZeroAbsolutePosition 按钮
// - setRelativeZero 按钮
// - clearRelativeZero 按钮
// - moveAbsolute 按钮
// - moveRelative 按钮
```

**重要说明**：`isGantryLocked` 仅影响 UI 层的按钮启用状态（enabled 属性），是**前端防御**。`SystemContext::tryGetAxis()` 已在 Domain 层根据 `PhysicalAxisLockedByGantry` / `LogicalAxisUnavailableWhenDecoupled` 做后端拦截，双重保险。

### 4.5 状态 C/D 下解除联动按钮的行为切换

解除联动按钮在状态 C 和 D 下分别执行不同操作：

```qml
// 解除联动按钮行为更新（在 TelemetryBlock.qml 中）
IndustrialButton {
    id: decoupleBtn
    text: "🔓"
    buttonSize: 40 * Theme.scale
    isCircle: true
    visible: gantryViewModel 
             && !gantryViewModel.isOrchestratorBusy
             && gantryViewModel.isSynchronized
             && (gantryViewModel.isCoupled || gantryViewModel.isDecoupledAndEnabled)
    enabled: visible
    baseColor: {
        if (gantryViewModel && gantryViewModel.isDecoupledAndEnabled) {
            return "#FF5252"  // 状态 C → D：关闭使能（红色警示）
        }
        return Theme.colorWarning
    }
    onClicked: {
        if (!gantryViewModel) return
        
        if (gantryViewModel.isCoupled) {
            // 状态 B → C：需要密码验证
            passwordDialog.open()
        } else if (gantryViewModel.isDecoupledAndEnabled) {
            // 状态 C → D：关闭使能（无需密码）
            gantryViewModel.disable()
        }
        // 注意：状态 D → C 由联动使能按钮通过 startCoupling 路径实现
        //      这里不需要单独处理
    }
}
```

---

## 五、实施步骤

### 阶段 1：应用层接口扩展

| 步骤 | 文件 | 变更内容 |
|------|------|---------|
| 1 | `application/policy/GantryOrchestrator.h` | 新增 `Step::Disabling` / `Step::WaitingDisabled` 枚举 |
| 2 | `application/policy/GantryOrchestrator.h` | 新增 `stopCouplingAndDisable()` 方法 |
| 3 | `application/policy/GantryOrchestrator.h` | 新增 `enableAndDecouple()` 方法 |
| 4 | `application/policy/GantryOrchestrator.h` | `tick()` 中新增 `Disabling` / `WaitingDisabled` 分支 |
| 5 | `application/policy/GantryOrchestrator.h` | `WaitingEnabled` 后增加 `m_decoupleAfterEnable` 条件分支 |
| 6 | `application/policy/GantryOrchestrator.h` | 新增私有成员 `m_disableAfterDecouple` / `m_decoupleAfterEnable` |
| 7 | `tests/application/policy/test_gantry_orchestrator.cpp` | 新增 `stopCouplingAndDisable` 和 `enableAndDecouple` 的测试用例 |

### 阶段 2：表现层 ViewModel 创建

| 步骤 | 文件 | 变更内容 |
|------|------|---------|
| 8 | `presentation/viewmodel/GantryViewModel.h` | **新建文件**，实现完整 GantryViewModel（参考第三节完整代码） |
| 9 | `presentation/CMakeLists.txt` | 添加 `GantryViewModel.h`（Header-only，无需编译） |
| 10 | `tests/presentation/viewmodel/` | 新增 `test_gantry_viewmodel.cpp`（验证状态投影和密码逻辑） |

### 阶段 3：QML UI 集成

| 步骤 | 文件 | 变更内容 |
|------|------|---------|
| 11 | `Main.qml` | 创建 `GantryViewModel` 实例（`gantryVM_A` / `gantryVM_B`），绑定到分组 |
| 12 | `Main.qml` | `currentGantryViewModel` 属性根据 `currentGroup` 动态选择 |
| 13 | `Main.qml` | 将 `gantryViewModel` 属性传递到 `AxisSelectorBlock`、`TelemetryBlock`、`ActionControlBlock` |
| 14 | `AxisSelectorBlock.qml` | 新增 `gantryViewModel` 属性 + X/X1/X2 的 visible/enabled 龙门语义逻辑 |
| 15 | `TelemetryBlock.qml` | 新增 `gantryViewModel` / `selectedAxis` 属性 + 联动使能按钮 + 解除联动按钮 + 密码弹窗 |
| 16 | `ActionControlBlock.qml` | 新增 `gantryViewModel` / `currentAxis` 属性 + `isGantryLocked` 派生属性 |
| 17 | `ActionControlBlock.qml` | 所有操作按钮的 `enabled` 条件增加 `isGantryLocked` 检查 |

### 阶段 4：主循环集成

| 步骤 | 文件 | 变更内容 |
|------|------|---------|
| 18 | `main.cpp`（或主 tick loop 调用处） | 在每帧 tick 循环中调用 `gantryVM_A.tick()` / `gantryVM_B.tick()` |

### 阶段 5：测试验证

| 步骤 | 测试内容 |
|------|---------|
| 19 | `test_gantry_orchestrator.cpp`：验证 `stopCouplingAndDisable()` 流程（解耦→掉电→Done） |
| 20 | `test_gantry_orchestrator.cpp`：验证 `enableAndDecouple()` 流程（使能→解耦→Done） |
| 21 | `test_gantry_viewmodel.cpp`：验证 `isDecoupledAndEnabled` 属性逻辑正确 |
| 22 | `test_gantry_viewmodel.cpp`：验证密码验证（正确密码 / 错误密码） |
| 23 | 手动 UI 测试：状态 A→B→C→D→A 的完整状态机转换 |
| 24 | 手动 UI 测试：状态 B 下 X1/X2 不可见且不可操作 |
| 25 | 手动 UI 测试：状态 C 下 X 逻辑轴锁定、X1/X2 可独立点动 |

---

## 六、状态锁定矩阵总结

| 操作 | 状态 A（OFF+OFF） | 状态 B（ON+ON） | 状态 C（ON+OFF） | 状态 D（OFF+OFF） |
|------|-----------------|----------------|-----------------|-----------------|
| X 逻辑轴 - 点动 | ❌ | ✅ | ❌ | ❌ |
| X 逻辑轴 - 定位 | ❌ | ✅ | ❌ | ❌ |
| X 逻辑轴 - 设零 | ❌ | ✅ | ❌ | ❌ |
| X 逻辑轴 - 使能 | ❌ | - | ❌ | ❌ |
| X1 物理轴 - 点动 | ❌ | ❌ | ✅ | ❌ |
| X1 物理轴 - 定位 | ❌ | ❌ | ✅ | ❌ |
| X1 物理轴 - 设零 | ❌ | ❌ | ✅ | ❌ |
| X2 物理轴 - 点动 | ❌ | ❌ | ✅ | ❌ |
| X2 物理轴 - 定位 | ❌ | ❌ | ✅ | ❌ |
| X2 物理轴 - 设零 | ❌ | ❌ | ✅ | ❌ |
| 联动使能按钮 | ✅ | ✅ (反向) | ❌ | ✅ |
| 解除联动按钮 | ❌ | ✅ (密码) | ✅ (反向) | ❌ |

---

## 七、关键设计决策

### 7.1 为什么新增 GantryViewModel 而非扩展 QtAxisViewModel

| 方案 | 优点 | 缺点 |
|------|------|------|
| **扩展 QtAxisViewModel** | 减少类数量 | 违反单一职责原则；每个轴实例都会携带龙门状态；X/X1/X2 三个 ViewModel 的状态需要同步 |
| **新增 GantryViewModel** ✅ | 职责清晰；龙门状态集中管理；不污染轴 ViewModel | 多一个类 |

选择新增 GantryViewModel，因为龙门是**分组级**概念（对应 `SystemContext`），不是轴级概念。

### 7.2 为什么 stopCouplingAndDisable 需要编排器而非直接调 Controller

- `GantryOrchestrator` 的设计理念是**保证异步操作的正确时序**：解耦 PLC 命令 → 等待反馈 → 掉电 PLC 命令 → 等待反馈
- 直接调用 `GantryCouplingController::requestCouple(false)` + `GantryPowerController::requestEnable(false)` 会同时下发两条命令，PLC 可能处理顺序不确定
- 编排器通过 `pendingCommand` 机制确保一条命令发送并收到反馈后，才发送下一条

### 7.3 密码验证为何放在 GantryViewModel 而非独立安全模块

- 当前需求是**简单的现场级操作密码**（默认 123456），用于防止误操作
- 不属于系统安全认证范畴（与紧急急停的安全等级不同）
- 置于 ViewModel 中保持实现简洁，后续如需升级为权限系统可抽取为 `AuthService`

### 7.4 X/X1/X2 轴的可见性切换而非单纯禁用

- **可见性（visible）**：状态 A/B 下 X1/X2 直接隐藏，状态 C/D 下 X 逻辑轴隐藏 → 减少用户困惑
- **可操作性（enabled）**：即使轴可见也可能在某些过渡状态下禁用 → 防止竞态操作
- 两者配合使用，提供清晰的 UX 引导

---

## 八、与现有架构的兼容性

| 组件 | 影响 |
|------|------|
| `GantryCouplingController` | **零改动** —— 仅通过编排器间接调用 |
| `GantryPowerController` | **零改动** —— 同上 |
| `GantryCouplingState` | **零改动** |
| `SystemContext::tryGetAxis()` | **零改动** —— 后端拦截逻辑不变 |
| `QtAxisViewModel` | **零改动** —— 不对现有轴 ViewModel 做任何修改 |
| `EmergencyStopViewModel` | **零改动** —— 急停逻辑独立 |
| `FakePLC` | **零改动** —— 龙门控制由 Domain 层封装 |

所有改动集中在三个层面：
1. **应用层**：`GantryOrchestrator` 新增两个编排方法
2. **表现层**：新增 `GantryViewModel`
3. **UI 层**：QML 文件增加龙门状态感知的 visible/enabled 逻辑和龙门控制按钮
