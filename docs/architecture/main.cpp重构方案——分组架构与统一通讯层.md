# main.cpp 重构方案 ---- 分组架构与统一通讯层

> **文档版本**: v2 (更新于 2026-05-18)
>
> **更新说明**: 基于代码库实际状态更新。AxisViewModelCore、Orchestrator、UseCase 均已完成 SystemManager 寻址模式重构，唯 main.cpp 仍为旧代码。

---

## 1. 问题诊断

当前 `main.cpp` 存在以下三个核心问题：

### 问题 1：`axis.applyFeedback(plc.getFeedback())` 接口已淘汰

**现状（main.cpp L93, L98）：**
```cpp
axis.applyFeedback(plc.getFeedback());   // ← 已淘汰接口
// ...
axis.applyFeedback(plc.getFeedback());   // ← tick loop 中再次调用
```

**根因：** 新架构引入了 `ISystemDriver::pollFeedback(SystemContext&)` 统一反馈通路。`FakeAxisDriver::pollFeedback()` 内部已经：

1. 推进 `FakePLC::tick(10)` 一个扫描周期
2. 注入急停反馈 -> `ctx.emergencyStopController().applyFeedback()`
3. 注入龙门反馈 -> `ctx.gantryPowerController().applyFeedback()` + `ctx.gantryCouplingController().applyFeedback()`
4. 遍历全部 6 个轴，逐一调用 `axis->applyFeedback(plc.getFeedback(axisId))`

**主程序不应再绕过 `SystemContext` 直接操作 `Axis` 对象或手动调用 `applyFeedback()`。** 反馈注入已被 `pollFeedback()` 完全封装。

---

### 问题 2：FakePLC 控制通讯层未纳入统一分组管理

**现状（main.cpp L74-L77）：**
```cpp
FakePLC plc;
FakeAxisDriver driver(plc);

Axis axis;                               // ← 独立创建，不在 SystemContext 内
EnableUseCase enableUc(driver);          // ← 旧接口，直接注 driver
JogAxisUseCase jogUc(driver);            // ← 旧接口
MoveAbsoluteUseCase moveAbsUc(driver);   // ← 旧接口
```

**根因：** `SystemIntegrationTest` 已经展示了正确模式：

```
SystemManager
├── "Machine_A" -> SystemContext_A ──绑定──-> FakeAxisDriver(plcA)
│   └── Axis Y, Z, R （由 SystemContext 自动创建）
└── "Machine_B" -> SystemContext_B ──绑定──-> FakeAxisDriver(plcB)
    └── Axis X1, X2  （由 SystemContext 自动创建）
```

所有 UseCase 的 `execute()` 方法接受 `(SystemManager&, groupName, axisId, ...)` 参数，内部通过 `SystemManager::tryGetGroup()` -> `SystemContext::tryGetAxis()` 获取轴对象。**直接注入裸 Driver 到 UseCase 构造器是旧的遗留行为，已不再需要。**

**当前正确的 UseCase 用法（已在 SystemIntegrationTest 中验证）：**
```cpp
EnableUseCase enableUc;           // 默认构造，无依赖（值语义、无状态）
enableUc.execute(manager, "Machine_A", AxisId::Y, true);  // 通过 SystemManager 寻址
```

---

### 问题 3：主程序缺乏多分组创建和切换能力

**现状：** main.cpp 只创建了一个 `Axis` + 一套 UseCase + 一个 `QtAxisViewModel`，硬编码注入到 QML 上下文名为 `"axisX1VM"`。

**需求：** `SystemIntegrationTest` 已经展示了分组的完整生命周期：

```cpp
// 1. 创建分组
manager.createGroup("Machine_A", reason);
manager.createGroup("Machine_B", reason);

// 2. 绑定驱动
ctxA->setDriver(&driverA);
ctxB->setDriver(&driverB);

// 3. 通过分组名 + 轴 ID 执行操作
enableUc.execute(manager, "Machine_A", AxisId::Y, true);
moveAbsUc.execute(manager, "Machine_B", AxisId::X1, 100.0);
```

主程序需要：
- 创建至少两个分组（如 "Machine_A" 和 "Machine_B"）
- 提供分组切换接口（QML 中选择当前活动分组）
- ViewModel 不再硬绑定单个 Axis，而是按分组+轴维度实例化

---

## 2. 重构方案

