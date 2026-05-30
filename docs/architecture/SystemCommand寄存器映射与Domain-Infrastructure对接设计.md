# SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计

> 状态：设计文档 v4.0  
> 日期：2026-05-30  
> 项目：servoV6

---

## 目录

1. [问题定义与当前状态](#1-问题定义与当前状态)
2. [SystemCommand ↔ 寄存器映射总表](#2-systemcommand--寄存器映射总表)
3. [边沿触发协议](#3-边沿触发协议)
4. [ModbusSystemDriver 实现设计](#4-modbussystemdriver-实现设计)
5. [集成策略：替换 FakePLC](#5-集成策略替换-fakeplc)
6. [实施计划](#6-实施计划)
7. [附录：完整寄存器清单](#7-附录完整寄存器清单)

---

## 1. 问题定义与当前状态

### 1.1 已完成的工作

```
RegisterAddressAll.h ✅ — 所有控制/状态寄存器已按真实 PLC 地址表声明
  ├── plc::reg::x_axis::{command, feedback}   (M0/M4/.../D1000/D1002/.../D120/D122/...)
  ├── plc::reg::y_axis::{command, feedback}   (M1/.../D1004/D1006/.../D124/D126/...)
  ├── plc::reg::z_axis::{command, feedback}   (M2/.../D1008/D1010/.../D128/D130/...)
  ├── plc::reg::r_axis::{command, feedback}   (M3/.../D1012/D1014/.../D132/D134/...)
  └── plc::reg::system_global::{command, feedback}  (M80/M125/M130/D180)

Protocol Runtime ✅ — RegisterRegistry / PlcPoller / PlcSnapshot / RegisterCodec / PlcDevice
IModbusClient ✅ — AsioModbusTcpClient

待完成: ModbusSystemDriver — 将 RegisterAddressAll 常量 + PlcDevice + IModbusClient
          组装成一个 ISystemDriver 实现，替换 FakeAxisDriver
```

### 1.2 真实 PLC 寄存器约定

| PLC 软元件 | Modbus 区域 | Modbus 地址 | 示例 |
|-----------|------------|------------|------|
| M (位) | Coil | 编号即地址 | M1 → Coil 地址 1 |
| D (字) | Holding Register | 编号即地址 | D1000 → HoldingReg 地址 1000 |

无需额外偏移，PLC 软元件编号 = Modbus 地址。

### 1.3 本版本范围约束

> ⚠️ **本版本暂时屏蔽 X1、X2 单独控制。**  
> X 轴仅以龙门结构（X1+X2 联动）进行运动控制，不开放单 X1 或单 X2 的独立操作。  
> Y、Z、R 轴不受影响，正常支持单轴控制。

因此以下寄存器在本版本中**不注册、不使用**：
- `M52` (X2_JOG_FORWARD) / `M53` (X2_JOG_BACKWARD)
- `M70` (X1_ABS_MOVE_TRIGGER) / `M71` (X2_ABS_MOVE_TRIGGER)
- `D40` (X1_ABS_TARGET) / `D42` (X2_ABS_TARGET)
- `M10` / `M11` / `M12` / `M13` (HOME_TRIGGER) — 本版本不开放回原点操作

X 轴的所有运动命令通过龙门统一寄存器下发：
- Jog：`M50` (X1_JOG_FORWARD) / `M51` (X1_JOG_BACKWARD) — PLC 侧自动同步 X2
- Move：`D20` (ABS_TARGET) / `D22` (REL_TARGET) + `M40` (ABS_MOVE_TRIGGER) / `M41` (REL_MOVE_TRIGGER) — PLC 侧自动同步 X1/X2

---

## 2. SystemCommand ↔ 寄存器映射总表

### 2.1 轴命令映射

#### 2.1.1 Level 型命令（持续电平）

| SystemCommand 变体 | X 轴（龙门）寄存器 | Y 轴寄存器 | Z 轴寄存器 | R 轴寄存器 | 类型 |
|---------------------|-------------------|-----------|-----------|-----------|------|
| **EnableCommand** | `ENABLE_REQUEST` (M0) | `ENABLE_REQUEST` (M1) | `ENABLE_REQUEST` (M2) | `ENABLE_REQUEST` (M3) | Bool / Level |
| **JogCommand (Fwd)** | `X1_JOG_FORWARD` (M50) | `JOG_FORWARD` (M54) | `JOG_FORWARD` (M56) | `JOG_FORWARD` (M58) | Bool / Level |
| **JogCommand (Bwd)** | `X1_JOG_BACKWARD` (M51) | `JOG_BACKWARD` (M55) | `JOG_BACKWARD` (M57) | `JOG_BACKWARD` (M59) | Bool / Level |
| **SetJogVelocity** | `JOG_SPEED` (D1000) | `JOG_SPEED` (D1004) | `JOG_SPEED` (D1008) | `JOG_SPEED` (D1012) | Float32 / Level |
| **SetMoveVelocity** | `MOVE_SPEED` (D1002) | `MOVE_SPEED` (D1006) | `MOVE_SPEED` (D1010) | `MOVE_SPEED` (D1014) | Float32 / Level |

#### 2.1.2 EdgeTrigger 型命令（脉冲触发，ON→150ms→OFF）

| SystemCommand 变体 | X 轴（龙门） | Y 轴 | Z 轴 | R 轴 |
|---------------------|------------|------|------|------|
| **SetRelativeZero** | `SET_REL_ZERO` (M14) | `SET_REL_ZERO` (M15) | `SET_REL_ZERO` (M16) | `SET_REL_ZERO` (M17) |
| **ClearRelativeZero** | `CLEAR_REL_ZERO` (M18) | `CLEAR_REL_ZERO` (M19) | `CLEAR_REL_ZERO` (M20) | `CLEAR_REL_ZERO` (M21) |
| **ZeroAbsoluteCommand** (清空绝对位置) | `CLEAR_ABS_POS` (M30) | `CLEAR_ABS_POS` (M31) | `CLEAR_ABS_POS` (M32) | `CLEAR_ABS_POS` (M33) |

#### 2.1.3 绝对/相对定位四命令独立映射

PLC 的运动模式是**参数预置-触发执行**：先将目标位置写入 D 寄存器（预设参数），再通过边沿触发信号让 PLC 按已设置的目标开始运动。真实 PLC 中绝对定位和相对定位使用**四组完全独立的寄存器**：

| 操作 | 寄存器 | 含义 |
|------|--------|------|
| 设置绝对移动位置 | `D20/D24/D28/D32` | **ABS_TARGET** — 仅写目标位置，不触发运动 |
| 触发绝对位置移动 | `M40/M42/M44/M46` | **ABS_MOVE_TRIGGER** — 边沿脉冲触发已设置的目标 |
| 设置相对移动距离 | `D22/D26/D30/D34` | **REL_TARGET** — 仅写移动距离，不触发运动 |
| 触发相对位置移动 | `M41/M43/M45/M47` | **REL_MOVE_TRIGGER** — 边沿脉冲触发已设置的距离 |

Domain 层对应四个独立命令（`MoveCommand` 已废弃）：

| Domain 命令 | Domain 接口 | PLC 操作 | 寄存器 |
|-------------|------------|---------|--------|
| **SetAbsTargetCommand** | `Axis::setAbsTarget(target)` | writeFloat(ABS_TARGET, target) | D20/D24/D28/D32 |
| **TriggerAbsMoveCommand** | `Axis::triggerAbsMove()` | sendEdgeTrigger(ABS_MOVE_TRIGGER) | M40/M42/M44/M46 |
| **SetRelTargetCommand** | `Axis::setRelTarget(distance)` | writeFloat(REL_TARGET, distance) | D22/D26/D30/D34 |
| **TriggerRelMoveCommand** | `Axis::triggerRelMove()` | sendEdgeTrigger(REL_MOVE_TRIGGER) | M41/M43/M45/M47 |

```cpp
// ===== 设置绝对目标（仅写 D 寄存器，不触发运动） =====
CommunicationResult sendSetAbsTarget(AxisId id, const SetAbsTargetCommand& cmd) {
    return m_device.writeFloat(regCmdAbsTarget(id), static_cast<float>(cmd.target));
}

// ===== 触发绝对移动（仅发 M 寄存器 EdgeTrigger） =====
CommunicationResult sendTriggerAbsMove(AxisId id) {
    return sendEdgeTrigger(regCmdAbsTrigger(id));   // M40/M42/M44/M46
}

// ===== 设置相对目标（仅写 D 寄存器，不触发运动） =====
CommunicationResult sendSetRelTarget(AxisId id, const SetRelTargetCommand& cmd) {
    return m_device.writeFloat(regCmdRelTarget(id), static_cast<float>(cmd.distance));
}

// ===== 触发相对移动（仅发 M 寄存器 EdgeTrigger） =====
CommunicationResult sendTriggerRelMove(AxisId id) {
    return sendEdgeTrigger(regCmdRelTrigger(id));   // M41/M43/M45/M47
}
```

> **设计原则**：每个命令只做一件事，PLC 和 Domain 层一一对应。`SetAbsTargetCommand` / `SetRelTargetCommand` 仅写 D 寄存器（Level 型），`TriggerAbsMoveCommand` / `TriggerRelMoveCommand` 仅发 M 寄存器 EdgeTrigger。`MoveCommand`（含 `MoveType` 字段的两阶段命令）已废弃，不再使用。

#### 2.1.4 StopCommand

将当前轴 Jog Forward + Jog Backward 两个 Coil 都写 OFF：

```cpp
m_device.writeBool(regCmdJogFwd(id), false);
m_device.writeBool(regCmdJogBwd(id), false);
```

### 2.2 组级命令映射

| SystemCommand 变体 | 寄存器 | 类型 | 行为 |
|---------------------|--------|------|------|
| **GantryCouplingCommand** | `LINKAGE_ENABLE` (M4) | Bool / Level | `cmd.enableCoupling` → ON/OFF |
| **GantryPowerCommand** | `ENABLE_REQUEST` (M0) | Bool / Level | `cmd.enable` → ON/OFF |
| **EmergencyStopCommand** (触发) | `ESTOP_TRIGGER` (M80) | Bool / Level | `cmd.active=true` → ON，持续急停 |
| **EmergencyStopCommand** (解除) | `ESTOP_TRIGGER` (M80) | Bool / Level | `cmd.active=false` → OFF |

**GantryPowerCommand = ENABLE_REQUEST (M0)**：M0 的物理含义是"X1/X2 同步使能上电"，GantryPowerCommand 和 `AxisCommandWithId(AxisId::X, EnableCommand)` 在 PLC 层面是同一操作。两个 SystemCommand 变体映射到同一寄存器。

**EmergencyStop 是 Level 型**：ESTOP_TRIGGER (M80) 是持续电平信号，不是边沿触发。ON = 急停中，OFF = 解除。直接根据 `EmergencyStopCommand.active` 写入电平，不需要脉冲。

### 2.3 反馈寄存器映射

#### 2.3.1 轴反馈

`AxisFeedback` 只包含最终推导结果，不暴露底层离散信号。**派生信号**（`MOVE_DONE`、`ABS_MOVING`、`REL_MOVING`、`JOGGING`、`ALARM_CODE`）在 `deriveAxisState()` 内部融合使用，不对 Domain 层直接可见。

| AxisFeedback 字段 | 直接读取寄存器 | 备注 |
|-------------------|----------------|------|
| **state** | — | 多信号融合推导（见 §4.8.3），不直接映射 |
| **absPos** | `ABS_POSITION` (D120/D124/D128/D132) | Float32 |
| **relPos** | `REL_POSITION` (D122/D126/D130/D134) | Float32 |
| **relZeroAbsPos** | — | Domain 层闭环计算（SetRelativeZero），不读取 |
| **posLimit** | `X1_SOFT_LIMIT_POS` (D150) / Y(D154) / Z(D158) | Float32，与当前位置比对 |
| **negLimit** | `X1_SOFT_LIMIT_NEG` (D152) / Y(D156) / Z(D160) | Float32，与当前位置比对 |
| **getjogVelocity** | — | SetVelocity 闭环确认，此处无需回读 |
| **getMoveVelocity** | — | SetVelocity 闭环确认，此处无需回读 |

**deriveAxisState() 内部使用的离散信号**（不写入 AxisFeedback）：

| 信号 | X 轴 (龙门/X1) | Y 轴 | Z 轴 | R 轴 | 类型 |
|------|---------------|------|------|------|------|
| STATE (D100) | D100 | D101 | D102 | D103 | Int16 |
| ABS_MOVING | M110 | M113 | M116 | M119 | Bool |
| REL_MOVING | M111 | M114 | M117 | M120 | Bool |
| JOGGING | M112 | M115 | M118 | M121 | Bool |
| ALARM_CODE | D110 | D111 | D112 | D113 | Int16 |

> 龙门 X 轴反馈以 X1 寄存器为准。X2 反馈暂不读取。MOVE_DONE (M100) 不参与状态推导，仅用于闭环确认。

#### 2.3.2 系统级反馈

| 域字段 | 寄存器 | 类型 |
|--------|--------|------|
| **龙门联动状态** | `LINKAGE_STATE` (M125) | Bool |
| **龙门 ErrorCode** | `GANTRY_ERROR_CODE` (D180) | Int16 |
| **急停中** | `ESTOP_ACTIVE` (M130) | Bool |

---

## 3. 边沿触发协议

### 3.1 脉冲时序

```
1. writeBool(reg, true)   → 立即生效
2. 加入挂起队列，记录时间戳
3. 等待 EDGE_TRIGGER_PULSE_MS (150ms)，由 pollFeedback 中非阻塞调度
4. writeBool(reg, false)  → 在 servicePendingEdgeTriggers() 中执行
```

```cpp
static constexpr int EDGE_TRIGGER_PULSE_MS = 150;
```

### 3.2 需要 EdgeTrigger 的命令

| 命令 | X 寄存器 | Y/Z/R 对应 |
|------|---------|-----------|
| ZeroAbsoluteCommand | CLEAR_ABS_POS (M30) | M31/M32/M33 |
| SetRelativeZero | SET_REL_ZERO (M14) | M15/M16/M17 |
| ClearRelativeZero | CLEAR_REL_ZERO (M18) | M19/M20/M21 |
| TriggerAbsMoveCommand | ABS_MOVE_TRIGGER (M40) | M42/M44/M46 |
| TriggerRelMoveCommand | REL_MOVE_TRIGGER (M41) | M43/M45/M47 |

> ⚠️ 本版本不开放回原点操作，HOME_TRIGGER (M10/M11/M12/M13) 不注册、不使用。

### 3.3 Level 型命令（不需要 EdgeTrigger）

| 命令 | 原因 |
|------|------|
| **Enable** | 持续 ON/OFF 电平 |
| **Jog** | ON=点动, OFF=停止，PLC 自停 |
| **SetVelocity** | 参数型持续写入 |
| **EmergencyStop** | Level 型：ON=急停中, OFF=解除 |
| **GantryCoupling** | Level 型：ON=联动, OFF=解耦 |
| **GantryPower** | Level 型：ON=使能, OFF=掉电 (与 M0 共用) |
| **SetAbsTarget** | 仅写 D 寄存器目标值，不触发运动 |
| **SetRelTarget** | 仅写 D 寄存器距离值，不触发运动 |

---

## 4. ModbusSystemDriver 实现设计

### 4.1 类声明

```cpp
#pragma once
#include "ISystemDriver.h"
#include "plc/protocol/PlcDevice.h"
#include "plc/protocol/PlcPoller.h"
#include "plc/protocol/RegisterRegistry.h"
#include "plc/protocol/RegisterAddressAll.h"
#include "plc/protocol/IModbusClient.h"
#include "domain/command/SystemCommand.h"
#include "domain/entity/AxisId.h"
#include "domain/entity/SystemContext.h"
#include <vector>
#include <chrono>

namespace plc {

class ModbusSystemDriver : public ISystemDriver {
public:
    explicit ModbusSystemDriver(const plc::protocol::ProtocolProfile& profile);

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;

    CommunicationResult connect(const std::string& host, uint16_t port);
    void disconnect();

private:
    void buildRegisterRegistry();

    // 命令分派
    CommunicationResult sendAxisCommand(AxisId id, const AxisCommand& cmd);
    CommunicationResult sendGantryCoupling(const GantryCouplingCommand& cmd);
    CommunicationResult sendGantryPower(const GantryPowerCommand& cmd);
    CommunicationResult sendEmergencyStop(const EmergencyStopCommand& cmd);

    // EdgeTrigger
    CommunicationResult sendEdgeTrigger(const RegisterInfo& reg);
    CommunicationResult sendSetAbsTarget(AxisId id, const SetAbsTargetCommand& cmd);
    CommunicationResult sendTriggerAbsMove(AxisId id);
    CommunicationResult sendSetRelTarget(AxisId id, const SetRelTargetCommand& cmd);
    CommunicationResult sendTriggerRelMove(AxisId id);

    // 反馈
    void servicePendingEdgeTriggers();
    void readAxisFeedback(AxisId id, SystemContext& ctx);
    void readSystemFeedback(SystemContext& ctx);

    // AxisId → 寄存器引用
    const protocol::RegisterInfo& regCmdEnable(AxisId id) const;
    const protocol::RegisterInfo& regCmdJogFwd(AxisId id) const;
    const protocol::RegisterInfo& regCmdJogBwd(AxisId id) const;
    const protocol::RegisterInfo& regCmdAbsTarget(AxisId id) const;
    const protocol::RegisterInfo& regCmdRelTarget(AxisId id) const;
    const protocol::RegisterInfo& regCmdAbsTrigger(AxisId id) const;
    const protocol::RegisterInfo& regCmdRelTrigger(AxisId id) const;
    const protocol::RegisterInfo& regCmdSetRelZero(AxisId id) const;
    const protocol::RegisterInfo& regCmdClearRelZero(AxisId id) const;
    const protocol::RegisterInfo& regCmdClearAbsPos(AxisId id) const;
    const protocol::RegisterInfo& regCmdJogSpeed(AxisId id) const;
    const protocol::RegisterInfo& regCmdMoveSpeed(AxisId id) const;

    const protocol::RegisterInfo& regFbState(AxisId id) const;
    const protocol::RegisterInfo& regFbMoveDone(AxisId id) const;
    const protocol::RegisterInfo& regFbJogging(AxisId id) const;
    const protocol::RegisterInfo& regFbAbsMoving(AxisId id) const;
    const protocol::RegisterInfo& regFbRelMoving(AxisId id) const;
    const protocol::RegisterInfo& regFbAbsPos(AxisId id) const;
    const protocol::RegisterInfo& regFbRelPos(AxisId id) const;
    const protocol::RegisterInfo& regFbAlarmCode(AxisId id) const;

private:
    static constexpr int EDGE_TRIGGER_PULSE_MS = 150;

    protocol::ProtocolProfile m_profile;
    protocol::RegisterRegistry m_registry;
    protocol::PlcDevice m_device;
    protocol::PlcPoller m_poller;
    IModbusClient* m_client;

    struct PendingEdge {
        protocol::RegisterInfo reg;
        std::chrono::steady_clock::time_point onTime;
        bool offSent = false;
    };
    std::vector<PendingEdge> m_pendingEdges;
};

} // namespace plc
```

### 4.2 buildRegisterRegistry — 寄存器注册

```cpp
void ModbusSystemDriver::buildRegisterRegistry() {
    using namespace plc::reg;
    using namespace plc::protocol;

    // === System Global ===
    m_registry.add(system_global::command::ESTOP_TRIGGER);       // M80
    m_registry.add(system_global::feedback::ESTOP_ACTIVE);       // M130
    m_registry.add(system_global::feedback::GANTRY_ERROR_CODE);  // D180

    // === X Axis（龙门结构，仅龙门统一寄存器；不含 HOME_TRIGGER M10） ===
    m_registry.add(x_axis::command::ENABLE_REQUEST);      // M0
    m_registry.add(x_axis::command::LINKAGE_ENABLE);      // M4
    m_registry.add(x_axis::command::SET_REL_ZERO);        // M14
    m_registry.add(x_axis::command::CLEAR_REL_ZERO);      // M18
    m_registry.add(x_axis::command::CLEAR_ABS_POS);       // M30
    m_registry.add(x_axis::command::ABS_MOVE_TRIGGER);    // M40
    m_registry.add(x_axis::command::REL_MOVE_TRIGGER);    // M41
    m_registry.add(x_axis::command::X1_JOG_FORWARD);      // M50
    m_registry.add(x_axis::command::X1_JOG_BACKWARD);     // M51
    m_registry.add(x_axis::command::ABS_TARGET);          // D20
    m_registry.add(x_axis::command::REL_TARGET);          // D22
    m_registry.add(x_axis::command::JOG_SPEED);           // D1000
    m_registry.add(x_axis::command::MOVE_SPEED);          // D1002
    // X feedback
    m_registry.add(x_axis::feedback::MOVE_DONE);          // M100
    m_registry.add(x_axis::feedback::ABS_MOVING);         // M110
    m_registry.add(x_axis::feedback::REL_MOVING);         // M111
    m_registry.add(x_axis::feedback::JOGGING);            // M112
    m_registry.add(x_axis::feedback::LINKAGE_STATE);      // M125
    m_registry.add(x_axis::feedback::STATE);              // D100
    m_registry.add(x_axis::feedback::ALARM_CODE);         // D110
    m_registry.add(x_axis::feedback::ABS_POSITION);       // D120
    m_registry.add(x_axis::feedback::REL_POSITION);       // D122
    m_registry.add(x_axis::feedback::X1_SOFT_LIMIT_POS);  // D150
    m_registry.add(x_axis::feedback::X1_SOFT_LIMIT_NEG);  // D152

    // === Y Axis（不含 HOME_TRIGGER M11） ===
    // ... (M1, M15, M19, M31, M42, M43, M54, M55, D24, D26, D1004, D1006,
    //       M101, M113-M115, D101, D111, D124, D126, D154, D156)

    // === Z Axis（不含 HOME_TRIGGER M12） ===
    // ... (M2, M16, M20, M32, M44, M45, M56, M57, D28, D30, D1008, D1010,
    //       M102, M116-M118, D102, D112, D128, D130, D158, D160)

    // === R Axis（不含 HOME_TRIGGER M13） ===
    // ... (M3, M17, M21, M33, M46, M47, M58, M59, D32, D34, D1012, D1014,
    //       M103, M119-M121, D103, D113, D132, D134)
}
```

> Y/Z/R 轴寄存器注册与 X 轴结构完全对称，此处省略重复代码。实现时参照 X 轴模式逐轴添加即可。注意所有轴均不含 HOME_TRIGGER。

### 4.3 AxisId → 寄存器映射

```cpp
const RegisterInfo& ModbusSystemDriver::regCmdEnable(AxisId id) const {
    switch (id) {
        case AxisId::X:  return plc::reg::x_axis::command::ENABLE_REQUEST;   // M0
        case AxisId::Y:  return plc::reg::y_axis::command::ENABLE_REQUEST;   // M1
        case AxisId::Z:  return plc::reg::z_axis::command::ENABLE_REQUEST;   // M2
        case AxisId::R:  return plc::reg::r_axis::command::ENABLE_REQUEST;   // M3
        case AxisId::X1: // X1/X2 本版本不单独控制，fallback 到 X 龙门
        case AxisId::X2: return plc::reg::x_axis::command::ENABLE_REQUEST;
    }
}

// X 轴 Jog：使用 X1_JOG_FORWARD/BACKWARD（龙门模式的 PLC 自动同步 X2）
const RegisterInfo& ModbusSystemDriver::regCmdJogFwd(AxisId id) const {
    switch (id) {
        case AxisId::X:  return plc::reg::x_axis::command::X1_JOG_FORWARD;  // M50
        case AxisId::Y:  return plc::reg::y_axis::command::JOG_FORWARD;     // M54
        case AxisId::Z:  return plc::reg::z_axis::command::JOG_FORWARD;     // M56
        case AxisId::R:  return plc::reg::r_axis::command::JOG_FORWARD;     // M58
        case AxisId::X1: // fallback to X
        case AxisId::X2: return plc::reg::x_axis::command::X1_JOG_FORWARD;
    }
}

// ... regCmdJogBwd, regCmdAbsTarget, regCmdRelTarget, regCmdAbsTrigger, regCmdRelTrigger,
//     regCmdSetRelZero, regCmdClearRelZero, regCmdClearAbsPos,
//     regCmdJogSpeed, regCmdMoveSpeed 均按相同模式实现
//
// ⚠️ 注意：不再提供 regCmdHomeTrigger，因为本版本不开放回原点操作
//
// regFb* 系列同理，X 轴反馈读取 X1 寄存器
```

### 4.4 send() — 顶层命令分派

```cpp
CommunicationResult ModbusSystemDriver::send(const SystemCommand& cmd) {
    return std::visit([this](auto&& c) -> CommunicationResult {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, AxisCommandWithId>) {
            return sendAxisCommand(c.id, c.cmd);
        }
        else if constexpr (std::is_same_v<T, GantryCouplingCommand>) {
            return sendGantryCoupling(c);
        }
        else if constexpr (std::is_same_v<T, GantryPowerCommand>) {
            return sendGantryPower(c);
        }
        else if constexpr (std::is_same_v<T, EmergencyStopCommand>) {
            return sendEmergencyStop(c);
        }
    }, cmd);
}
```

### 4.5 sendAxisCommand — 轴命令分派

```cpp
CommunicationResult ModbusSystemDriver::sendAxisCommand(AxisId id, const AxisCommand& cmd) {
    return std::visit([this, id](auto&& c) -> CommunicationResult {
        using T = std::decay_t<decltype(c)>;

        if constexpr (std::is_same_v<T, EnableCommand>) {
            return m_device.writeBool(regCmdEnable(id), c.active);
        }
        else if constexpr (std::is_same_v<T, JogCommand>) {
            bool fwd = (c.dir == Direction::Forward);
            auto& reg = fwd ? regCmdJogFwd(id) : regCmdJogBwd(id);
            return m_device.writeBool(reg, c.active);
        }
        else if constexpr (std::is_same_v<T, SetAbsTargetCommand>) {
            // 仅写 ABS_TARGET D 寄存器，不触发运动
            return m_device.writeFloat(regCmdAbsTarget(id), static_cast<float>(c.target));
        }
        else if constexpr (std::is_same_v<T, TriggerAbsMoveCommand>) {
            // 仅发 ABS_MOVE_TRIGGER M 寄存器 EdgeTrigger
            return sendEdgeTrigger(regCmdAbsTrigger(id));
        }
        else if constexpr (std::is_same_v<T, SetRelTargetCommand>) {
            // 仅写 REL_TARGET D 寄存器，不触发运动
            return m_device.writeFloat(regCmdRelTarget(id), static_cast<float>(c.distance));
        }
        else if constexpr (std::is_same_v<T, TriggerRelMoveCommand>) {
            // 仅发 REL_MOVE_TRIGGER M 寄存器 EdgeTrigger
            return sendEdgeTrigger(regCmdRelTrigger(id));
        }
        else if constexpr (std::is_same_v<T, MoveCommand>) {
            // ⚠️ MoveCommand 已废弃，此分支仅用于向后兼容
            // 建议迁移到 SetAbsTarget / TriggerAbsMove / SetRelTarget / TriggerRelMove
            if (c.type == MoveType::Absolute) {
                auto r = m_device.writeFloat(regCmdAbsTarget(id), static_cast<float>(c.target));
                if (!r.isOk()) return r;
                return sendEdgeTrigger(regCmdAbsTrigger(id));
            } else {
                auto r = m_device.writeFloat(regCmdRelTarget(id), static_cast<float>(c.target));
                if (!r.isOk()) return r;
                return sendEdgeTrigger(regCmdRelTrigger(id));
            }
        }
        else if constexpr (std::is_same_v<T, StopCommand>) {
            auto r1 = m_device.writeBool(regCmdJogFwd(id), false);
            auto r2 = m_device.writeBool(regCmdJogBwd(id), false);
            return (!r1.isOk()) ? r1 : r2;
        }
        else if constexpr (std::is_same_v<T, ZeroAbsoluteCommand>) {
            // 清空绝对位置 → CLEAR_ABS_POS (M30~M33)
            return sendEdgeTrigger(regCmdClearAbsPos(id));
        }
        else if constexpr (std::is_same_v<T, SetRelativeZeroCommand>) {
            return sendEdgeTrigger(regCmdSetRelZero(id));
        }
        else if constexpr (std::is_same_v<T, ClearRelativeZeroCommand>) {
            return sendEdgeTrigger(regCmdClearRelZero(id));
        }
        else if constexpr (std::is_same_v<T, SetJogVelocityCommand>) {
            return m_device.writeFloat(regCmdJogSpeed(id), static_cast<float>(c.velocity));
        }
        else if constexpr (std::is_same_v<T, SetMoveVelocityCommand>) {
            return m_device.writeFloat(regCmdMoveSpeed(id), static_cast<float>(c.velocity));
        }
    }, cmd);
}
```

> 要点：`ZeroAbsoluteCommand` 直接映射到 `regCmdClearAbsPos(id)` → `CLEAR_ABS_POS` 寄存器（M30/M31/M32/M33），不再经过 `regCmdHomeTrigger`。

### 4.6 组级命令实现

```cpp
CommunicationResult ModbusSystemDriver::sendGantryCoupling(const GantryCouplingCommand& cmd) {
    // M4: LINKAGE_ENABLE — Level 型
    return m_device.writeBool(plc::reg::x_axis::command::LINKAGE_ENABLE, cmd.enableCoupling);
}

CommunicationResult ModbusSystemDriver::sendGantryPower(const GantryPowerCommand& cmd) {
    // GantryPower 与 X 轴 Enable 共用 M0 (ENABLE_REQUEST)
    return m_device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, cmd.enable);
}

CommunicationResult ModbusSystemDriver::sendEmergencyStop(const EmergencyStopCommand& cmd) {
    // M80: ESTOP_TRIGGER — Level 型，不需要脉冲
    //   active=true  → ON  (急停中)
    //   active=false → OFF (解除急停)
    return m_device.writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, cmd.active);
}
```

### 4.7 EdgeTrigger 脉冲管理

```cpp
// ===== 四命令独立实现（推荐） =====

CommunicationResult ModbusSystemDriver::sendSetAbsTarget(AxisId id, const SetAbsTargetCommand& cmd) {
    return m_device.writeFloat(regCmdAbsTarget(id), static_cast<float>(cmd.target));
}

CommunicationResult ModbusSystemDriver::sendTriggerAbsMove(AxisId id) {
    return sendEdgeTrigger(regCmdAbsTrigger(id));   // M40/M42/M44/M46
}

CommunicationResult ModbusSystemDriver::sendSetRelTarget(AxisId id, const SetRelTargetCommand& cmd) {
    return m_device.writeFloat(regCmdRelTarget(id), static_cast<float>(cmd.distance));
}

CommunicationResult ModbusSystemDriver::sendTriggerRelMove(AxisId id) {
    return sendEdgeTrigger(regCmdRelTrigger(id));   // M41/M43/M45/M47
}

// ===== 已废弃：sendMoveCommand 不再使用 =====
// MoveCommand 已被 SetAbsTarget / TriggerAbsMove / SetRelTarget / TriggerRelMove 取代。
// sendAxisCommand() 中 MoveCommand 分支保留仅用于向后兼容，建议迁移后删除。

CommunicationResult ModbusSystemDriver::sendEdgeTrigger(const RegisterInfo& reg) {
    auto result = m_device.writeBool(reg, true);
    if (!result.isOk()) return result;

    m_pendingEdges.push_back({reg, std::chrono::steady_clock::now(), false});
    return result;
}

void ModbusSystemDriver::servicePendingEdgeTriggers() {
    auto now = std::chrono::steady_clock::now();
    for (auto& edge : m_pendingEdges) {
        if (!edge.offSent) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - edge.onTime).count();
            if (elapsed >= EDGE_TRIGGER_PULSE_MS) {
                m_device.writeBool(edge.reg, false);
                edge.offSent = true;
            }
        }
    }
    m_pendingEdges.erase(
        std::remove_if(m_pendingEdges.begin(), m_pendingEdges.end(),
            [](const PendingEdge& e) { return e.offSent; }),
        m_pendingEdges.end());
}
```

### 4.8 PLC 状态寄存器差异分析与多信号融合推导

#### 4.8.1 问题描述

PLC 端 **D100（轴状态显示）** 寄存器提供 4 个值：

| D100 值 | 含义 |
|---------|------|
| 0 | 未使能 |
| 1 | 使能（静止） |
| 2 | 运动状态 |
| 3 | 报警 |

由于多圈绝对值编码电机的特性，电机使能后 D100 会在 **1 和 2 之间跳变**，即使在空闲状态下也可能短暂翻转为 2。这导致无法直接将 D100 一对一映射到 Domain 层的 7 状态枚举：

```cpp
enum class AxisState {
    Unknown = 0,        // 不存在于 PLC
    Disabled = 1,       // ← D100=0
    Idle = 2,           // ← D100=1，但可能与 2 抖跳
    Jogging = 3,        // ← D100=2，但无法区分运动类型
    MovingAbsolute = 4, // ← D100=2，但无法区分运动类型
    MovingRelative = 5, // ← D100=2，但无法区分运动类型
    Error = 6           // ← D100=3，但报警时运动 Coil 均为 OFF，仍需 ALARM_CODE 辅助
};
```

#### 4.8.2 可用信号清单

PLC 提供了充足的**离散 Coil 信号**来补充 D100 的不足：

| 信号 | 寄存器 | 类型 | 含义 |
|------|--------|------|------|
| D100 STATE | D100~D103 | Int16 | 粗略状态码 (0=未使能, 1=使能, 2=运动, 3=报警) |
| ABS_MOVING | M110/M113/M116/M119 | Bool | 绝对定位进行中 |
| REL_MOVING | M111/M114/M117/M120 | Bool | 相对定位进行中 |
| JOGGING | M112/M115/M118/M121 | Bool | 点动进行中 |
| ALARM_CODE | D110~D113 | Int16 | 报警码（非零 = 故障，补充 D100=3 的报警详情） |

#### 4.8.3 多信号融合状态推导规则

不依赖 D100 的 1/2 区分，使用 D100 + Coil 信号 + ALARM_CODE **优先级链**推导精确状态：

```
输入: d100State, absMoving, relMoving, jogging, alarmCode

推导:
  if d100State == 3 || alarmCode != 0  → AxisState::Error     (D100=3 或报警码非零)
  if d100State == 0                    → AxisState::Disabled   (未使能)

  // d100State ∈ {1, 2} 时，由 Coil 信号决定精确子状态
  if absMoving           → AxisState::MovingAbsolute
  if relMoving           → AxisState::MovingRelative
  if jogging             → AxisState::Jogging
  else                   → AxisState::Idle            (使能但无运动信号)
```

> **D100=3 是 Error 第一信号源**：PLC 报警时 d100State 会置为 3。`alarmCode != 0` 作为冗余兜底（例如 ALARM_CODE 先到达、D100 暂未翻转的过渡场景）。

**设计原理**：

1. **报警最高优先级（双条件 OR）**：`d100State == 3` 是 PLC 报警的标准状态码，`alarmCode != 0` 作为冗余兜底。两个条件任一满足即判定为 Error。PLC 报警时会将运动 Coil 全部清零，此规则确保 Error 状态不被误判为 Idle。

2. **Disabled 由 D100=0 判定**：未使能状态下所有运动 Coil 均 OFF，D100=0 是可靠信号。

3. **D100 的 1↔2 抖跳被消除**：使能后 D100 不论是 1 还是 2，都由独立的 Coil 信号决定子状态。多圈编码器导致的 2 闪烁不会误触发运动状态判断。

4. **运动 Coil 互斥**：PLC 程序保证同一时刻最多一个运动 Coil 为 ON，因此 if-else 链安全。

5. **非运动 Coil 的** `MOVE_DONE` (M100) **不参与状态推导**：`MOVE_DONE` 是运动完成后的暂态信号，可能短暂为 OFF 导致误判。状态推导只用"正在运动中"的持续信号。

#### 4.8.4 状态推导与 AxisFeedback 的关系

`AxisFeedback` 只需要 `state` (AxisState)，不需要 `moveDoneFlag`、`isJogging` 等字段。状态推导在 `ModbusSystemDriver::readAxisFeedback()` 内部完成，对 Domain 层透明。

```cpp
// AxisFeedback 保持不变，无需扩展
struct AxisFeedback {
    AxisState state;       // ← 由 Driver 推导后填入
    double absPos;
    double relPos;
    double relZeroAbsPos;
    bool posLimit;
    bool negLimit;
    double posLimitValue;
    double negLimitValue;
    double getjogVelocity;
    double getMoveVelocity;
};
```

> FakeAxisDriver 不受影响：FakePLC 内部直接管理 AxisState，`getFeedback()` 返回完整 AxisFeedback，无需融合逻辑。

### 4.9 pollFeedback — 反馈链路

```cpp
void ModbusSystemDriver::pollFeedback(SystemContext& ctx) {
    // 1. 先处理待发送的 OFF 脉冲
    servicePendingEdgeTriggers();

    // 2. 批量轮询 PLC
    PollRequest req = m_poller.buildRequest();
    PlcSnapshot snap = m_poller.poll(*m_client, req);
    m_device.updateSnapshot(std::move(snap));

    if (!m_device.isStateTrusted()) return;

    // 3. 分发轴反馈（本版本 X 轴使用 X1 寄存器）
    for (auto axId : {AxisId::X, AxisId::Y, AxisId::Z, AxisId::R}) {
        readAxisFeedback(axId, ctx);
    }

    // 4. 分发系统反馈
    readSystemFeedback(ctx);
}

AxisState ModbusSystemDriver::deriveAxisState(AxisId id) {
    // 1. 读取所有相关信号
    int16_t d100State  = m_device.readInt16(regFbState(id));       // D100~D103: 0/1/2/3
    int16_t alarmCode  = m_device.readInt16(regFbAlarmCode(id));   // D110~D113
    bool absMoving     = m_device.readBool(regFbAbsMoving(id));    // M110/M113/M116/M119
    bool relMoving     = m_device.readBool(regFbRelMoving(id));    // M111/M114/M117/M120
    bool jogging       = m_device.readBool(regFbJogging(id));      // M112/M115/M118/M121

    // 2. 多信号融合推导（优先级链）
    //    d100State==3 或 alarmCode!=0 → Error → Disabled → 运动子类型 → Idle
    if (d100State == 3 || alarmCode != 0) {
        return AxisState::Error;
    }

    if (d100State == 0) {
        return AxisState::Disabled;
    }

    // d100State ∈ {1, 2}：由 Coil 信号精确判定子状态
    // 多圈编码器导致的 D100 1↔2 抖跳不影响此逻辑
    if (absMoving) {
        return AxisState::MovingAbsolute;
    }
    if (relMoving) {
        return AxisState::MovingRelative;
    }
    if (jogging) {
        return AxisState::Jogging;
    }

    return AxisState::Idle;
}

void ModbusSystemDriver::readAxisFeedback(AxisId id, SystemContext& ctx) {
    // 1. 多信号融合推导轴状态
    AxisState derivedState = deriveAxisState(id);

    // 2. 读取位置和限位物理量
    double absPos = m_device.readFloat(regFbAbsPos(id));           // D120/D124/D128/D132
    double relPos = m_device.readFloat(regFbRelPos(id));           // D122/D126/D130/D134

    // 3. 写入 AxisFeedback（只设置真实存在的字段）
    AxisFeedback fb{};
    fb.state          = derivedState;
    fb.absPos         = absPos;
    fb.relPos         = relPos;
    fb.relZeroAbsPos  = 0.0;  // PLC 不直接提供此值，由 Domain 层 SetRelativeZero 闭环计算
    fb.posLimit       = false; // TODO: 从 SOFT_LIMIT_STATE / 硬限位信号读取
    fb.negLimit       = false;
    fb.posLimitValue  = 0.0;   // TODO: 从 SOFT_LIMIT_POS/NEG 读取
    fb.negLimitValue  = 0.0;
    fb.getjogVelocity = 0.0;   // 速度反馈由 SetVelocity 闭环处理，此处无需回读
    fb.getMoveVelocity= 0.0;

    // 4. 注入 Domain 轴实体
    Axis* axis = nullptr;
    ContextRejection r;
    if (ctx.tryGetAxis(id, axis, r) && axis) {
        axis->applyFeedback(fb);
    }
}

void ModbusSystemDriver::readSystemFeedback(SystemContext& ctx) {
    ctx.setEmergencyStopActive(
        m_device.readBool(plc::reg::system_global::feedback::ESTOP_ACTIVE));   // M130
    ctx.setGantryErrorCode(
        m_device.readInt16(plc::reg::system_global::feedback::GANTRY_ERROR_CODE)); // D180
    ctx.setGantryCoupled(
        m_device.readBool(plc::reg::x_axis::feedback::LINKAGE_STATE));         // M125
}
```

---

## 5. 集成策略：替换 FakePLC

### 5.1 替换方案

```cpp
// 原来（main.cpp）:
auto driver = std::make_unique<FakeAxisDriver>(groupId, fakePLC);

// 改为：
auto driver = std::make_unique<plc::ModbusSystemDriver>(
    plc::protocol::INOVANCE_PROFILE);
driver->connect("192.168.1.xxx", 502);
```

### 5.2 主循环集成

```cpp
void tick() {
    for (auto& driver : systemDrivers) {
        driver->pollFeedback(systemContext);
        // pollFeedback 内部自动完成:
        //   1. servicePendingEdgeTriggers() → 发送
