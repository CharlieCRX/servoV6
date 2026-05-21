# servoV6 项目架构说明文档

## 1. 项目概述

**servoV6** 是一个基于 **C++20 / Qt6 / QML** 构建的伺服电机控制系统，采用 **Clean Architecture（整洁架构）** 分层设计，实现对多轴（Y/Z/R/X/X1/X2）运动控制的业务建模、指令编排、硬件仿真与图形化人机交互。

### 核心技术栈

| 技术 | 版本/说明 |
|------|----------|
| 语言标准 | C++20 |
| 构建系统 | CMake ≥ 3.21 |
| UI 框架 | Qt6 (Qml, Quick, QuickControls2) |
| 测试框架 | GoogleTest |
| 版本控制 | Git |

### 项目仓库

```
origin:  git@github.com:CharlieCRX/servoV6.git
work:    git@10.1.2.222:buddy/servov6.git
```

---

## 2. 架构分层总览

项目严格遵循 **依赖方向由外向内** 的整洁架构原则，划分为以下四层：

```
┌──────────────────────────────────────────────────────────────┐
│                    presentation (表现层)                       │
│              QML UI / ViewModel / 数据绑定                     │
├──────────────────────────────────────────────────────────────┤
│                    application (应用层)                        │
│          UseCases(用例) / Orchestrator(编排器) / Policy(策略)  │
├──────────────────────────────────────────────────────────────┤
│                      domain (领域层)                           │
│              Axis 实体 / 状态机 / 领域规则 / 命令意图            │
├──────────────────────────────────────────────────────────────┤
│                  infrastructure (基础设施层)                    │
│          FakeAxisDriver / FakePLC / Logger / 硬件抽象          │
└──────────────────────────────────────────────────────────────┘
```

**依赖规则**：外层依赖内层，内层绝不依赖外层。`domain` 层是纯 C++ 无任何框架依赖；`application` 层依赖 `domain`；`infrastructure` 层实现 `application` 层定义的抽象接口；`presentation` 层依赖 `application` 层获取可绑定的 ViewModel。

---

## 3. 目录结构