### 2.1 目标架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                        main.cpp (重构后)                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  SystemManager (全局单例)                                        │
│  ├── "Machine_A" -> SystemContext_A                              │
│  │   ├── Axis Y, Z, R                      ← 6轴自动初始化       │
│  │   ├── GantryCouplingController                               │
│  │   ├── GantryPowerController                                  │
│  │   ├── EmergencyStopController                                │
│  │   └── driver -> FakeAxisDriver(plcA)    ← 绑定                │
│  │                    ↑                                         │
│  │              FakePLC_A                   ← 独立物理态         │
│  │                                                                 │
│  └── "Machine_B" -> SystemContext_B                              │
│      ├── Axis X1, X2                     ← 6轴自动初始化         │
│      ├── GantryCouplingController                               │
│      ├── GantryPowerController                                  │
│      ├── EmergencyStopController                                │
│      └── driver -> FakeAxisDriver(plcB)    ← 绑定                │
│                       ↑                                         │
│                  FakePLC_B                  ← 独立物理态         │
│                                                                  │
│  UseCases (无状态，值语义，通过 manager + groupName 寻址):        │
│      EnableUseCase, MoveAbsoluteUseCase, JogAxisUseCase, …      │
│                                                                  │
│  ViewModels (按 分组+轴 维度实例化):                              │
│      AxisViewModelCore vm_A_Y(mgr, "Machine_A", AxisId::Y)     │
│      AxisViewModelCore vm_A_Z(mgr, "Machine_A", AxisId::Z)     │
│      AxisViewModelCore vm_B_X1(mgr, "Machine_B", AxisId::X1)   │
│      AxisViewModelCore vm_B_X2(mgr, "Machine_B", AxisId::X2)   │
│                                                                  │
│  QML 注入: 按分组名注册上下文属性                                 │
│      "group_A_Y"  -> QtAxisViewModel(&vm_A_Y)                    │
│      "group_A_Z"  -> QtAxisViewModel(&vm_A_Z)                    │
│      "group_B_X1" -> QtAxisViewModel(&vm_B_X1)                   │
│      "group_B_X2" -> QtAxisViewModel(&vm_B_X2)                   │
│                                                                  │
│  全局 Tick Loop (QTimer, 10ms):                                  │
│      for (auto& name : manager.groupNames()) {                  │
│          if (auto* ctx = ...) {                                 │
│              if (auto* drv = ctx->driver()) {                   │
│                  drv->pollFeedback(*ctx); // 统一反馈通路       │
│              }                                                   │
│          }                                                       │
│      }                                                           │
│      for (auto* vm : allViewModels) { vm->tick(); }             │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 已完成的子重构（无需重复实施）

以下重构已在代码库中完成，main.cpp 需要做的仅仅是**使用**它们：

| 子重构 | 状态 | 说明 |
|--------|------|------|
| UseCase 无状态化 | ✅ 已完成 | `EnableUseCase enableUc;` 默认构造即可，execute() 通过 `SystemManager&` 寻址 |
| AxisViewModelCore 适配 SystemManager | ✅ 已完成 | 构造器 `(SystemManager&, groupName, axisId)`，内部动态获取 Axis* |
| Orchestrator 适配 SystemManager | ✅ 已完成 | `JogOrchestrator(SystemManager&, groupName)`，不再直接注 Driver/UseCase |
| FakeAxisDriver::pollFeedback() | ✅ 已完成 | 内部推进 FakePLC + 注入全部反馈到 SystemContext |
| SystemManager 分组管理 | ✅ 已完成 | createGroup / tryGetGroup / removeGroup |
| ContextRejection 枚举 | ✅ 已完成 | GroupAlreadyExists / GroupNotFound / GroupNameInvalid / … |

---

### 2.3 关键变更点

#### 变更 1：移除裸 `Axis` 声明和 `applyFeedback()` 手动调用

**删除：**
```cpp
Axis axis;                                    // ← 删
axis.applyFeedback(plc.getFeedback());        // ← 删
```

**替换为：** `SystemContext` 自动初始化 6 个 Axis，`driver->pollFeedback(*ctx)` 自动注入反馈。

---

#### 变更 2：FakePLC + FakeAxisDriver 纳入 SystemManager 分组架构

**旧代码（main.cpp 当前）：**
```cpp
FakePLC plc;
FakeAxisDriver driver(plc);

Axis axis;
EnableUseCase enableUc(driver);    // ← 旧接口，绕过 SystemContext
JogAxisUseCase jogUc(driver);
// ...
JogOrchestrator jogOrch(enableUc, jogUc);  // ← 旧构造，直接注 UseCase
```

