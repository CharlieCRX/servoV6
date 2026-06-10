# servoV6 项目架构总览

> **最后更新**: 2026-06-06  
> **对应代码基线**: main branch, commit `f517fd6`  
> **替代旧文档**: `docs/architecture.md`（已过期）

---

## 目录

1. [项目概述](#1-项目概述)
2. [架构分层总览](#2-架构分层总览)
3. [目录结构（全量）](#3-目录结构全量)
4. [领域层 (domain)](#4-领域层-domain)
5. [基础设施层 (infrastructure)](#5-基础设施层-infrastructure)
6. [应用层 (application)](#6-应用层-application)
7. [表现层 (presentation)](#7-表现层-presentation)
8. [核心数据流](#8-核心数据流)
9. [main.cpp 启动流程](#9-maincpp-启动流程)
10. [关键设计决策](#10-关键设计决策)
11. [测试层 (tests)](#11-测试层-tests)
12. [构建与运行](#12-构建与运行)

---

## 1. 项目概述

**servoV6** 是一个基于 **C++20 / Qt6 / QML** 的工控伺服电机控制系统，遵循 **Clean Architecture（整洁架构）** 分层设计。系统通过 **Modbus TCP** 与真实 PLC（汇川 H5U）通讯，实现对双分组 6 轴（Y/Z/R/X/X1/X2）的运动控制、龙门联动、安全急停以及远程 UDP 控制。

### 核心技术栈

| 技术 | 版本/说明 |
|------|----------|
| 语言标准 | C++20 |
| 构建系统 | CMake ≥ 3.21 |
| UI 框架 | Qt6 (Qml, Quick, QuickControls2, Network) |
| 网络库 | Asio 1.28.0 (header-only, FetchContent 自动获取) |
| Modbus 协议 | 自研 ProtocolRuntime（声明式寄存器映射 + 端序三态决议） |
| 测试框架 | GoogleTest |
| 版本控制 | Git |

### 项目仓库

```
origin:  git@github.com:CharlieCRX/servoV6.git
work:    git@10.1.2.222:buddy/servov6.git
```

---

## 2. 架构分层总览

项目严格遵循 **依赖方向由外向内** 的整洁架构原则，分为四层 + 一个测试层：

```
┌──────────────────────────────────────────────────────────────────┐
│                      presentation (表现层)                        │
│     QML UI / QtAxisViewModel / GantryViewModel /                 │
│     EmergencyStopViewModel / ErrorTranslator                      │
├──────────────────────────────────────────────────────────────────┤
│                      application (应用层)                         │
│     UseCases / Orchestrator / Policy / UdpCommandDispatcher /    │
│     SystemManager                                                 │
├──────────────────────────────────────────────────────────────────┤
│                        domain (领域层)                            │
│     Axis / AxisId / SystemContext / GantryCouplingController /   │
│     GantryPowerController / EmergencyStopController /             │
│     SystemCommand                                                 │
├──────────────────────────────────────────────────────────────────┤
│                    infrastructure (基础设施层)                     │
│     ISystemDriver / ModbusSystemDriver / plc::protocol /          │
│     AsioModbusTcpClient / RegisterRegistry / PlcPoller /         │
│     UdpServer / Logger / FakeAxisDriver / FakePLC (测试替身)      │
└──────────────────────────────────────────────────────────────────┘

                          tests (测试层)
      GoogleTest / FakePLC 驱动的集成测试 / 纯逻辑单元测试
```

**依赖规则**：
- `domain` ➜ 纯 C++，**零外部框架依赖**
- `infrastructure` ➜ 依赖 `domain`（`ISystemDriver.h` include `SystemCommand.h`）
- `application` ➜ 依赖 `domain` + `infrastructure`
- `presentation` ➜ 依赖 `application` + `domain`（ViewModel 持有 UseCase/Policy 引用）
- **测试层** 跨层依赖所有模块

**CMake 链接关系**：

```
infrastructure ── PUBLIC ──> asio, Qt6::Core, Qt6::Network
domain          ── PUBLIC ──> infrastructure
application     ── PUBLIC ──> domain, infrastructure, Qt6::Core
presentation    ── PUBLIC ──> Qt6::Core, domain, application
appservoV6      ── PUBLIC ──> domain, presentation
                     PRIVATE ─> Qt6::Qml, Qt6::Quick, Qt6::QuickControls2
```

---

## 3. 目录结构（全量）

```
servoV6/
├── CMakeLists.txt                         # 顶层构建 + Asio 获取 + 子模块
├── main.cpp                               # 应用入口（见 §9）
├── Main.qml                               # QML 主入口
│
├── domain/                                # ====== 领域层 ======
│   ├── CMakeLists.txt
│   ├── command/
│   │   └── SystemCommand.h                # 统一命令总线（variant）
│   ├── entity/
│   │   ├── Axis.h / .cpp                  # 轴领域实体（核心聚合根）
│   │   ├── AxisId.h                       # 轴标识枚举 (Y/Z/R/X/X1/X2)
│   │   ├── ContextRejection.h             # 上下文拦截拒绝原因
│   │   └── SystemContext.h                # 分组上下文（轴容器+龙门+急停）
│   ├── gantry/
│   │   ├── GantryCouplingController.h     # 龙门联动五态状态机
│   │   ├── GantryCouplingState.h          # 龙门耦合状态枚举
│   │   ├── GantryFeedback.h               # 龙门反馈结构体
│   │   ├── GantryPowerController.h        # 龙门电机使能控制器
│   │   └── GantryRejection.h              # 龙门操作拒绝原因
│   └── safety/
│       ├── EmergencyStopController.h      # 急停五态安全状态机
│       ├── SafetyState.h                  # 安全状态枚举
│       └── SafetyRejection.h              # 安全操作拒绝原因
│
├── infrastructure/                        # ====== 基础设施层 ======
│   ├── CMakeLists.txt
│   ├── ISystemDriver.h                    # ★ 统一驱动抽象接口
│   ├── FakeAxisDriver.h                   # 模拟驱动（测试替身）
│   ├── FakePLC.h                          # 模拟 PLC 物理引擎（测试替身）
│   ├── logger/
│   │   ├── Logger.h                       # 日志系统核心宏
│   │   ├── LogContext.h                   # 日志上下文
│   │   └── TraceScope.h                   # 链路追踪作用域
│   ├── plc/
│   │   ├── ModbusSystemDriver.h           # ★ 真实 Modbus 驱动实现（~876行）
│   │   ├── AxisStateDeriver.h             # 轴状态推导引擎
│   │   └── protocol/
│   │       ├── RegisterAddressAll.h       # ★ 全量寄存器地址声明
│   │       ├── RegisterMetadata.h         # 寄存器元数据 (RegisterInfo)
│   │       ├── RegisterRegistry.h         # 寄存器注册表（去重+聚合）
│   │       ├── RegisterCodec.h            # 编解码器（位/字/浮点转换+端序）
│   │       ├── ProtocolProfile.h          # PLC 协议特征（端序策略）
│   │       ├── EndianPolicy.h             # 端序策略枚举
│   │       ├── IModbusClient.h            # Modbus 客户端抽象接口
│   │       ├── AsioModbusTcpClient.h/.cpp # ★ Asio 实现的真实 Modbus TCP 客户端
│   │       ├── ModbusTcpFrame.h           # Modbus TCP 帧定义
│   │       ├── PlcValue.h                 # PLC 值类型（bool/int16/float）
│   │       ├── PlcDevice.h               # 寄存器读写门面
│   │       ├── PlcPoller.h/.cpp           # 轮询请求生成 → 响应装配
│   │       ├── PlcSnapshot.h              # 设备快照（带可信度标记）
│   │       ├── MemorySnapshot.h           # 内存快照容器
│   │       └── validator/
│   │           └── ProtocolConstraintValidator.cpp
│   ├── udp/
│   │   └── UdpServer.h                    # UDP 网络 IO 封装
│   └── utils/
│       ├── IClock.h                       # 时钟抽象（真实/假时钟）
│       ├── CommandFormatter.h             # 命令格式化工具
│       └── overloaded.h                   # variant 访问辅助
│
├── application/                           # ====== 应用层 ======
│   ├── CMakeLists.txt
│   ├── SystemManager.h                    # 系统分组管理器
│   ├── UseCaseError.h                     # UseCase 错误类型
│   ├── axis/
│   │   ├── EnableUseCase.h               # 使能/掉电用例
│   │   ├── JogAxisUseCase.h              # 点动用例
│   │   ├── MoveAbsoluteUseCase.h          # 绝对定位用例（已废弃，见 Policy）
│   │   ├── MoveRelativeUseCase.h          # 相对定位用例（已废弃，见 Policy）
│   │   ├── StopAxisUseCase.h             # 停止用例
│   │   ├── TriggerAbsMoveUseCase.h        # ★ 触发绝对移动（四寄存器解耦）
│   │   ├── TriggerRelMoveUseCase.h        # ★ 触发相对移动（四寄存器解耦）
│   │   └── AxisSyncService.h             # ★ 轴同步服务（反馈同步+状态分发）
│   ├── policy/
│   │   ├── AbsMovePolicy.h               # ★ 绝对定位策略（状态机）
│   │   ├── RelMovePolicy.h               # ★ 相对定位策略（状态机）
│   │   ├── JogOrchestrator.h             # 点动编排器（状态机）
│   │   ├── AutoAbsMoveOrchestrator.h      # 自动绝对定位编排器（已废弃）
│   │   ├── AutoRelMoveOrchestrator.h      # 自动相对定位编排器（已废弃）
│   │   └── GantryOrchestrator.h           # ★ 龙门编排器（联动/解耦流程）
│   ├── safety/
│   │   ├── EmergencyStopUseCase.h         # 急停触发用例
│   │   └── ReleaseEmergencyStopUseCase.h  # 急停解除用例
│   └── udp/
│       ├── UdpProtocol.h                  # UDP 协议字段定义
│       ├── UdpResponseBuilder.h           # UDP 回复构建器
│       └── UdpCommandDispatcher.h         # ★ UDP 命令解析/路由/执
│
├── presentation/                          # ====== 表现层 ======
│   ├── CMakeLists.txt
│   ├── viewmodel/
│   │   ├── AxisViewModelCore.h/.cpp       # ViewModel 纯逻辑核心（无 Qt 依赖）
│   │   ├── QtAxisViewModel.h/.cpp         # Qt 属性绑定适配层 (+Q_PROPERTY)
│   │   ├── GantryViewModel.h/.cpp         # 龙门 ViewModel
│   │   ├── EmergencyStopViewModel.h       # 急停安全 ViewModel
│   │   ├── ErrorTranslator.h/.cpp         # 错误码翻译器
│   │   └── ViewModelError.h              # ViewModel 错误类型
│   └── qml/
│       ├── core/
│       │   └── Theme.qml                  # 全局主题（Singleton）
│       ├── components/
│       │   ├── IndustrialButton.qml       # 工业风格按钮
│       │   ├── AxisItemDelegate.qml       # 轴列表项代理
│       │   ├── VelocitySettingsPopup.qml  # 速度设置弹窗
│       │   ├── TargetSettingsPopup.qml    # 目标设置弹窗
│       │   └── NumPad.qml                 # 数字键盘
│       ├── blocks/
│       │   ├── AxisSelectorBlock.qml      # 轴选择器区块
│       │   ├── ActionControlBlock.qml     # 动作控制区块
│       │   ├── TelemetryBlock.qml         # 遥测数据显示区块
│       │   └── ErrorPanelBlock.qml        # 错误面板区块
│       └── views/
│           └── MainDashboard.qml          # 主仪表盘视图
│
├── tests/                                 # ====== 测试层 ======
│   ├── CMakeLists.txt
│   ├── udp_client_test.py                 # UDP 客户端测试脚本
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   └── presentation/
│
├── external/
│   └── googletest/                        # GoogleTest 框架
│
└── docs/
    ├── architecture/                      # ★ 架构设计文档集
    │   ├── servoV6项目架构总览.md          # （本文档）
    │   ├── ModbusTCP架构演进设计文档v*.md  # Modbus 演进历史
    │   ├── ISystemDriver重构设计说明.md
    │   ├── Infrastructure项目架构设计说明文档.md
    │   ├── UDP通讯层设计文档.md
    │   ├── Domain层send与pollFeedback数据流架构说明.md
    │   └── ...（共 45+ 篇详细设计文档）
    ├── bug/
    ├── refactor/
    └── ui/
```

---

## 4. 领域层 (domain)

### 4.1 设计原则

领域层是系统的最内层，**零外部框架依赖**（纯 C++20），封装所有伺服控制的核心业务规则。该层定义了系统的"语言"——实体、状态、命令、拒绝原因。

### 4.2 核心实体

#### 4.2.1 Axis（轴聚合根）

`Axis` 是系统的核心领域实体，封装了单轴的完整生命周期：

**状态机**（7 态）：
```
Unknown(0) → Disabled(1) → Idle(2) → Jogging(3) / MovingAbsolute(4) / MovingRelative(5) → Error(6)
```

**统一命令槽位**（`AxisCommand = std::variant<...>`）：
```cpp
std::monostate               // 空槽位
JogCommand                   // 点动（方向+启停）
MoveCommand                  // 定位（绝对/相对+目标值）【已废弃，保留兼容】
StopCommand                  // 停止
EnableCommand                // 使能/掉电
SetJogVelocityCommand        // 设置点动速度
SetMoveVelocityCommand       // 设置定位速度
ZeroAbsoluteCommand          // 绝对零点归零
SetRelativeZeroCommand       // 设置相对零点
ClearRelativeZeroCommand     // 清除相对零点
SetAbsTargetCommand          // ★ 阶段1：设置绝对目标
TriggerAbsMoveCommand        // ★ 阶段1：触发绝对移动
SetRelTargetCommand          // ★ 阶段1：设置相对距离
TriggerRelMoveCommand        // ★ 阶段1：触发相对移动
```

**设计要点**：
- **单一命令槽位**：`m_pending_intent` 同一时刻最多一个待发命令
- **领域规则**：软限位预检、边界拦截、状态合法性校验
- **不感知分组**：Axis 不知道自己在哪个 SystemContext 中
- **反馈驱动**：`applyFeedback(AxisFeedback)` 注入 PLC 真实状态
- **四寄存器解耦**：目标设置与触发分离（阶段1新增），由 application 层 Policy 编排

#### 4.2.2 AxisId（轴标识）

```cpp
enum class AxisId { Y, Z, R, X, X1, X2 };
```

- **Y, Z**：常规逻辑轴
- **R**：旋转轴（UDP 远程控制主要对象）
- **X**：龙门逻辑轴（联动时为虚拟轴）
- **X1, X2**：龙门物理轴

#### 4.2.3 SystemContext（分组上下文）

`SystemContext` 是分组运行的载体，组合持有三个核心领域组件：

```
SystemContext
├── Axis[6]  (X/X1/X2/Y/Z/R)            // 6 个固定轴实体
├── GantryCouplingController             // 龙门联动五态状态机
├── GantryPowerController                // 龙门电机使能控制器
├── EmergencyStopController              // 急停五态安全状态机（值语义）
└── ISystemDriver*                       // 驱动绑定
```

**关键约束**：
- 不同 SystemContext 之间**绝对隔离**（平行宇宙模式）
- 龙门状态和急停状态属于 SystemContext 的全局状态，不属于任何单个 Axis
- `tryGetAxis(id, out, reason)` 控制操作入口，经过四层拦截：
  1. **Layer 0 — 安全锁定**：急停中/未同步/过渡中 → `SystemSafetyLocked`
  2. **Layer 1 — 龙门同步**：NotSynchronized → `GantryNotSynchronized`
  3. **Layer 2 — 龙门语义**：Coupled → `PhysicalAxisLockedByGantry` / Decoupled → `LogicalAxisUnavailableWhenDecoupled`
  4. **Layer 3 — 容器查找**：`AxisNotRegistered`
- `tryReadAxis(id, out, reason)` 遥测读取入口，跳过 Layer 0 安全锁定

### 4.3 龙门子系统

#### GantryCouplingController（龙门联动控制器）

五态耦合状态机：
```
NotSynchronized ──(applyFeedback)──> Coupled / Decoupled
Decoupled       ──(requestCouple)──> CouplingRequested ──(PLC确认)──> Coupled
Coupled         ──(requestDecouple)──> DecouplingRequested ──(PLC确认)──> Decoupled
```

**设计原则**：
- **不持有 Axis 引用**：PLC 负责所有物理安全校验（位置超差、使能状态、静止状态）
- **PLC 是最终安全裁决者**：通过 `GantryFeedback.errorCode` 反馈拒绝原因
- **意图-反馈分离**：`requestCouple()` 产生意图 → `applyFeedback()` 确认状态

#### GantryPowerController（龙门电机控制器）

独立于联动状态，任何状态下均可访问。管理龙门电机（X1/X2）的整体使能/掉电。

### 4.4 安全子系统

#### EmergencyStopController（急停控制器）

五态工业安全状态机：
```
NotSynchronized ──(首次applyFeedback)──> Running / EmergencyStopped
Running         ──(requestEmergencyStop)──> EmergencyStopping ──(PLC确认)──> EmergencyStopped
EmergencyStopped──(requestRelease)──> ReleasingEmergencyStop ──(PLC确认)──> Running
Running         ──(物理急停按钮触发,PLC直接反馈true)──> EmergencyStopped
```

**设计原则**：
- **单一反馈入口**：`applyFeedback()` 是 PLC Feedback 的唯一入口
- **反馈驱动**：状态变更由 PLC 反馈确认，不由请求立即变更
- **命令与状态分离**：PLC 有独立的"急停命令"寄存器和"急停中"状态寄存器
- **EmergencyStopped 是锁存态**：一旦进入，不会因 PLC 反馈恢复而自动退出
- **Controller 永远相信 PLC Feedback**：不与物理真相为敌

### 4.5 统一命令总线

```cpp
using SystemCommand = std::variant<
    AxisCommandWithId,        // 单轴命令 (AxisId + AxisCommand)
    GantryCouplingCommand,    // 龙门联动/解耦
    GantryPowerCommand,       // 龙门电机使能/掉电
    EmergencyStopCommand      // 急停触发/解除
>;
```

所有领域层产生的控制意图通过此 variant 统一发送到基础设施层。`ISystemDriver::send(SystemCommand)` 是唯一的命令下发入口。

---

## 5. 基础设施层 (infrastructure)

### 5.1 设计目标

基础设施层实现 **「ProtocolRuntime —— 从声明到运行」** 的完整管线：将开发者声明的 Modbus 寄存器映射表，在运行时自动、可靠地转换为业务层可直接使用的强类型数据。

### 5.2 驱动抽象

#### ISystemDriver（统一驱动接口）

```cpp
class ISystemDriver {
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

**双通路设计**：
- **命令通路**：`send()` 返回 `CommunicationResult`，只表达"帧是否送达"，不表达"PLC是否执行"
- **反馈通路**：`pollFeedback()` 主动拉取，每主循环周期调用一次（10ms）

#### CommunicationResult

精细表达 Modbus TCP 通讯的每一类失败：

| 状态 | 含义 | 可重试 |
|------|------|--------|
| `Sent` | 成功写入 PLC 寄存器 | — |
| `NetworkError` | TCP 连接失败/网线断开 | ❌ |
| `Timeout` | 通讯超时 | ✅ |
| `Busy` | PLC 忙（Exc 0x06） | ✅ |
| `ProtocolError` | Modbus 异常响应 | ❌ |
| `InvalidResponse` | 数据格式非法 | ❌ |
| `Disconnected` | 未连接 | ❌ |

### 5.3 ModbusSystemDriver（真实驱动）

`plc::ModbusSystemDriver` 是 `ISystemDriver` 的真实硬件实现（约 876 行），核心职责：

1. **寄存器选择器**：19 个命令寄存器 + 20 个反馈寄存器的 `AxisId → RegisterInfo` 映射
2. **系统命令分发**：通过双层 `std::visit` 将 `SystemCommand` 拆解为具体的 Modbus 写操作
3. **边沿触发协议**：`PendingEdge` 队列管理"写 ON → 等待 150ms → 写 OFF"的时序
4. **反馈轮询**：`pollFeedback()` 执行全量寄存器读取 → 状态推导 → 分发到各领域实体
5. **状态推导**：`AxisStateDeriver` 融合多个 PLC 信号位推导最终 `AxisState`

**依赖注入**（可替换组件）：
```
ModbusSystemDriver
├── PlcDevice*          // 寄存器读写门面
├── IModbusClient*      // Modbus TCP 传输
├── PlcPoller*          // 轮询策略
└── IClock*             // 时钟（测试时可注入 FakeClock）
```

### 5.4 plc::protocol 子系统

实现**声明式寄存器映射**架构，核心组件：

| 组件 | 职责 |
|------|------|
| `RegisterInfo` | 单个寄存器的完整元数据：地址、类型(Coil/HoldingReg)、语义类型(Bool/Int16/Float32)、端序策略、描述 |
| `RegisterRegistry` | 寄存器注册表：去重、分组（Coil/Word）、地址聚合 |
| `RegisterCodec` | 编解码器：位↔Bool、16位字↔Int16、双字↔Float32（3种端序：ABCD/CDAB/DCBA） |
| `PlcPoller` | 轮询策略：注册表 → 地址区间聚合 → Modbus 请求生成 → 响应装配为快照 |
| `PlcDevice` | 寄存器读写门面：`readBool()`, `writeFloat()`, `readInt16()` 等 |
| `PlcSnapshot` | 设备快照：携带 `trusted` 标志位，标记本轮采集是否完全成功 |
| `ProtocolProfile` | PLC 协议特征：端序策略（全局默认 + 寄存器级 override） |
| `RegisterAddressAll.h` | 全量寄存器地址声明（X/Y/Z/R 四轴 + 系统全局） |
| `AsioModbusTcpClient` | 基于 Asio 的真实 Modbus TCP 传输实现 |

**端序三态决议**：
```
寄存器级 override > Profile 全局默认 > 运行时拒绝
```

### 5.5 UdpServer

基于 `QUdpSocket` 的非阻塞 UDP 服务器：
- 监听 62000 端口，接收 JSON 格式远程控制命令
- 收包 → 透传 `UdpCommandDispatcher` → 回包
- 由外部 `tick()` 循环驱动

### 5.6 日志系统

分层日志系统，支持：
- **日志层级**：`LogLayer::DOM` / `APP` / `HAL`（硬件抽象层）
- **日志级别**：`LOG_TRACE` / `LOG_DEBUG` / `LOG_INFO` / `LOG_WARN` / `LOG_ERROR` / `LOG_SUMMARY`
- **链路追踪**：`TraceScope` RAII 作用域
- **限频日志**：`LOG_WARN_EVERY_MS(N, ...)` 避免高频刷屏
- **输出**：Console + 文件（`<exe目录>/logs/`）

### 5.7 测试替身

- **FakeAxisDriver**：实现 `ISystemDriver`，提供 `send()` 存根用于纯逻辑测试
- **FakePLC**：完整的 6 轴物理引擎仿真（含运动学、软限位、使能延迟），可用于无硬件集成测试

---

## 6. 应用层 (application)

### 6.1 职责

应用层定义了系统的**用例（UseCases）**、**编排策略（Orchestrator/Policy）** 和**分组管理**。它不包含核心业务规则（属于 domain），而是协调领域实体完成特定业务场景。

### 6.2 分组管理

#### SystemManager

管理多个 `SystemContext` 分组实例：

```
SystemManager
├── "Machine_A" → SystemContext (Y/Z/R/X/X1/X2 轴 + 驱动A)
└── "Machine_B" → SystemContext (Y/Z/R/X/X1/X2 轴 + 驱动B)
```

- **Try-Get 模式**：`tryGetGroup(name, out, reason)` 安全查找
- **多分组并行**：每个分组绑定独立的 `ISystemDriver`（不同 PLC IP）
- **创建/删除分组**：`createGroup()` / `removeGroup()`

### 6.3 用例层 (Use Cases)

每个用例遵循**单一职责原则**：

| 用例 | 职责 | 流向 |
|------|------|------|
| `EnableUseCase` | 使能/掉电 | axis.enable() → driver.send() |
| `JogAxisUseCase` | 点动控制 | axis.jog() → driver.send() |
| `StopAxisUseCase` | 停止 | axis.stop() → driver.send() |
| `TriggerAbsMoveUseCase` | 触发绝对定位 | axis.triggerAbsMove() → driver.send() |
| `TriggerRelMoveUseCase` | 触发相对定位 | axis.triggerRelMove() → driver.send() |
| `EmergencyStopUseCase` | 触发急停 | estop.requestEmergencyStop() → driver.send() |
| `ReleaseEmergencyStopUseCase` | 解除急停 | estop.requestReleaseEmergencyStop() → driver.send() |

### 6.4 编排层 (Policy / Orchestrator)

编排器是**有状态的状态机**，负责协调多步骤业务流程：

#### JogOrchestrator

状态流转：`Idle → EnsuringEnabled → IssuingJog → Jogging → IssuingStop → WaitingForIdle → EnsuringDisabled → Done`

- 自动使能/掉电
- 双向防误杀（同时校验 AxisId 和方向）
- 熔断保护

#### AbsMovePolicy / RelMovePolicy

面向**四寄存器解耦**的新定位策略：
- **AbsMovePolicy**：`startAbs()` → 设置目标 → 触发 → 等待运动开始 → 等待运动完成
- **RelMovePolicy**：`startRel()` → 设置距离 → 触发 → 等待运动开始 → 等待运动完成
- **物理级验证**：不仅检查领域意图完成，还验证实际位置是否到达目标
- **超时保护**：运动超时自动报错

#### GantryOrchestrator

龙门联动/解耦完整流程编排：
- **联动流程**：X1拨码请求 → 设置 enableCoupling=true → 等待 PLC 确认 → 检查 errorCode → Done
- **解耦流程**：设置 enableCoupling=false → 等待 PLC 确认 → Done

### 6.5 UDP 通讯层

| 组件 | 职责 |
|------|------|
| `UdpProtocol.h` | 协议字段常量定义（cmd/motor/group/target/offset/speed） |
| `UdpCommandDispatcher` | JSON 解析 → 必填字段校验 → motor→AxisId 映射 → group 校验 → Axis 获取 → 路由到处理函数 |
| `UdpResponseBuilder` | 构建 JSON 格式的成功/错误回复 |

**当前支持的命令**（仅 R 轴）：
| cmd | 名称 | 说明 |
|-----|------|------|
| 0 | MOVE_TO_REL_TARGET | 基于相对零点的绝对位置移动 |
| 1 | MOVE_OFFSET | 相对偏移移动 |
| 2 | GET_REL_POSITION | 查询当前相对位置 |
| 3 | SET_MOVE_SPEED | 设置位置移动速度 |
| 4 | GET_MOVE_SPEED | 查询位置移动速度 |
| 5 | SET_REL_ZERO | 设置相对零点 |

### 6.6 UseCaseError

统一的 UseCase 错误类型（`std::variant`）：
```cpp
std::monostate              // 成功
ContextRejection            // 上下文拦截
RejectionReason             // 领域规则拒绝
CommunicationResult         // 通讯失败
ErrTimeout                  // 超时
```

---

## 7. 表现层 (presentation)

### 7.1 MVVM 架构

```
        View (QML)          ←→       ViewModel (C++)        ←→     Model (Domain/App)
   ┌─────────────────┐         ┌────────────────────┐         ┌─────────────────┐
   │ MainDashboard    │  bind   │ QtAxisViewModel    │  hold   │ Axis (领域实体)  │
   │ ActionControl    │ ←─────→ │     (Qt 适配)      │ ←─────→ │ Policy          │
   │ TelemetryBlock   │         │ AxisViewModelCore  │         │ UseCases        │
   │ ErrorPanelBlock  │         │ (纯逻辑核心)       │         │ SystemContext   │
   └─────────────────┘         │ GantryViewModel    │         └─────────────────┘
                               │ EmergencyStopVM    │
                               │ ErrorTranslator    │
                               └────────────────────┘
```

### 7.2 ViewModel 层

#### AxisViewModelCore（纯逻辑核心）

- **零 Qt 依赖**：纯 C++ 实现，100% 可单元测试
- **状态投影**：将 Axis 领域状态映射为 UI 可消费属性
- **控制输入**：接收操作并转发给 Policy/UseCase
- **tick() 驱动**：每帧推进 Orchestrator/Policy 状态机

#### QtAxisViewModel（Qt 适配层）

- 继承 `QObject`，将 `AxisViewModelCore` 属性暴露为 `Q_PROPERTY`
- 属性变化信号（`NOTIFY`）驱动 QML 自动刷新
- 缓存节流：仅在值变化超阈值时 emit signal

**关键 Q_PROPERTY**：
| 属性 | 说明 |
|------|------|
| `state` / `stateText` / `isEnabled` | 轴状态 |
| `absPos` / `relPos` | 位置遥测 |
| `isLoading` / `moveStep` | Policy 运行状态 |
| `absMoveTarget` / `relMoveTarget` | PLC 目标寄存器镜像 |
| `hasError` / `hasBlockingError` | 错误状态 |
| `errorCode` / `errorMessage` / `errorCategory` | 错误详情 |
| `jogVelocity` / `moveVelocity` | 速度配置 |

#### GantryViewModel

桥接 Domain 龙门控制器状态到 QML：
- 龙门联动/解耦操作接口
- 龙门状态查询（isCoupled, isCouplingRequested, isDecouplingRequested）
- tick() 驱动 GantryOrchestrator 状态机

#### EmergencyStopViewModel

桥接 Domain 急停控制器状态到 QML：
- 急停触发/解除操作接口
- 急停状态查询（isSystemLocked, isEmergencyStopped）
- 安全状态日志

### 7.3 QML 视图结构

| 文件 | 职责 |
|------|------|
| `MainDashboard.qml` | 主仪表盘视图，组合各功能区块 |
| `AxisSelectorBlock.qml` | 轴选择器（分组 + 轴切换） |
| `ActionControlBlock.qml` | 动作控制（使能/Jog/定位/停止/急停） |
| `TelemetryBlock.qml` | 遥测数据显示（位置/速度/状态/目标） |
| `ErrorPanelBlock.qml` | 错误面板（错误列表+确认） |
| `IndustrialButton.qml` | 工业风格可复用按钮 |
| `AxisItemDelegate.qml` | 轴列表项代理 |
| `VelocitySettingsPopup.qml` | 速度设置弹窗 |
| `TargetSettingsPopup.qml` | 目标设置弹窗 |
| `NumPad.qml` | 数字键盘 |
| `Theme.qml` | 全局主题单例 |

---

## 8. 核心数据流

### 8.1 命令下发链路（以绝对定位为例）

```
QML Button Press
  │
  ▼
QtAxisViewModel::triggerAbsMove()
  │
  ▼
AxisViewModelCore::triggerAbsMove()
  ├── Axis::triggerAbsMove()              // 领域规则校验
  │   └── RejectionReason (通过/拒绝)
  │
  ├── Axis::hasPendingCommand() → getPendingCommand()
  │   └── TriggerAbsMoveCommand
  │
  ▼
ISystemDriver::send(
    AxisCommandWithId{AxisId::R, TriggerAbsMoveCommand{}}
)
  │
  ▼
ModbusSystemDriver::send()
  ├── Command dispatch (双层 variant visit)
  ├── Reg selector: regCmdAbsTrigger(id)
  ├── sendEdgeTrigger(reg)                // 边沿触发协议
  │   ├── PlcDevice::writeBool(true)
  │   └── PendingEdge 入队
  │
  ▼
AsioModbusTcpClient::writeCoil()          // Modbus FC05
  │
  ▼
PLC (汇川 H5U)
```

### 8.2 反馈轮询链路

```
QTimer (10ms)
  │
  ▼
ISystemDriver::pollFeedback(SystemContext& ctx)
  │
  ▼
ModbusSystemDriver::pollFeedback()
  ├── servicePendingEdgeTriggers()        // 处理边沿触发OFF
  ├── PlcPoller::prepare()                // 生成批量读取请求
  │   └── RegisterRegistry → 地址聚合 → FC01/FC03 请求
  │
  ├── AsioModbusTcpClient::readCoils/readHoldingRegisters()
  │   └── Modbus TCP 批量读取
  │
  ├── PlcPoller::assemble()               // 装配为 PlcSnapshot
  ├── PlcDevice::updateSnapshot()
  │
  ├── For each AxisId:
  │   ├── 读取各反馈寄存器
  │   ├── AxisStateDeriver::deriveAxisState()
  │   ├── 构筑 AxisFeedback
  │   └── axis->applyFeedback(fb)
  │
  ├── ctx.emergencyStopController().applyFeedback(estopActive)
  │
  └── ctx.gantryCouplingController().applyFeedback(gantryFb)
      ctx.gantryPowerController().applyFeedback(gantryFb)
```

### 8.3 UDP 远程控制链路

```
Remote Client (JSON via UDP)
  │
  ▼
UdpServer::tick()
  ├── QUdpSocket::readDatagram()
  │
  ▼
UdpCommandDispatcher::dispatch(jsonStr)
  ├── JSON 解析
  ├── 必填字段校验 (cmd/motor/group)
  ├── motor → AxisId 映射 (仅 R 轴)
  ├── SystemManager::tryGetGroup()
  ├── SystemContext::tryGetAxis() / tryReadAxis()
  ├── 路由到处理函数 (cmd 0~5)
  │   ├── Axis 领域操作
  │   ├── driver->send() 下发命令
  │   ├── Policy 状态机驱动（阻塞等待完成）
  │   └── 构建 JSON 回复
  │
  ▼
UdpResponseBuilder::buildSuccess/buildError()
  │
  ▼
QUdpSocket::writeDatagram() → 回包
```

---

## 9. main.cpp 启动流程

```
1. 初始化 Logger（Console + File）
2. 注册全量 Modbus 寄存器（RegisterRegistry）
3. 创建两个 AsioModbusTcpClient（连接不同 PLC IP）
4. 创建 PlcPoller + PlcDevice + ModbusSystemDriver（双套）
5. 创建两个 SystemContext（"Machine_A": Y/Z/R/X/X1/X2, "Machine_B": X/X1/X2/Y/Z/R）
6. 为所有 Axis 设置身份信息（绕过龙门语义拦截）
7. 首次 pollFeedback（将 PLC 当前状态注入）
8. 启动 UdpServer（端口 62000）
9. 创建 12 个 AxisViewModelCore + QtAxisViewModel（2 组 × 6 轴）
10. 创建 EmergencyStopViewModel + GantryViewModel（每组一个）
11. QML 引擎初始化 + 依赖注入（setContextProperty）
12. 10ms Tick Loop:
    ├── 两个分组 pollFeedback（轴 + 龙门 + 急停）
    ├── 消费 EmergencyStopController 的 pending command
    ├── 12 个 AxisViewModel tick()
    ├── EmergencyStopVM tick()
    ├── GantryVM tick()
    └── UdpServer tick()
13. 1s 摘要时钟（按分组打印轴状态）
14. app.exec()
```

---

## 10. 关键设计决策

### 10.1 命令意图模式 (Command Intent Pattern)

每个领域控制器（Axis / GantryCouplingController / EmergencyStopController）内部采用**单一命令槽位**设计：
- `m_pending_intent` 同一时刻最多一个待发命令
- `hasPendingCommand()` 检测 → `popPendingCommand()` 取出
- 命令产生与消费分离，确保原子性和顺序性

### 10.2 依赖反转 (DIP)

- `ISystemDriver` 在 infrastructure 层定义抽象接口
- `ModbusSystemDriver` 在 infrastructure 层实现真实驱动
- `FakeAxisDriver` 在 infrastructure 层提供测试替身
- Domain 层通过 `ISystemDriver*` 与硬件解耦

### 10.3 四寄存器解耦

阶段1 将传统的 `MoveCommand`（目标+触发合二为一）拆分为：
- **SetAbsTargetCommand** / **SetRelTargetCommand**：仅写目标值 D 寄存器
- **TriggerAbsMoveCommand** / **TriggerRelMoveCommand**：仅写触发位 M 寄存器

优势：
- 目标写入与触发执行可异步进行
- 支持"先预览目标，再确认执行"的交互模式
- PlcPoller 可在反馈中独立回读目标寄存器

### 10.4 龙门分组设计

- **逻辑轴 X**：面向统一操作界面，联动时可用
- **物理轴 X1/X2**：联动时锁定，解耦时独立可操作
- **GantryCouplingController**：五态状态机管理联动/解耦流程
- **PLC 是最终安全裁决者**：上位机不自行校验位置超差/使能/静止，全由 PLC 通过 errorCode 反馈

### 10.5 急停安全状态机

- **五态状态机**：NotSynchronized → Running ↔ EmergencyStopping → EmergencyStopped ↔ ReleasingEmergencyStop
- **命令与状态分离**：PLC 有独立的"急停命令"寄存器（write）和"急停中"状态寄存器（read）
- **锁存安全态**：EmergencyStopped 不会因 PLC 反馈自动退出，必须显式解除
- **启动同步**：初始 NotSynchronized，首次 applyFeedback() 完成与 PLC 的同步

### 10.6 ProtocolRuntime 声明式架构

- **RegisterRegistry**：开发者声明式注册所有需要轮询的寄存器
- **地址聚合**：PlcPoller 自动将离散地址聚合成连续区间，最小化 Modbus 请求次数
- **端序三态决议**：寄存器级 > Profile 全局 > 拒绝
- **可信度标记**：PlcSnapshot 携带 `trusted` 标志，业务层据此决定是否使用数据

### 10.7 边沿触发协议

PLC 的触发位寄存器需要"写 ON → 保持 150ms → 写 OFF"的时序。`PendingEdge` 队列管理此协议：
- `enqueueEdge()` 注册边沿
- `servicePendingEdgeTriggers()` 在每次 pollFeedback 时检查并关闭到期的边沿

---

## 11. 测试层 (tests)

### 11.1 测试框架

GoogleTest，通过 CMake 的 `gtest_discover_tests` 自动发现。

### 11.2 测试分类

| 测试目录 | 测试内容 |
|---------|---------|
| `tests/domain/` | Axis 状态机、SystemContext 拦截逻辑 |
| `tests/application/` | UseCase 行为、Policy 状态机编排 |
| `tests/infrastructure/` | FakePLC 物理引擎、ModbusSystemDriver 集成测试 |
| `tests/presentation/viewmodel/` | AxisViewModelCore 纯逻辑 |
| `tests/udp_client_test.py` | UDP 远程控制端到端测试（Python 脚本） |

### 11.3 测试策略

- **FakePLC 集成测试**：FakePLC + FakeAxisDriver 组合实现完整反馈闭环，无需真实硬件
- **纯逻辑单元测试**：领域层不依赖任何外部组件，可直接单测
- **通讯层测试**：plc::protocol 的编解码器/轮询器使用纯数据变换，100% 可单测

---

## 12. 构建与运行

### 构建命令

```bash
# 首次构建（自动下载 Asio 和 GoogleTest）
cmake -B build -S .
cmake --build build

# 快速构建（跳过 CMake 重生成）
cmake --build build
```

### 运行测试

```bash
cd build && ctest
```

### 运行应用

```bash
./build/appservoV6
```

### 当前状态

| 层 | 状态 |
|-----|------|
| domain | ✅ 完整实现（Axis + SystemContext + Gantry + EmergencyStop） |
| infrastructure | ✅ 完整实现（ModbusSystemDriver + plc::protocol + UdpServer + Logger） |
| application | ✅ 完整实现（UseCases + Policy + UdpCommandDispatcher + SystemManager） |
| presentation | ✅ 完整实现（MVVM + QML UI 双分组 12 轴） |
| tests | ✅ 全面覆盖（单元测试 + 集成测试 + UDP 端到端） |

---

> **相关文档**：  
> - `docs/architecture/Infrastructure项目架构设计说明文档.md` — plc::protocol 详细设计  
> - `docs/architecture/Domain层send与pollFeedback数据流架构说明.md` — 数据流详解  
> - `docs/architecture/UDP通讯层设计文档.md` — UDP 远程控制详细设计  
> - `docs/architecture/ModbusTCP架构演进设计文档v4.md` — Modbus 演进历史  
> - `docs/architecture/ISystemDriver重构设计说明.md` — 驱动接口重构说明