```
servoV6/
├── CMakeLists.txt                    # 顶层构建配置
├── main.cpp                          # 应用入口
├── Main.qml                          # QML 主入口
│
├── domain/                           # ====== 领域层 ======
│   ├── CMakeLists.txt
│   ├── entity/
│   │   ├── Axis.h / .cpp             # 轴领域实体（核心聚合根）
│   │   ├── AxisId.h                  # 轴标识枚举
│   │   └── SystemContext.h           # 分组上下文（轴容器 + 龙门状态）
│   └── command/                      # 预留：命令对象目录
│
├── application/                      # ====== 应用层 ======
│   ├── CMakeLists.txt
│   ├── axis/
│   │   ├── IAxisDriver.h             # 驱动抽象接口
│   │   ├── AxisRepository.h          # 轴仓库（注册/查找）
│   │   ├── AxisSyncService.h         # 轴同步服务
│   │   ├── EnableUseCase.h           # 使能/掉电用例
│   │   ├── JogAxisUseCase.h          # 点动用例
│   │   ├── MoveAbsoluteUseCase.h     # 绝对定位用例
│   │   ├── MoveRelativeUseCase.h     # 相对定位用例
│   │   └── StopAxisUseCase.h         # 急停用例
│   └── policy/
│       ├── JogOrchestrator.h         # 点动编排器（状态机）
│       ├── AutoAbsMoveOrchestrator.h # 绝对定位编排器（状态机）
│       └── AutoRelMoveOrchestrator.h # 相对定位编排器（状态机）
│
├── infrastructure/                   # ====== 基础设施层 ======
│   ├── FakeAxisDriver.h              # 模拟驱动实现
│   ├── FakePLC.h                     # 模拟 PLC（物理引擎）
│   ├── logger/
│   │   ├── Logger.h                  # 日志宏定义
│   │   ├── LogContext.h              # 日志上下文
│   │   └── TraceScope.h              # 链路追踪作用域
│   └── utils/
│       └── CommandFormatter.h        # 命令格式化工具
│
├── presentation/                     # ====== 表现层 ======
│   ├── CMakeLists.txt
│   ├── viewmodel/
│   │   ├── AxisViewModelCore.h/.cpp  # ViewModel 纯逻辑核心
│   │   └── QtAxisViewModel.h/.cpp    # Qt 属性绑定适配层
│   └── qml/
│       ├── core/
│       │   └── Theme.qml             # 全局主题
│       ├── components/
│       │   ├── IndustrialButton.qml  # 工业风格按钮
│       │   ├── AxisItemDelegate.qml  # 轴列表项代理
│       │   └── VelocitySettingsPopup.qml # 速度设置弹窗
│       ├── blocks/
│       │   ├── AxisSelectorBlock.qml # 轴选择器区块
│       │   ├── ActionControlBlock.qml# 动作控制区块
│       │   └── TelemetryBlock.qml    # 遥测数据显示区块
│       └── views/
│           └── MainDashboard.qml     # 主仪表盘视图
│
├── tests/                            # ====== 测试层 ======
│   ├── CMakeLists.txt
│   ├── domain/
│   │   ├── test_axis.cpp
│   │   └── test_system_context.cpp
│   ├── application/
│   │   ├── test_axis_repository.cpp
│   │   ├── test_enable_usecase.cpp
│   │   ├── test_jog_usecase.cpp
│   │   ├── test_move_absolute_usecase.cpp
│   │   ├── test_move_relative_usecase.cpp
│   │   ├── test_stop_usecase.cpp
│   │   └── policy/
│   │       ├── test_jog_orchestrator.cpp
│   │       ├── test_auto_abs_move_orchestrator.cpp
│   │       └── test_auto_rel_move_orchestrator.cpp
│   ├── infrastructure/
│   │   ├── test_fake_plc.cpp
│   │   └── test_system_integration.cpp
│   └── presentation/
│       └── viewmodel/
│           └── test_axis_viewmodel_core.cpp
│
├── external/
│   └── googletest/                   # GoogleTest 测试框架
│
└── build/                            # 构建输出目录
```

---

## 4. 领域层 (domain)

### 4.1 职责

领域层是系统的核心，封装了所有与伺服轴控制相关的 **业务规则** 和 **领域模型**。该层 **不依赖任何框架或外部库**，是纯 C++ 代码。

### 4.2 核心实体

#### 4.2.1 Axis（轴聚合根）

`Axis` 是整个系统的核心领域实体，封装了单轴的完整生命周期与行为：

- **状态机**：`AxisState` 枚举定义了轴的 7 种状态
  ```
  Unknown -> Disabled -> Idle -> Jogging / MovingAbsolute / MovingRelative -> Error
  ```

- **命令意图系统**：使用 `std::variant` 实现统一的类型安全命令槽位 `AxisCommand`，支持：
  - `JogCommand` -- 点动指令（方向 + 启停标记）
  - `MoveCommand` -- 定位指令（绝对/相对 + 目标值）
  - `StopCommand` -- 急停指令
  - `EnableCommand` -- 使能/掉电指令
  - `SetJogVelocityCommand` / `SetMoveVelocityCommand` -- 速度配置
  - `ZeroAbsoluteCommand` -- 绝对零点归零
  - `SetRelativeZeroCommand` / `ClearRelativeZeroCommand` -- 相对零点管理

- **反馈镜像**：通过 `applyFeedback()` 方法接收 PLC 反馈数据 `AxisFeedback`，刷新内部位置、限位、速度等状态。

- **领域规则**：
  - 软限位预检：`TargetOutOfPositiveLimit` / `TargetOutOfNegativeLimit`
  - 边界状态拦截：`AtPositiveLimit` / `AtNegativeLimit`
  - 状态合法性校验：`InvalidState`、`AlreadyMoving`
  - 拒绝原因枚举：`RejectionReason` 提供幂等性保证

