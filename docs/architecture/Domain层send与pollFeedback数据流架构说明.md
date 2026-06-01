# Domain 层 send / pollFeedback 数据流架构说明

> **视角**：以"查询 Machine_A 分组的 Y 轴状态"为线索，追踪命令下发（send）和反馈轮询（pollFeedback）的完整链路。

---

## 目录

1. [架构总览](#1-架构总览)
2. [分层职责速查表](#2-分层职责速查表)
3. [核心数据结构](#3-核心数据结构)
4. [Send 通路：从 UI 到 PLC 寄存器](#4-send-通路从-ui-到-plc-寄存器)
5. [PollFeedback 通路：从 PLC 寄存器到 UI](#5-pollfeedback-通路从-plc-寄存器到-ui)
6. [以"查询 Y 轴状态"为例的完整时间线](#6-以查询-y-轴状态为例的完整时间线)
7. [关键设计决策](#7-关键设计决策)

---

## 1. 架构总览

```
┌─────────────────────────────────────────────────────────────────┐
│                          UI 层 (QML)                             │
│  MainDashboard.qml / 组件 blocks                                 │
│  绑定: group_A_Y.state / group_A_Y.absPos / group_A_Y.relPos     │
└──────────────────────────┬──────────────────────────────────────┘
                           │ Qt 属性绑定（只读投影）
┌──────────────────────────▼──────────────────────────────────────┐
│                   Presentation 层 (ViewModel)                     │
│  QtAxisViewModel → AxisViewModelCore                              │
│  职责: 状态投影、编排器驱动、错误收集、tick() 帧驱动               │
└──────────────────────────┬──────────────────────────────────────┘
                           │ tryReadAxis() / tryGetAxis()
┌──────────────────────────▼──────────────────────────────────────┐
│                     Application 层 (UseCase)                      │
│  JogAxisUseCase / EnableUseCase / MoveAbsoluteUseCase ...         │
│  职责: 业务流程编排、多层级错误处理、命令生产与投递               │
└───────┬──────────────────────────────────────┬──────────────────┘
        │ 读: tryReadAxis()                    │ 写: send(SystemCommand)
        │ 领域层验证                           │ 经 ISystemDriver 接口
┌───────▼──────────────────────────────────────▼──────────────────┐
│                      Domain 层 (Entity)                           │
│  SystemContext → Axis / GantryCouplingController / EstopCtrl      │
│  职责: 领域状态机、业务规则校验、意图槽位（pending_command）      │
└──────────────────────────────┬──────────────────────────────────┘
                               │ ISystemDriver 接口
┌──────────────────────────────▼──────────────────────────────────┐
│                   Infrastructure 层 (Driver)                      │
│  ModbusSystemDriver: send() / pollFeedback()                      │
│  ├─ PlcDevice     (寄存器读写门面)                                │
│  ├─ PlcPoller     (批量轮询编排)                                  │
│  ├─ AsioModbusTcpClient (Modbus TCP 通讯)                         │
│  └─ AxisStateDeriver (多信号→统一状态融合)                       │
└──────────────────────────────┬──────────────────────────────────┘
                               │ Modbus TCP
┌──────────────────────────────▼──────────────────────────────────┐
│                      PLC 硬件 (汇川 Inovance)                     │
│  线圈(Coils M区) + 保持寄存器(Holding Registers D区)             │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 分层职责速查表

| 层 | 文件 | 在 send 中的角色 | 在 pollFeedback 中的角色 |
|---|---|---|---|
| **UI** | `MainDashboard.qml`, `blocks/*.qml` | 触发动作（按钮点击→调用 ViewModel 方法） | 绑定 ViewModel 属性，自动刷新显示 |
| **Presentation** | `QtAxisViewModel.h` | 接收 UI 调用，转发至 `AxisViewModelCore` | `tick()` 推进编排器，属性 getter 从 Domain 读取 |
| **Presentation** | `AxisViewModelCore.h/.cpp` | 创建 UseCase，调用 `execute()`；持有 Policy/Orchestrator | `tick()` 驱动 Policy/Orch 状态机；`state()/absPos()` 等读取 Axis 实体 |
| **Application** | `JogAxisUseCase.h` 等 | 三层错误处理 → 调用 `axis->jog()` → `drv->send()` | 不参与（pollFeedback 是 Infrastructure → Domain 直通） |
| **Domain** | `Axis.h/.cpp` | 状态校验 → 生产 `JogCommand` → 存入 `m_pending_intent` | `applyPlcFeedback()` 更新 `m_state`, `m_current_abs_pos` 等 |
| **Domain** | `SystemContext.h` | 提供 `tryGetAxis()` 轴访问（龙门/安全拦截）；持有 `m_driver` 指针 | 被 pollFeedback 注入到各 Controller/Entity |
| **Domain** | `SystemCommand.h` | 统一命令边界 `variant<AxisCommandWithId, Gantry*, EmergencyStop>` | 不参与 |
| **Domain** | `AxisStateDeriver.h` | 不参与 | 纯函数：5 个离散信号→1 个 `AxisState` |
| **Infrastructure** | `ISystemDriver.h` | 定义 `send(SystemCommand)→CommunicationResult` 接口 | 定义 `pollFeedback(SystemContext&)→void` 接口 |
| **Infrastructure** | `ModbusSystemDriver.h` | `send()`: variant visitor → `PlcDevice::writeBool/writeFloat` → Modbus | `pollFeedback()`: 批量读写 → snapshot → AxisStateDeriver → `axis->applyPlcFeedback()` |
| **Infrastructure** | `PlcDevice.h` | 按寄存器类型写入（Coil / Holding Register） | 提供 `readBool/readInt16/readFloat` 从 snapshot 读取 |
| **Infrastructure** | `PlcPoller.h` | 不参与 | `prepare()`: 合并寄存器到最少 Modbus 请求；`assemble()`: 响应组装为 snapshot |
| **Infrastructure** | `AsioModbusTcpClient.h` | `writeSingleCoil/writeSingleRegister` → 返回 `CommunicationResult` | `readCoils/readHoldingRegisters` 批量读取 |
| **主循环** | `main.cpp` | 不直接参与（由 UseCase 间接触发） | `QTimer(10ms)` → 遍历分组 → `drv->pollFeedback(*ctx)` |

---

## 3. 核心数据结构

### 3.1 统一命令边界 — `SystemCommand`

> 文件: `domain/command/SystemCommand.h`

```cpp
using SystemCommand = std::variant<
    AxisCommandWithId,       // 单轴命令 + 目标轴 ID
    GantryCouplingCommand,   // 龙门联动/解耦
    GantryPowerCommand,      // 龙门电机使能/掉电
    EmergencyStopCommand     // 急停触发/解除
>;

struct AxisCommandWithId {
    AxisId id;           // X, X1, X2, Y, Z, R
    AxisCommand cmd;     // variant<JogCommand, MoveCommand, EnableCommand, ...>
};
```

### 3.2 通讯结果 — `CommunicationResult`

> 文件: `infrastructure/ISystemDriver.h`

```cpp
struct CommunicationResult {
    enum class Status {
        Sent,              // 成功写入 PLC
        NetworkError,      // TCP 断连
        Timeout,           // 超时（可重试）
        Busy,              // PLC 忙（可重试）
        ProtocolError,     // Modbus 异常码
        InvalidResponse,   // 数据非法
        Disconnected       // 未连接
    };
    Status status;
    int exceptionCode;     // Modbus exception code
    std::string diagnostic; // 诊断信息
};
```

### 3.3 Axis 实体的 `pending_intent` 槽位

> 文件: `domain/entity/Axis.h`

```cpp
class Axis {
    AxisCommand m_pending_intent = std::monostate{};  // 唯一的命令意图槽位
    // ...
    bool hasPendingCommand() const;
    const AxisCommand& getPendingCommand() const;
};
```

**设计含义**：Axis 每次只保留一个待发送命令。UseCase 调用 `axis->jog(dir)` 后，JogCommand 写入 `m_pending_intent`；UseCase 随后读取并投递给 Driver。

### 3.4 ISystemDriver 接口

> 文件: `infrastructure/ISystemDriver.h`

```cpp
class ISystemDriver {
public:
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

---

## 4. Send 通路：从 UI 到 PLC 寄存器

### 4.1 完整调用链（以 Y 轴正向点动为例）

```
QML 按钮 onClicked
  │
  ▼
QtAxisViewModel::jog(Direction::Forward)         [presentation/viewmodel/QtAxisViewModel.h]
  │  转发到 m_core
  ▼
AxisViewModelCore::jog(Direction::Forward)        [presentation/viewmodel/AxisViewModelCore.cpp]
  │  创建 JogAxisUseCase，调用 execute()
  ▼
JogAxisUseCase::execute(manager, "Machine_A", AxisId::Y, Direction::Forward)
  │                                                [application/axis/JogAxisUseCase.h]
  │
  ├─ 阶段0: manager.tryGetGroup("Machine_A", ctx, reason)
  │   → 从 SystemManager 的 m_groups map 获取 SystemContext*
  │
  ├─ 阶段1: ctx->tryGetAxis(AxisId::Y, axis, reason)
  │   → SystemContext 内部:
  │     Layer 0: 安全锁定？(急停中/未同步) → 拒绝
  │     Layer 1: 龙门同步？(仅 X/X1/X2，Y 轴跳过)
  │     Layer 3: m_axes.find(AxisId::Y) → 返回 Axis*
  │
  ├─ 阶段2: axis->jog(Direction::Forward)
  │   → Axis 内部状态机校验:
  │     - 是否处于 Error/Disabled 状态？→ RejectionReason::InvalidState
  │     - 是否已在软限位边界？→ AtPositiveLimit / AtNegativeLimit
  │     - 通过 → 创建 JogCommand{dir=Forward, active=true}
  │     - 存入 m_pending_intent
  │   → 返回 true
  │
  └─ 阶段3: 消费 pending_command
      if (axis->hasPendingCommand()) {
          auto* drv = group->driver();                              // 获取 ISystemDriver*
          auto commResult = drv->send(
              AxisCommandWithId{AxisId::Y, axis->getPendingCommand()}
          );
      }
```

### 4.2 `ModbusSystemDriver::send()` 内部实现

> 文件: `infrastructure/plc/ModbusSystemDriver.h`

```cpp
CommunicationResult ModbusSystemDriver::send(const SystemCommand& cmd) {
    // 1. 检查 PlcDevice 是否已绑定
    if (!m_device) return CommunicationResult::Disconnected();

    // 2. std::visit 模式匹配命令类型
    return std::visit(overloaded{
        // === 单轴命令 ===
        [this](const AxisCommandWithId& ac) -> CommunicationResult {
            return std::visit(overloaded{
                // JogCommand → 根据方向写正/反向点动 Coil
                [this, id](const JogCommand& j) {
                    if (j.dir == Direction::Forward)
                        return m_device->writeBool(regCmdJogFwd(id), j.active);
                    else
                        return m_device->writeBool(regCmdJogBwd(id), j.active);
                },
                // EnableCommand → 写使能 Coil
                [this, id](const EnableCommand& e) {
                    return m_device->writeBool(regCmdEnable(id), e.active);
                },
                // TriggerAbsMoveCommand → 边沿触发 (ON→150ms→OFF)
                [this, id](const TriggerAbsMoveCommand&) {
                    return sendEdgeTrigger(regCmdAbsTrigger(id));
                },
                // SetAbsTargetCommand → 写 Float32 到保持寄存器
                [this, id](const SetAbsTargetCommand& t) {
                    return m_device->writeFloat(regCmdAbsTarget(id),
                                                static_cast<float>(t.target));
                },
                // ... 其他命令类型
            }, ac.cmd);
        },
        // === 龙门联动命令 ===
        [this](const GantryCouplingCommand& g) {
            return m_device->writeBool(regGantryCoupling(), g.enableCoupling);
        },
        // === 急停命令 ===
        [this](const EmergencyStopCommand& e) {
            return m_device->writeBool(regEmergencyStopTrigger(), e.active);
        }
    }, cmd);
}
```

**关键要点**：
- `send()` 通过 `std::visit` 双重模式匹配：外层匹配 `SystemCommand` variant，内层匹配 `AxisCommand` variant
- 寄存器选择器（如 `regCmdJogFwd(id)`）通过 `AxisId` switch-case 映射到正确的 PLC 地址
- 边沿触发型命令（如 ABS_MOVE_TRIGGER）走 `sendEdgeTrigger()`：先写 `true`，150ms 后在 `pollFeedback()` 中写回 `false`

### 4.3 寄存器选择器映射（以 Y 轴为例）

| 命令类型 | 选择器函数 | Y 轴 PLC 地址 |
|---|---|---|
| `JogCommand{Forward}` | `regCmdJogFwd(AxisId::Y)` | `y_axis::command::JOG_FORWARD` |
| `JogCommand{Backward}` | `regCmdJogBwd(AxisId::Y)` | `y_axis::command::JOG_BACKWARD` |
| `EnableCommand` | `regCmdEnable(AxisId::Y)` | `y_axis::command::ENABLE_REQUEST` |
| `SetAbsTargetCommand` | `regCmdAbsTarget(AxisId::Y)` | `y_axis::command::ABS_TARGET` |
| `TriggerAbsMoveCommand` | `regCmdAbsTrigger(AxisId::Y)` | `y_axis::command::ABS_MOVE_TRIGGER` |

---

## 5. PollFeedback 通路：从 PLC 寄存器到 UI

### 5.1 调度入口 — `main.cpp` 的全局 Tick Loop

```cpp
// main.cpp — 第 6 节：全局 Tick Loop
QTimer systemClock;
QObject::connect(&systemClock, &QTimer::timeout, [&]() {
    // === 6a. 物理引擎推进 ===
    for (const auto& groupName : manager.groupNames()) {
        SystemContext* ctx = nullptr;
        if (manager.tryGetGroup(groupName, ctx, r) && ctx) {
            auto* drv = ctx->driver();               // ISystemDriver*
            if (!drv) continue;

            drv->pollFeedback(*ctx);                  // ★ 核心：反馈注入

            // 消费 EmergencyStopController 产生的 pending command
            auto& estopCtrl = ctx->emergencyStopController();
            if (estopCtrl.hasPendingCommand()) {
                drv->send(estopCtrl.popPendingCommand());
            }
        }
    }

    // === 6b. ViewModel 帧驱动 ===
    for (auto* vm : allViewModels) { vm->tick(); }

    // === 6c-6d. 安全/龙门 ViewModel 推进 ===
    emergencyVM_A.tick(); emergencyVM_B.tick();
    gantryVM_A.tick();    gantryVM_B.tick();
});
systemClock.start(10);  // 10ms 物理心跳
```

**调度顺序（每 10ms）**：
1. `pollFeedback()` → PLC → Domain 实体（Axis / EmergencyStop / Gantry）
2. `ViewModel::tick()` → 推进 Policy/Orchestrator 状态机 → 消费 pending_command
3. UI 通过 Qt 属性绑定自动刷新

### 5.2 `ModbusSystemDriver::pollFeedback()` 内部实现

```cpp
void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    // ============================================
    // Step 0: 服务边沿触发队列（写 OFF 回已超时的 ON 信号）
    // ============================================
    servicePendingEdgeTriggers();

    // ============================================
    // Step 1: PlcPoller 生成批量 Modbus 请求
    //         将 RegisterRegistry 中所有寄存器合并为最少请求数
    // ============================================
    const auto req = m_poller->prepare();

    // ============================================
    // Step 2: 批量读取 Coils（线圈）
    // ============================================
    for (const auto& cr : req.coilRequests) {
        CommunicationResult result =
            m_modbusClient->readCoils(cr.range.startAddress,
                                      cr.range.count, payload);
        // 失败则标记 allCoilsOk = false
    }

    // ============================================
    // Step 3: 批量读取 Holding Registers（保持寄存器）
    // ============================================
    for (const auto& wr : req.wordRequests) {
        CommunicationResult result =
            m_modbusClient->readHoldingRegisters(wr.range.startAddress,
                                                  wr.range.count, payload);
    }

    // ============================================
    // Step 4: 组装 Snapshot + 注入 PlcDevice
    // ============================================
    auto snapshot = m_poller->assemble(coilResponses, wordResponses, timestamp);
    m_device->updateSnapshot(std::move(snapshot));

    // ============================================
    // Step 5: 逐轴读取反馈 → 状态融合 → 注入 Axis
    // ============================================
    for (AxisId id : {X, X1, X2, Y, Z, R}) {
        Axis* axis = nullptr;
        if (!ctx.tryReadAxis(id, axis, rejection)) continue;

        // 5a. 从 PlcDevice 读取各反馈寄存器
        const int16_t stateRaw  = m_device->readInt16(regFbState(id));    // D100
        const int16_t alarmCode = m_device->readInt16(regFbAlarmCode(id)); // D110
        const bool absMoving    = m_device->readBool(regFbAbsMoving(id));  // M110
        const bool relMoving    = m_device->readBool(regFbRelMoving(id));  // M111
        const bool jogging      = m_device->readBool(regFbJogging(id));    // M112
        const float absPos      = m_device->readFloat(regFbAbsPos(id));    // D120
        const float relPos      = m_device->readFloat(regFbRelPos(id));    // D122

        // 5b. 多信号融合 → 统一 AxisState
        const AxisState derivedState = deriveAxisState(
            stateRaw, alarmCode, absMoving, relMoving, jogging);

        // 5c. 注入 Axis 实体
        axis->applyPlcFeedback(derivedState,
                               static_cast<double>(absPos),
                               static_cast<double>(relPos));
    }

    // ============================================
    // Step 6: 急停状态注入
    // ============================================
    const bool estopActive = m_device->readBool(regFbEmergencyStopActive());
    ctx.emergencyStopController().applyFeedback(estopActive);

    // ============================================
    // Step 7: 龙门状态注入
    // ============================================
    const bool gantryEnabled = m_device->readInt16(regFbState(AxisId::X)) != 0;
    const bool gantryCoupled = m_device->readBool(regFbLinkageState());
    const int  gantryErrCode = m_device->readInt16(regFbGantryErrorCode());
    ctx.gantryCouplingController().applyFeedback({gantryEnabled, gantryCoupled, gantryErrCode});
    ctx.gantryPowerController().applyFeedback({gantryEnabled, gantryCoupled, gantryErrCode});
}
```

### 5.3 状态融合引擎 — `AxisStateDeriver`

> 文件: `infrastructure/plc/AxisStateDeriver.h`

**输入**：5 个 PLC 离散信号

| 信号 | 来源 | Y 轴地址（示例） | 类型 |
|---|---|---|---|
| `d100State` | D 寄存器 STATE | D101 | Int16 (0=未使能, 1=空闲, 2=运动中, 3=报警) |
| `alarmCode` | D 寄存器 ALARM_CODE | D111 | Int16 (0=正常) |
| `absMoving` | M 线圈 ABS_MOVING | M113 | Bool |
| `relMoving` | M 线圈 REL_MOVING | M114 | Bool |
| `jogging` | M 线圈 JOGGING | M115 | Bool |

**推导优先级链**：

```
d100State == 3 || alarmCode != 0  ──→  AxisState::Error        (最高优先级)
d100State == 0                     ──→  AxisState::Disabled
absMoving                          ──→  AxisState::MovingAbsolute
relMoving                          ──→  AxisState::MovingRelative
jogging                            ──→  AxisState::Jogging
其他                                ──→  AxisState::Idle          (最低优先级)
```

### 5.4 领域实体接收 — `Axis::applyPlcFeedback()`

> 文件: `domain/entity/Axis.cpp`

```cpp
void Axis::applyPlcFeedback(AxisState state, double absPos, double relPos) {
    m_plcState = state;           // 仅记录，用于诊断
    m_state = state;              // ★ 直接采纳派生状态为权威状态
    m_current_abs_pos = absPos;   // ★ 更新绝对位置
    m_current_rel_pos = relPos;   // ★ 更新相对位置
}
```

**设计要点**：
- `applyPlcFeedback()` 是精简版反馈接口，专为 `pollFeedback()` 设计
- 直接采纳派生状态（不做二次校验），因为 `AxisStateDeriver` 的融合逻辑已经过 TDD 验证
- 域的 `AxisFeedback` 结构体（14 字段）用于更丰富的测试/模拟场景

### 5.5 ViewModel 读取 — 从 Domain 到 UI

```cpp
// AxisViewModelCore 的属性 getter
AxisState AxisViewModelCore::state() const {
    SystemContext* ctx = nullptr;
    if (!m_manager.tryGetGroup(m_groupName, ctx, r)) return AxisState::Unknown;
    Axis* axis = nullptr;
    if (!ctx->tryReadAxis(m_axisId, axis, r)) return AxisState::Unknown;  // ★ 使用 tryReadAxis（绕过安全锁定）
    return axis->state();
}

double AxisViewModelCore::absPos() const {
    // 同样的模式：tryGetGroup → tryReadAxis → axis->currentAbsolutePosition()
}

double AxisViewModelCore::relPos() const {
    // tryGetGroup → tryReadAxis → axis->currentRelativePosition()
}
```

**关键区分**：
- `tryReadAxis()`：遥测读取，绕过安全锁定（急停中仍可读位置/状态）
- `tryGetAxis()`：控制访问，受安全锁定拦截（急停中拒绝一切控制操作）

---

## 6. 以"查询 Y 轴状态"为例的完整时间线

```
时间轴（每 10ms 一个周期）
═══════════════════════════════════════════════════════════════════

T0: QTimer(10ms) 触发
│
├─ [6a] 遍历分组 → Machine_A
│   │
│   └─ driverA.pollFeedback(ctxA)
│       │
│       ├─ Step 0: servicePendingEdgeTriggers()
│       │   └─ 检查是否有边沿触发的 ON 信号需要写回 OFF
│       │
│       ├─ Step 1: m_poller->prepare()
│       │   └─ 分析 RegisterRegistry，合并为最少 Modbus 请求
│       │      例: M110~M121 合并为 1 次 readCoils(start=110, count=12)
│       │          D100~D103 合并为 1 次 readHoldingRegisters(start=100, count=4)
│       │
│       ├─ Step 2-3: m_modbusClient->readCoils / readHoldingRegisters
│       │   └─ 通过 AsioModbusTcpClient 发送 Modbus TCP 帧到 192.168.1.88:502
│       │   └─ 收到响应字节流
│       │
│       ├─ Step 4: m_poller->assemble() → PlcDevice snapshot
│       │   └─ 原始字节 → 按寄存器地址解析为 PlcValue (bool/int16/float32)
│       │
│       ├─ Step 5: 逐轴注入 (Y 轴)
│       │   │
│       │   ├─ ctx.tryReadAxis(AxisId::Y, axis, rejection)
│       │   │   └─ Y 轴非龙门轴，跳过龙门语义拦截
│       │   │   └─ m_axes.find(AxisId::Y) → 返回 Axis*
│       │   │
│       │   ├─ m_device->readInt16(regFbState(AxisId::Y))
│       │   │   └─ regFbState(Y) → y_axis::feedback::STATE (D101)
│       │   │   └─ 从 snapshot[addr=101] 读取 → 比方说是 2 (运动中)
│       │   │
│       │   ├─ m_device->readInt16(regFbAlarmCode(AxisId::Y))    → 0
│       │   ├─ m_device->readBool(regFbAbsMoving(AxisId::Y))     → false
│       │   ├─ m_device->readBool(regFbRelMoving(AxisId::Y))     → false
│       │   ├─ m_device->readBool(regFbJogging(AxisId::Y))       → true
│       │   ├─ m_device->readFloat(regFbAbsPos(AxisId::Y))       → 41.2
│       │   ├─ m_device->readFloat(regFbRelPos(AxisId::Y))       → -5.8
│       │   │
│       │   ├─ deriveAxisState(2, 0, false, false, true)
│       │   │   └─ 优先级: !Error → !Disabled → !MovingAbs → !MovingRel
│       │   │   └─ jogging=true → 返回 AxisState::Jogging
│       │   │
│       │   └─ axis->applyPlcFeedback(Jogging, 41.2, -5.8)
│       │       └─ m_state = Jogging
│       │       └─ m_current_abs_pos = 41.2
│       │       └─ m_current_rel_pos = -5.8
│       │
│       ├─ Step 6: 急停状态注入
│       │   └─ m_device->readBool(ESTOP_ACTIVE) → false
│       │   └─ ctx.emergencyStopController().applyFeedback(false)
│       │
│       └─ Step 7: 龙门状态注入
│           └─ ctx.gantryCouplingController/PowerController.applyFeedback(...)
│
├─ [6b] ViewModel::tick() (qtVM_A_Y)
│   │
│   └─ AxisViewModelCore::tick()
│       ├─ 推进 JogOrchestrator / AbsMoveOrchestrator 等状态机
│       ├─ collectOrchError() — 收集编排器错误
│       └─ consumePendingCommands() — 消费 pending_command（如有）
│
└─ Qt 属性绑定自动刷新
    │
    └─ QML 绑定: group_A_Y.state → "Jogging"
                  group_A_Y.absPos → 41.2
                  group_A_Y.relPos → -5.8
                  group_A_Y.stateText → "Jogging(3)"
                  → UI 刷新显示
```

---

## 7. 关键设计决策

### 7.1 统一命令总线 vs 分轴命令接口

**决策**：使用 `SystemCommand` variant 统一所有命令类型，通过单一 `send()` 接口投递。

**理由**：
- Driver 只需实现一个 `send()` 方法，不需要为每种命令类型提供独立方法
- 命令路由通过 `AxisCommandWithId.id` 在 Driver 内部完成（寄存器选择器 switch-case）
- 新增命令类型只需扩展 variant，Driver 的 visitor 模式自动要求覆盖

### 7.2 send 只表达通讯结果，不表达执行结果

**决策**：`send()` 返回 `CommunicationResult`，只表达"帧是否成功写入 PLC 寄存器"。

**理由**：
- PLC 是否执行命令，由 `pollFeedback()` 通过读取状态寄存器来确认
- 命令投递和结果确认是两个独立的时间点，不应混淆
- `CommunicationResult` 的 `retryable()` 方法支持工业现场的重试决策

### 7.3 pollFeedback 是主动拉取，不是事件推送

**决策**：主循环每 10ms 主动调用 `pollFeedback()`，而非 PLC 推送。

**理由**：
- Modbus TCP 是请求-响应协议，天然不支持推送
- 固定周期轮询保证了状态读取的确定性时序
- 所有领域实体在一次轮询中同步更新，避免状态不一致

### 7.4 多信号融合为单一 AxisState

**决策**：不直接用 `D100 STATE` 寄存器值，而是融合 5 个信号推导统一状态。

**理由**：
- `D100 STATE` 只有 0~3 四个值，粒度不足（无法区分 Jogging/AbsMoving/RelMoving）
- 独立的 Coil 信号（ABS_MOVING, REL_MOVING, JOGGING）提供精确运动子状态
- `AxisStateDeriver` 纯函数保证确定性，易于 TDD 验证

### 7.5 tryReadAxis vs tryGetAxis 的双通道设计

**决策**：`SystemContext` 提供两个轴访问入口。

| 方法 | 用途 | 安全锁定拦截 | 使用场景 |
|---|---|---|---|
| `tryGetAxis()` | 控制操作 | ✅ 拦截 | UseCase 执行前的轴获取 |
| `tryReadAxis()` | 遥测读取 | ❌ 绕过 | pollFeedback 和 ViewModel 读取 |

**理由**：
- 急停期间控制操作必须被拒绝，但位置/状态遥测仍需可读
- 避免 UI 在急停期间显示"无数据"或闪烁

### 7.6 边沿触发协议（ManualResetEdgeTrigger）

**决策**：触发型命令（如 ABS_MOVE_TRIGGER）使用 ON→150ms→OFF 的边沿协议。

**理由**：
- PLC 需要上升沿触发，持续 ON 会导致重复触发
- 150ms 脉冲宽度由 `PendingEdge` 队列管理
- `servicePendingEdgeTriggers()` 在每次 `pollFeedback()` 开头执行

### 7.7 ViewModel 持有 Policy/Orchestrator 而非 UseCase

**决策**：`AxisViewModelCore` 通过 `JogOrchestrator` / `AbsMovePolicy` 等编排器执行复杂业务流程，而非直接持有 UseCase。

**理由**：
- 编排器封装多步业务流程的状态机（如"设置 target→等待→触发移动→等待完成"）
- UseCase 只负责单步操作的验证+命令生产
- ViewModel 的 `tick()` 推进编排器状态机，实现异步业务流程

---

## 附录：文件索引

| 层 | 文件 | 说明 |
|---|---|---|
| Domain | `domain/entity/Axis.h` | 轴实体：状态机 + pending_command 槽位 |
| Domain | `domain/entity/Axis.cpp` | `applyPlcFeedback()` / `jog()` 实现 |
| Domain | `domain/entity/AxisId.h` | AxisId 枚举 (X, X1, X2, Y, Z, R) |
| Domain | `domain/entity/SystemContext.h` | 分组上下文：6 轴容器、龙门/安全控制器、tryGetAxis/tryReadAxis |
| Domain | `domain/entity/ContextRejection.h` | 轴访问拒绝原因枚举 |
| Domain | `domain/command/SystemCommand.h` | 统一命令边界 (variant) |
| Domain | `domain/safety/EmergencyStopController.h` | 急停状态机 + pending command 生产 |
| Domain | `domain/gantry/GantryCouplingController.h` | 龙门联动状态机 |
| Domain | `domain/gantry/GantryPowerController.h` | 龙门电机使能状态机 |
| Infrastructure | `infrastructure/ISystemDriver.h` | send/pollFeedback 接口 + CommunicationResult |
| Infrastructure | `infrastructure/plc/ModbusSystemDriver.h` | Modbus 驱动实现（header-only） |
| Infrastructure | `infrastructure/plc/AxisStateDeriver.h` | 多信号融合状态推导（纯函数） |
| Infrastructure | `infrastructure/plc/protocol/PlcDevice.h` | 寄存器读写门面 |
| Infrastructure | `infrastructure/plc/protocol/PlcPoller.h` | 批量轮询编排 (prepare/assemble) |
| Infrastructure | `infrastructure/plc/protocol/AsioModbusTcpClient.h` | Modbus TCP 异步客户端 |
| Infrastructure | `infrastructure/plc/protocol/RegisterRegistry.h` | 寄存器注册表（声明哪些地址需要轮询） |
| Infrastructure | `infrastructure/plc/protocol/RegisterAddressAll.h` | 所有 PLC 地址常量定义 |
| Application | `application/SystemManager.h` | 分组注册表 (名称→SystemContext) |
| Application | `application/axis/JogAxisUseCase.h` | 点动用例：三层错误处理 + 命令投递 |
| Application | `application/axis/EnableUseCase.h` | 使能用例 |
| Application | `application/axis/MoveAbsoluteUseCase.h` | 绝对定位用例 |
| Application | `application/axis/MoveRelativeUseCase.h` | 相对定位用例 |
| Application | `application/axis/StopAxisUseCase.h` | 停止用例 |
| Application | `application/axis/AxisSyncService.h` | Feedback→Axis 同步服务 |
| Application | `application/policy/AbsMovePolicy.h` | 绝对定位多步编排策略 |
| Application | `application/policy/RelMovePolicy.h` | 相对定位多步编排策略 |
| Application | `application/policy/JogOrchestrator.h` | 点动编排器 |
| Application | `application/policy/AutoAbsMoveOrchestrator.h` | 绝对移动自编排器 |
| Application | `application/policy/AutoRelMoveOrchestrator.h` | 相对移动自编排器 |
| Application | `application/policy/GantryOrchestrator.h` | 龙门联动编排器 |
| Application | `application/safety/EmergencyStopUseCase.h` | 急停触发用例 |
| Application | `application/safety/ReleaseEmergencyStopUseCase.h` | 急停解除用例 |
| Presentation | `presentation/viewmodel/QtAxisViewModel.h` | Qt 属性包装（Q_PROPERTY 暴露给 QML） |
| Presentation | `presentation/viewmodel/AxisViewModelCore.h` | ViewModel 核心：状态投影 + Policy/Orch 持有 |
| Presentation | `presentation/viewmodel/AxisViewModelCore.cpp` | tick() / jog() / enable() 实现 |
| Presentation | `presentation/viewmodel/EmergencyStopViewModel.h` | 急停 ViewModel |
| Presentation | `presentation/viewmodel/GantryViewModel.h` | 龙门 ViewModel |
| Presentation | `presentation/viewmodel/ErrorTranslator.h` | UseCaseError→ViewModelError 翻译 |
| Entry Point | `main.cpp` | 依赖注入组装 + 全局 Tick Loop 调度 |