**新代码（应改为）：**
```cpp
// 1. 创建独立硬件仿真实例（每分组一套）
FakePLC plcA, plcB;
FakeAxisDriver driverA(plcA), driverB(plcB);

// 2. 通过 SystemManager 创建分组
SystemManager manager;
ContextRejection reason;
manager.createGroup("Machine_A", reason);
manager.createGroup("Machine_B", reason);

// 3. 绑定驱动到分组
SystemContext* ctxA = nullptr;
SystemContext* ctxB = nullptr;
manager.tryGetGroup("Machine_A", ctxA, reason);
manager.tryGetGroup("Machine_B", ctxB, reason);
ctxA->setDriver(&driverA);
ctxB->setDriver(&driverB);

// 4. UseCases 无状态化（默认构造即可）
EnableUseCase enableUc;       // ✅ 已支持
MoveAbsoluteUseCase moveAbsUc;
JogAxisUseCase jogUc;
StopAxisUseCase stopUc;
// …

// 5. 通过 manager + groupName 执行操作
enableUc.execute(manager, "Machine_A", AxisId::Y, true);
```

---

#### 变更 3：全局 Tick Loop 改为遍历所有分组

**旧代码（main.cpp 当前）：**
```cpp
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    qtVM.tick();
    plc.tick(10);
    axis.applyFeedback(plc.getFeedback());
});
```

**新代码：**
```cpp
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    // 1. 遍历所有分组，统一推进物理引擎 + 注入反馈
    for (const auto& groupName : manager.groupNames()) {
        SystemContext* ctx = nullptr;
        ContextRejection r;
        if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
            if (auto* drv = ctx->driver()) {
                drv->pollFeedback(*ctx);  // ← 统一反馈通路
            }
        }
    }

    // 2. 推进所有 ViewModel 状态机
    for (auto* vm : allViewModels) {
        vm->tick();
    }
});
```

> **注意：** `SystemManager` 需新增 `groupNames()` 方法（或 `allGroups()` 迭代器），返回所有已注册分组名称列表。这是对 `SystemManager` 的轻微扩展。

---

#### 变更 4：ViewModel 按分组+轴维度实例化

**旧代码（main.cpp 当前）：**
```cpp
AxisViewModelCore vmCore(axis, jogOrch, absOrch, relOrch, stopUc);  // ← 旧接口
QtAxisViewModel qtVM(&vmCore);
engine.rootContext()->setContextProperty("axisX1VM", &qtVM);
```

**新代码（AxisViewModelCore 和 Orchestrator 已支持此模式）：**
```cpp
// ⭐ AxisViewModelCore 当前构造器 (已重构完成):
// AxisViewModelCore(SystemManager& manager,
//                   const std::string& groupName,
//                   AxisId axisId);

// Machine_A 的 3 个轴
auto vm_A_Y = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Y);
auto vm_A_Z = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Z);
auto vm_A_R = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::R);

// Machine_B 的 2 个龙门轴
auto vm_B_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X1);
auto vm_B_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X2);

// 包装为 Qt viewmodel
QtAxisViewModel qtVM_A_Y(vm_A_Y.get());
QtAxisViewModel qtVM_A_Z(vm_A_Z.get());
QtAxisViewModel qtVM_B_X1(vm_B_X1.get());
QtAxisViewModel qtVM_B_X2(vm_B_X2.get());

// 注入 QML 上下文
engine.rootContext()->setContextProperty("group_A_Y",  &qtVM_A_Y);
engine.rootContext()->setContextProperty("group_A_Z",  &qtVM_A_Z);
engine.rootContext()->setContextProperty("group_B_X1", &qtVM_B_X1);
engine.rootContext()->setContextProperty("group_B_X2", &qtVM_B_X2);
```

**说明：** `AxisViewModelCore` 已在内部创建所有 UseCase（值语义）和 Orchestrator（`unique_ptr`），调用方只需传入 `SystemManager&` + `groupName` + `axisId`。

---

### 2.4 SystemManager 扩展（轻微，尚未实施）

为支持 tick loop 遍历，`SystemManager` 需新增：

```cpp
// application/SystemManager.h 新增方法

/// @brief 获取所有已注册的分组名称列表
[[nodiscard]]
std::vector<std::string> groupNames() const {
    std::vector<std::string> names;
    names.reserve(m_groups.size());
    for (const auto& [name, _] : m_groups) {
        names.push_back(name);
    }
    return names;
}
```

---

## 3. 实施步骤