- **关键设计**：Axis **不感知分组概念**，不知道自己属于哪个 SystemContext。轴身份的互斥由上层 Policy 层负责。

#### 4.2.2 AxisId（轴标识）

```cpp
enum class AxisId { Y, Z, R, X, X1, X2 };
```

定义了系统中 6 个轴的唯一标识。其中 `X` 为龙门逻辑轴，`X1`/`X2` 为龙门物理轴。

#### 4.2.3 SystemContext（分组上下文）

`SystemContext` 是分组运行的载体，职责包括：

- **轴注册与管理**：通过 `registerAxis()` / `getAxis()` 管理该分组下所有 Axis 实例
- **驱动绑定**：通过 `setDriver()` 绑定 `IAxisDriver*`，将命令意图发送到物理层
- **龙门全局状态**：`isGantryCoupled()` / `setGantryCoupled()` 管理龙门联动/解耦模式

**关键约束**：
1. 不同 SystemContext 之间绝对隔离（平行宇宙模式）
2. 龙门状态属于 SystemContext 的全局状态，不属于任何单个 Axis
3. Axis 在 AxisRepository 中平级注册，身份互斥由编排层 Policy 负责

---

## 5. 应用层 (application)

### 5.1 职责

应用层定义了系统的 **用例（Use Cases）** 和 **编排策略（Orchestrator/Policy）**。它不包含业务规则，而是协调领域实体完成特定业务场景。

### 5.2 抽象接口

#### IAxisDriver（驱动抽象）

```cpp
class IAxisDriver {
public:
    virtual ~IAxisDriver() = default;
    virtual void send(AxisId id, const AxisCommand& cmd) = 0;
};
```

定义了命令下发的统一抽象接口。`infrastructure` 层的 `FakeAxisDriver` 实现此接口，实现了依赖反转（DIP）。

### 5.3 轴仓库 (AxisRepository)

`AxisRepository` 提供轴的注册与查找功能，是应用层管理轴实例的容器。与 `SystemContext` 的区别在于：`SystemContext` 面向"分组"概念且包含龙门状态，而 `AxisRepository` 是纯粹的轴注册表。

### 5.4 用例层 (Use Cases)

每个用例遵循 **单一职责原则**，只做一件事：

| 用例 | 职责 | 核心流程 |
|------|------|---------|
| `EnableUseCase` | 使能/掉电 | `axis.enable(active)` -> 规则校验 -> `driver.send()` |
| `JogAxisUseCase` | 点动控制 | `axis.jog(dir)` -> 规则校验 -> `driver.send()` |
| `MoveAbsoluteUseCase` | 绝对定位 | `axis.moveAbsolute(target)` -> 规则校验 -> `driver.send()` |
| `MoveRelativeUseCase` | 相对定位 | `axis.moveRelative(distance)` -> 规则校验 -> `driver.send()` |
| `StopAxisUseCase` | 急停 | `axis.stop()` -> `driver.send()` |

**流程模式**：UseCase 从 `AxisRepository` 获取轴引用 -> 调用领域方法 -> 若领域层通过校验，则将产生的 `AxisCommand` 通过 `IAxisDriver` 下发。若被拒绝，则透传 `RejectionReason`。

### 5.5 编排层 (Policy / Orchestrator)

编排器是 **有状态的状态机**，负责协调多个用例完成复杂业务流程：

#### JogOrchestrator（点动编排器）

状态流转：
```
Idle -> EnsuringEnabled -> IssuingJog -> Jogging -> IssuingStop -> WaitingForIdle -> EnsuringDisabled -> Done
```

特性：
- 自动使能：Jog 前自动检查并执行使能
- 双向防误杀：`stopJog()` 同时校验 AxisId 和方向
- 熔断保护：失败时自动掉电
- 安全收尾：停止后自动掉电回到 Disabled 状态

#### AutoAbsMoveOrchestrator（绝对定位编排器）

状态流转：
```
Initial -> EnsuringEnabled -> IssuingMove -> WaitingMotionStart -> WaitingMotionFinish -> Done
```

特性：
- 物理级终极验证：不仅检查领域意图完成，还验证实际位置是否到达目标
- 异常检测：若意图消失但物理未到位 -> 标记为 ABORTED
- 全局错误拦截：任何状态下检测到 `AxisState::Error` 立即中断

#### AutoRelMoveOrchestrator（相对定位编排器）

与 `AutoAbsMoveOrchestrator` 对称，处理相对定位的自动化编排。

---

## 6. 基础设施层 (infrastructure)

### 6.1 职责

提供技术实现细节，包括硬件仿真、日志记录、工具函数等。这层实现应用层定义的抽象接口。

### 6.2 核心组件

#### FakeAxisDriver（模拟驱动）

实现 `IAxisDriver` 接口，将命令路由到 `FakePLC`：

```
Axis(领域层) -> UseCase(应用层) -> FakeAxisDriver(基础层) -> FakePLC(物理仿真)
```

#### FakePLC（模拟 PLC / 物理引擎）

完整的伺服驱动器仿真，包含：

- **多轴寄存器组**：为每个 AxisId 维护独立的 `AxisStateInternal`
- **命令分发**：通过 `std::visit` 模式匹配处理所有 `AxisCommand` 类型
- **状态转换模拟**：使能延迟 150ms（`ENABLE_DELAY_MS`）
- **运动学引擎**：
  - 定位：P 控制收敛，速度由 `move_velocity` 决定
  - 点动：连续匀速累加，速度由 `jog_velocity` 决定
- **软限位检测**：到达限位时自动停止运动并触发日志
- **时钟驱动**：`tick(int ms)` 作为物理引擎心跳

#### Logger（日志系统）

分层日志系统，支持：
- **日志层级**：`LogLayer::DOMAIN` / `APP` / `HAL`（硬件抽象层）
- **日志级别**：`LOG_TRACE` / `LOG_DEBUG` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` / `LOG_SUMMARY`
- **链路追踪**：`TraceScope` 支持跨层调用链追踪
- **限频日志**：`LOG_TRACE_EVERY_N` 避免高频日志刷屏

---

## 7. 表现层 (presentation)

### 7.1 职责

提供图形化用户界面，将领域状态投影到 Qt/QML 视图，并将用户操作转发到应用层。

### 7.2 架构模式：MVVM

```
        View (QML)          ←->       ViewModel (C++)        ←->     Model (Domain)
   ┌─────────────────┐         ┌───────────────────┐         ┌─────────────────┐
   │ MainDashboard    │  bind   │ QtAxisViewModel   │  hold   │ Axis (领域实体)  │
   │ ActionControl    │ ←─────-> │     (Qt 适配)     │ ←─────-> │ Orchestrator    │
   │ TelemetryBlock   │         │ AxisViewModelCore │         │ UseCases        │
   └─────────────────┘         └───────────────────┘         └─────────────────┘