| 步骤 | 内容 | 影响范围 | 状态 |
|------|------|----------|------|
| **Step 1** | `SystemManager` 新增 `groupNames()` 方法 | `application/SystemManager.h` | ⬜ 待实施 |
| **Step 2** | 重写 `main.cpp`： | `main.cpp` | ⬜ 待实施 |
| | - 删除裸 `Axis axis;` 声明 | | |
| | - 删除 `axis.applyFeedback(plc.getFeedback())` | | |
| | - 删除 UseCase 构造器的 Driver 注入参数 | | |
| | - 引入 `SystemManager` + 双分组 (Machine_A / Machine_B) | | |
| | - 创建独立 `FakePLC` + `FakeAxisDriver` 对 | | |
| | - 按分组+轴维度创建 `AxisViewModelCore` + `QtAxisViewModel` | | |
| | - 注入到 QML 上下文（按 `group_X_axis` 命名） | | |
| | - Tick loop 改为 `manager.groupNames()` 遍历 + `driver->pollFeedback(*ctx)` | | |
| **Step 3** | 适配 QML 支持分组切换 | `Main.qml` 等 | ⬜ 待实施 |
| **Step 4** | 编译验证 + 运行测试 | `cmake --build build && ctest` | ⬜ 待实施 |

---

## 4. 重构后的 main.cpp 完整代码（伪代码/模板）

```cpp
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTimer>
#include <QUrl>
#include <QQuickStyle>
#include <QStandardPaths>
#include <vector>

#include "application/SystemManager.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/ContextRejection.h"
#include "infrastructure/FakePLC.h"
#include "infrastructure/FakeAxisDriver.h"
#include "presentation/viewmodel/AxisViewModelCore.h"
#include "presentation/viewmodel/QtAxisViewModel.h"
#include "infrastructure/logger/Logger.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    // ============================
    // 0. 日志初始化
    // ============================
    LoggerConfig logCfg;
    logCfg.enableConsole = true;
    logCfg.enableFile = true;
    // ...路径配置...
    Logger::init(logCfg);
    LOG_INFO(LogLayer::APP, "System", "servoV6 Application Starting...");

    QQuickStyle::setStyle("Basic");

    // ============================
    // 1. 硬件仿真层（每个分组独立的 FakePLC + FakeAxisDriver）
    // ============================
    FakePLC plcA, plcB;
    FakeAxisDriver driverA(plcA), driverB(plcB);

    // ============================
    // 2. 系统分组管理
    // ============================
    SystemManager manager;
    ContextRejection reason;

    manager.createGroup("Machine_A", reason);   // Y, Z, R 轴
    manager.createGroup("Machine_B", reason);   // X1, X2 轴（龙门）

    SystemContext* ctxA = nullptr;
    SystemContext* ctxB = nullptr;
    manager.tryGetGroup("Machine_A", ctxA, reason);
    manager.tryGetGroup("Machine_B", ctxB, reason);
    ctxA->setDriver(&driverA);
    ctxB->setDriver(&driverB);

    // ============================
    // 3. 初始化物理世界默认状态
    // ============================
    plcA.forceState(AxisId::Y, AxisState::Disabled);
    plcA.setSimulatedJogVelocity(AxisId::Y, 20.0);
    plcA.setSimulatedMoveVelocity(AxisId::Y, 50.0);
    plcA.setLimits(AxisId::Y, 1000.0, -1000.0);

    plcB.forceState(AxisId::X1, AxisState::Disabled);
    plcB.forceState(AxisId::X2, AxisState::Disabled);
    plcB.setSimulatedJogVelocity(AxisId::X1, 20.0);
    plcB.setSimulatedMoveVelocity(AxisId::X1, 50.0);
    plcB.setLimits(AxisId::X1, 1000.0, -1000.0);
    plcB.setLimits(AxisId::X2, 1000.0, -1000.0);

    // 首次同步（将 plc 默认状态注入 SystemContext）
    driverA.pollFeedback(*ctxA);
    driverB.pollFeedback(*ctxB);

    // ============================
    // 4. ViewModels（按 分组+轴 维度）
    // ============================
    // Machine_A 的轴
    auto vmCore_A_Y  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Y);
    auto vmCore_A_Z  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::Z);
    auto vmCore_A_R  = std::make_unique<AxisViewModelCore>(manager, "Machine_A", AxisId::R);

    // Machine_B 的轴
    auto vmCore_B_X1 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X1);
    auto vmCore_B_X2 = std::make_unique<AxisViewModelCore>(manager, "Machine_B", AxisId::X2);

    // Qt 包装
    QtAxisViewModel qtVM_A_Y(vmCore_A_Y.get());
    QtAxisViewModel qtVM_A_Z(vmCore_A_Z.get());
    QtAxisViewModel qtVM_A_R(vmCore_A_R.get());
    QtAxisViewModel qtVM_B_X1(vmCore_B_X1.get());
    QtAxisViewModel qtVM_B_X2(vmCore_B_X2.get());

    // ============================
    // 5. QML 上下文注入
    // ============================
    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("group_A_Y",  &qtVM_A_Y);
    engine.rootContext()->setContextProperty("group_A_Z",  &qtVM_A_Z);
    engine.rootContext()->setContextProperty("group_A_R",  &qtVM_A_R);
    engine.rootContext()->setContextProperty("group_B_X1", &qtVM_B_X1);
    engine.rootContext()->setContextProperty("group_B_X2", &qtVM_B_X2);

    // 可选: 注入 SystemManager 引用供 QML 分组切换
    // engine.rootContext()->setContextProperty("systemManager", &manager);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
        &app, []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

    engine.loadFromModule("servoV6", "Main");

    // ============================
    // 6. 全局 Tick Loop（统一 pollFeedback）
    // ============================
    std::vector<QtAxisViewModel*> allViewModels = {
        &qtVM_A_Y, &qtVM_A_Z, &qtVM_A_R,
        &qtVM_B_X1, &qtVM_B_X2
    };

    QTimer systemClock;
    QObject::connect(&systemClock, &QTimer::timeout, [&]() {
        // 6a. 所有分组推进物理引擎 + 反馈注入
        for (const auto& groupName : manager.groupNames()) {
            SystemContext* ctx = nullptr;
            ContextRejection r;
            if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
                if (auto* drv = ctx->driver()) {
                    drv->pollFeedback(*ctx);  // ← 统一反馈通路
                }
            }
        }

        // 6b. 所有 ViewModel 推进状态机
        for (auto* vm : allViewModels) {
            vm->tick();
        }
    });
    systemClock.start(10);  // 10ms 物理心跳

    int result = app.exec();

    Logger::shutdown();
    return result;
}
```

---

## 5. 潜在风险与注意事项

| 风险 | 缓解措施 |
|------|----------|
| `AxisViewModelCore` 每次 tick 通过 map 查找 `Axis*` 引入开销 | 当前 10ms 周期、5 个 VM 场景下可忽略。未来若分组/轴数量增长，可在 Core 中缓存 `Axis*`，仅在分组切换或首次获取时通过 manager 查找。 |
| QML 分组切换需要前端配合 | 需设计分组选择器 UI。可在 `Main.qml` 中用 `ComboBox` 选择当前活动分组，ViewModel 暴露 `activeGroup` 属性。 |
| `main.cpp` 中 `AxisViewModelCore` 实例需确保生命周期长于 `QtAxisViewModel` | 用 `unique_ptr` 持有 Core，QtVM 只持裸指针。当前代码已正确处理。 |
| FakePLC 的 `tick(10)` 不应被重复调用 | `FakeAxisDriver::pollFeedback()` 内部已调用一次 `m_plc.tick(10)`，主循环不应再手动调用 `plc.tick()`。 |

---

## 6. 与旧模式的对比总结

| 旧模式（main.cpp 当前） | 新模式（目标） |
|-------------------------|----------------|
| `Axis axis;` 直接声明 | `SystemContext` 内部自动创建 6 轴 |
| `axis.applyFeedback(plc.getFeedback())` | `driver->pollFeedback(*ctx)` |
| UseCase 构造器注 Driver | UseCase 无状态，通过 `SystemManager` 寻址 |
| 一个 ViewModel 硬编码 | 按分组+轴维度创建多个 ViewModel |
| tick loop 手动 `plc.tick()` + `axis.applyFeedback()` | tick loop 遍历分组调用 `pollFeedback()` |
| 无分组概念 | SystemManager -> 多分组隔离 |
| QML 只有一个 `"axisX1VM"` | QML 有 `"group_A_Y"`, `"group_B_X1"` 等多个上下文属性 |

---

## 7. 后续展望

1. **QtAxisViewModel 分组切换** -- 当前每个 VM 绑定一个 (groupName, axisId) 组合。若需动态切换，可增加 `rebind(SystemManager&, groupName, axisId)` 方法。
2. **QML 分组选择器 UI** -- 设计 ComboBox/TabBar 切换活动分组，联动显示对应轴的 ViewModel。
3. **真实硬件驱动** -- 当 FakeAxisDriver 替换为真实 Modbus TCP 驱动时，`main.cpp` 的结构无需改动，仅需替换 `FakeAxisDriver` 为 `RealAxisDriver` 并配置 IP 地址。