```

### 7.3 核心组件

#### AxisViewModelCore（ViewModel 纯逻辑核心）

不含 Qt 依赖的纯 C++ ViewModel，职责：
- **状态投影**：将 `Axis` 领域对象的状态映射为 UI 可消费属性（`state()`、`absPos()`、`relPos()` 等）
- **控制输入**：接收 UI 操作并转发给 Orchestrator（`jogPositivePressed()`、`moveAbsolute()` 等）
- **驱动机制**：`tick()` 方法驱动 Orchestrator 状态机流转

#### QtAxisViewModel（Qt 适配层）

继承 `QObject`，将 `AxisViewModelCore` 的属性暴露为 `Q_PROPERTY`，供 QML 数据绑定使用。

### 7.4 QML 视图结构

| 文件 | 职责 |
|------|------|
| `MainDashboard.qml` | 主仪表盘视图，组合各功能区块 |
| `AxisSelectorBlock.qml` | 轴选择器（Y/Z/R/X 切换） |
| `ActionControlBlock.qml` | 动作控制（使能/Jog/定位/停止） |
| `TelemetryBlock.qml` | 遥测数据显示（位置/速度/状态） |
| `IndustrialButton.qml` | 工业风格可复用按钮组件 |
| `AxisItemDelegate.qml` | 轴列表项代理 |
| `VelocitySettingsPopup.qml` | 速度设置弹窗 |
| `Theme.qml` | 全局主题单例 |

---

## 8. 测试层 (tests)

### 8.1 测试框架

使用 GoogleTest，通过 CMake 的 `gtest_discover_tests` 自动发现并注册测试。

### 8.2 测试分类

| 测试目录 | 测试内容 | 测试文件数 |
|---------|---------|-----------|
| `tests/domain/` | 领域实体与状态机逻辑 | 2 |
| `tests/application/` | 用例与编排器行为 | 8 |
| `tests/infrastructure/` | PLC 仿真与系统集成 | 2 |
| `tests/presentation/viewmodel/` | ViewModel 核心逻辑 | 1 |

### 8.3 测试特点

- **FakePLC 驱动的集成测试**：使用 `FakePLC` + `FakeAxisDriver` 组合实现完整的反馈闭环，无需真实硬件即可验证完整的"命令->运动->状态变化"链路
- **纯逻辑单元测试**：领域层的 `Axis` 实体不依赖任何外部组件，可直接单元测试

---

## 9. 关键设计决策

### 9.1 命令意图模式 (Command Intent Pattern)

Axis 实体内部采用 **单一命令槽位** 设计：`AxisCommand m_pending_intent`。任何时候轴最多只能有一个待处理命令，通过 `hasPendingCommand()` 检测，通过 `getPendingCommand()` 取出。这确保了指令的原子性和顺序性。

### 9.2 依赖反转 (DIP)

应用层定义 `IAxisDriver` 抽象接口，基础设施层实现 `FakeAxisDriver`。领域层完全不感知驱动实现细节。当需要对接真实硬件时，只需提供新的 `IAxisDriver` 实现即可。

### 9.3 龙门分组设计

- **逻辑轴 X**：面向统一操作界面
- **物理轴 X1/X2**：面向硬件控制
- **SystemContext** 负责管理龙门联动/解耦状态
- 当前阶段已完成骨架，后续阶段将实现防撕裂运算和安全拦截策略

### 9.4 状态机编排

复杂业务流程（如自动使能+Jog+停止+掉电）通过 Orchestrator 状态机管理，而非分散在各处。每个 tick 周期检查状态迁移条件，实现确定性的流程控制。

---

## 10. 构建与运行

### 构建命令

```bash
cmake -B build -S .
cmake --build build
```

### 运行测试

```bash
cd build && ctest
```

### 当前状态

- **领域层**：完整实现（Axis 状态机、SystemContext）
- **应用层**：完整实现（5 个 UseCase + 3 个 Orchestrator）
- **基础设施层**：完整实现（FakePLC 物理引擎 + Driver + Logger）
- **表现层**：QML UI 已标记为 `# 注释` 状态（骨架完成，待启用）
- **测试层**：全面覆盖（12+ 测试文件）

---

## 11. 扩展方向（规划）

1. **龙门防撕裂**：`abs(X1-X2)` 超差检查 + `GantrySafetyPolicy` 安全拦截
2. **真实硬件对接**：替换 `FakeAxisDriver` -> `RealAxisDriver`（Modbus/EtherCAT）
3. **表现层激活**：取消 CMakeLists 中 presentation 的注释，启用完整 QML UI
4. **AxisSyncService 完善**：完善反馈同步与状态分发机制
5. **多分组支持**：多 SystemContext 实例并行（如龙门组 + 辅助轴组）
