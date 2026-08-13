# 阶段四：命令分派单元测试 —— TDD 详细开发文档

> 版本：v1.0  
> 日期：2026-05-30  
> 项目：servoV6  
> 前置文档：
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计 (v4.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md)
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤 (v2.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤.md)
> - [阶段三：状态推导引擎单元测试 —— TDD 详细开发文档 (v1.0)](./阶段三-状态推导引擎TDD详细开发文档.md)

---

## 目录

1. [目标与范围](#1-目标与范围)
2. [架构决策：send 分派策略](#2-架构决策send-分派策略)
3. [命令分派路由规则规范](#3-命令分派路由规则规范)
4. [测试文件规划](#4-测试文件规划)
5. [RED 步骤 —— 测试用例编写](#5-red-步骤--测试用例编写)
6. [GREEN 步骤 —— 生产代码实现](#6-green-步骤--生产代码实现)
7. [REFACTOR 步骤](#7-refactor-步骤)
8. [CMakeLists 变更](#8-cmakelists-变更)
9. [构建与运行命令](#9-构建与运行命令)
10. [验收检查清单](#10-验收检查清单)

---

## 1. 目标与范围

### 1.1 阶段目标

验证 `send(SystemCommand)` 顶层分派以及 `sendAxisCommand()` 内部正确路由到对应的 `writeBool` / `writeFloat` / `sendEdgeTrigger`。确保每种 `SystemCommand` variant 的替代项都被正确处理，不会出现"漏写"、"错写"或"多写"的情况。

### 1.2 测试范围

| 范围 | 包含 | 不包含 |
|------|------|--------|
| 顶层分派 | `SystemCommand` variant 四种类型的路由 | — |
| 轴命令 | 全部 11 种 `AxisCommand` variant（含 `std::monostate` 忽略） | Domain 层状态机校验（那是 `Axis` 类的职责） |
| 四命令定位 | `SetAbsTargetCommand` / `TriggerAbsMoveCommand` / `SetRelTargetCommand` / `TriggerRelMoveCommand` 各自的独立路由 | 命令间的时序编排（那是 Orchestrator 的职责） |
| 组级命令 | `GantryCouplingCommand` / `GantryPowerCommand` / `EmergencyStopCommand` | 龙门状态机（那是 `GantryCouplingController` 的职责） |
| 向后兼容 | `MoveCommand`（deprecated）的双步路由 | `MoveCommand` 的 Domain 层逻辑（已废弃） |
| 错误路径 | 设备未初始化（`m_device == nullptr`）时的返回值 | Modbus 网络超时重试（阶段五/六的职责） |
| 命令类型 | Level 型 vs EdgeTrigger 型的正确区分 | EdgeTrigger 的 ON→OFF 生命周期（旧 legacy；PLC_re 已改为触发型 PLC 自复位、只写 ON，不实现客户端回写 OFF） |

### 1.3 当前代码状态

```
已有资产 ✅
├── domain/command/SystemCommand.h              — SystemCommand variant 定义
├── domain/entity/Axis.h                        — AxisCommand variant (含全部子命令)
├── infrastructure/plc/ModbusSystemDriver.h     — regCmd* 选择器（阶段一已完成）
├── infrastructure/plc/ModbusSystemDriver.h     — sendEdgeTrigger / servicePendingEdgeTriggers（阶段二已完成）
├── infrastructure/plc/AxisStateDeriver.h       — 纯函数 deriveAxisState()（阶段三已完成）
├── infrastructure/ISystemDriver.h              — send(SystemCommand) 纯虚接口
├── infrastructure/plc/protocol/PlcDevice.h     — writeBool / writeFloat 方法

待开发 ❌
├── infrastructure/plc/ModbusSystemDriver.h     — send() 方法体（当前为 stub: return {};）
├── infrastructure/plc/ModbusSystemDriver.h     — sendAxisCommand() 内部方法（待创建）
├── tests/infrastructure/test_modbus_system_driver_send.cpp  — 本阶段的测试文件
└── tests/CMakeLists.txt                                        — 新增 test target
```

---

## 2. 架构决策：send 分派策略

### 2.1 决策

采用**双层 `std::visit` 嵌套分派**：`send(SystemCommand)` 对顶层 variant 做一次 visit，`sendAxisCommand(AxisId, AxisCommand)` 对轴命令子 variant 做二次 visit。组级命令直接在顶层 visit 中分发。

### 2.2 决策理由

| 因素 | 单层扁平分派 | 双层嵌套分派 |
|------|------------|------------|
| **职责分离** | 轴命令与组级命令混在同一函数 | 顶层分 `AxisCommand` / `Gantry*` / `EmergencyStop` 三大类 |
| **可测试性** | 需要为每个子命令 mock 完整 SystemCommand | 可以独立测试 `sendAxisCommand()`，不依赖 SystemContext |
| **扩展性** | 新增轴命令类型需修改顶层 visit | 顶层 visit 不变，仅修改 `sendAxisCommand()` |
| **代码可读性** | 一个巨大函数 | 两个职责清晰的中等函数 |
| **复用性** | — | `sendAxisCommand()` 可被未来其他 Driver 复用 |

### 2.3 分派架构

```
send(SystemCommand cmd)
  │
  ├── std::visit(overloaded{
  │       [](AxisCommandWithId& ax)      → sendAxisCommand(ax.id, ax.cmd)
  │       [](GantryCouplingCommand& g)   → writeBool(LINKAGE_ENABLE, g.enable)
  │       [](GantryPowerCommand& g)      → writeBool(ENABLE_REQUEST(X), g.enable)
  │       [](EmergencyStopCommand& e)    → writeBool(ESTOP_TRIGGER, e.active)  // Level 型
  │   }, cmd)
  │
  └── sendAxisCommand(AxisId id, AxisCommand cmd)
        │
        ├── std::visit(overloaded{
        │       [](std::monostate)        → return Sent()      // 空命令，无操作
        │       [](EnableCommand& e)      → writeBool(ENABLE_REQUEST, e.active)
        │       [](JogCommand& j)         → writeBool(JOG_FWD/BWD, j.active)
        │       [](StopCommand&)          → writeBool(JOG_FWD, false) + writeBool(JOG_BWD, false)
        │       [](SetJogVelocityCommand& v)   → writeFloat(JOG_SPEED, v.velocity)
        │       [](SetMoveVelocityCommand& v)  → writeFloat(MOVE_SPEED, v.velocity)
        │       [](SetAbsTargetCommand& s)     → writeFloat(ABS_TARGET, s.target)
        │       [](TriggerAbsMoveCommand&)     → sendEdgeTrigger(ABS_MOVE_TRIGGER)
        │       [](SetRelTargetCommand& s)     → writeFloat(REL_TARGET, s.distance)
        │       [](TriggerRelMoveCommand&)     → sendEdgeTrigger(REL_MOVE_TRIGGER)
        │       [](ZeroAbsoluteCommand&)       → sendEdgeTrigger(CLEAR_ABS_POS)
        │       [](SetRelativeZeroCommand&)    → sendEdgeTrigger(SET_REL_ZERO)
        │       [](ClearRelativeZeroCommand&)  → sendEdgeTrigger(CLEAR_REL_ZERO)
        │       [](MoveCommand& m)             // deprecated: writeFloat + sendEdgeTrigger
        │   }, cmd)
```

### 2.4 命令类型分类

| 类型 | 操作 | 命令列表 |
|------|------|---------|
| **Level 型** | `writeBool(reg, true/false)` | `EnableCommand`, `JogCommand`, `StopCommand`, `GantryCouplingCommand`, `GantryPowerCommand`, `EmergencyStopCommand` |
| **D-Register 型** | `writeFloat(reg, value)` | `SetJogVelocityCommand`, `SetMoveVelocityCommand`, `SetAbsTargetCommand`, `SetRelTargetCommand` |
| **EdgeTrigger 型** | `sendEdgeTrigger(reg)` → `writeBool(true)` + 入队 + 150ms 后 `writeBool(false)` | `TriggerAbsMoveCommand`, `TriggerRelMoveCommand`, `ZeroAbsoluteCommand`, `SetRelativeZeroCommand`, `ClearRelativeZeroCommand` |
| **空命令** | `return CommunicationResult::Sent()` | `std::monostate` |
| **废弃** | `writeFloat` + `sendEdgeTrigger` | `MoveCommand`（向后兼容） |

---

## 3. 命令分派路由规则规范

### 3.1 正式规则（设计文档 §4.5 + §4.6）

#### 3.1.1 轴命令 → 寄存器映射表

| AxisCommand 子类型 | 寄存器选择器 | 写入方法 | 类型 |
|--------------------|-------------|---------|------|
| `std::monostate` | — | 直接返回 `Sent()` | 空命令 |
| `EnableCommand{active}` | `regCmdEnable(id)` | `writeBool(reg, active)` | Level |
| `JogCommand{Forward, active}` | `regCmdJogFwd(id)` | `writeBool(reg, active)` | Level |
| `JogCommand{Backward, active}` | `regCmdJogBwd(id)` | `writeBool(reg, active)` | Level |
| `StopCommand{}` | `regCmdJogFwd(id)` + `regCmdJogBwd(id)` | `writeBool` ×2 (均写 `false`) | Level |
| `SetJogVelocityCommand{v}` | `regCmdJogSpeed(id)` | `writeFloat(reg, v)` | D-Register |
| `SetMoveVelocityCommand{v}` | `regCmdMoveSpeed(id)` | `writeFloat(reg, v)` | D-Register |
| `SetAbsTargetCommand{target}` | `regCmdAbsTarget(id)` | `writeFloat(reg, target)` | D-Register |
| `TriggerAbsMoveCommand{}` | `regCmdAbsTrigger(id)` | `sendEdgeTrigger(reg)` | EdgeTrigger |
| `SetRelTargetCommand{distance}` | `regCmdRelTarget(id)` | `writeFloat(reg, distance)` | D-Register |
| `TriggerRelMoveCommand{}` | `regCmdRelTrigger(id)` | `sendEdgeTrigger(reg)` | EdgeTrigger |
| `ZeroAbsoluteCommand{}` | `regCmdClearAbsPos(id)` | `sendEdgeTrigger(reg)` | EdgeTrigger |
| `SetRelativeZeroCommand{}` | `regCmdSetRelZero(id)` | `sendEdgeTrigger(reg)` | EdgeTrigger |
| `ClearRelativeZeroCommand{}` | `regCmdClearRelZero(id)` | `sendEdgeTrigger(reg)` | EdgeTrigger |
| `MoveCommand{Absolute, t, _}` | `regCmdAbsTarget(id)` + `regCmdAbsTrigger(id)` | `writeFloat` + `sendEdgeTrigger` | Deprecated |
| `MoveCommand{Relative, d, _}` | `regCmdRelTarget(id)` + `regCmdRelTrigger(id)` | `writeFloat` + `sendEdgeTrigger` | Deprecated |

#### 3.1.2 组级命令 → 寄存器映射表

| SystemCommand 子类型 | 寄存器选择器 | 写入方法 | 类型 |
|---------------------|-------------|---------|------|
| `GantryCouplingCommand{enable}` | `regGantryCoupling()` | `writeBool(reg, enable)` | Level |
| `GantryPowerCommand{enable}` | `regCmdEnable(AxisId::X)` | `writeBool(reg, enable)` | Level |
| `EmergencyStopCommand{active}` | `regEmergencyStopTrigger()` | `writeBool(reg, active)` | Level |

### 3.2 关键设计原理

1. **v4.0 四命令解耦**：`SetAbsTargetCommand` **仅写 D 寄存器**，不触发。`TriggerAbsMoveCommand` **仅发 EdgeTrigger**，不写 D 寄存器。两者完全独立，确保测试可以分别验证"写而不触发"和"触发而不写"。

2. **MoveCommand 废弃但保留**：由于 Application 层 `MoveAbsoluteUseCase` 和 `MoveRelativeUseCase` 已使用新四命令接口，`MoveCommand` 仅用于向后兼容。`sendAxisCommand` 中保留其分支但标记 `[[deprecated]]`。

3. **EmergencyStop 是 Level 型**：急停触发需要持续写入 `true`（保持急停），解除需要写入 `false`。EdgeTrigger 的脉冲特性不适合 Level 信号。

4. **GantryPower 与 X 轴 Enable 共用 M0**：因为龙门模式下 X 轴使能就是整个龙门电机上电，物理上同一继电器回路。

5. **StopCommand 写两个 Coil 为 false**：停止 Jog 时需要同时清除正向和反向两个 Coil，确保 PLC 不再输出运动信号。不写 `true` —— 停止就是清除信号。

6. **空命令 std::monostate 直接返回成功**：这是一个合法的命令（领域层可能因为条件不满足而产生空意图），不应视为错误。

---

## 4. 测试文件规划

### 4.1 文件命名

```
tests/infrastructure/test_modbus_system_driver_send.cpp
```

命名遵循 `test_modbus_system_driver_*` 前缀约定（TDD 步骤文档 §2），与阶段一的 `test_modbus_system_driver_registers.cpp`、阶段二的 `test_modbus_system_driver_edge_trigger.cpp`、阶段三的 `test_modbus_system_driver_state_derive.cpp` 保持一致。

### 4.2 测试架构

```
测试文件结构：
├── ModbusSystemDriverSendTest              (TEST_F 夹具 — 所有测试共享)
│   ├── Level 型轴命令 (6 项)
│   │   ├── EnableCommandWritesCoil
│   │   ├── DisableCommandWritesCoilFalse
│   │   ├── JogForwardCommandWritesCoil
│   │   ├── JogBackwardCommandWritesCoil
│   │   ├── StopCommandWritesBothJogCoilsOff
│   │   └── MonostateEmptyCommandNoWrite
│   ├── D-Register 型轴命令 (4 项)
│   │   ├── SetJogVelocityWritesFloat
│   │   ├── SetMoveVelocityWritesFloat
│   │   ├── SetAbsTargetWritesOnlyDRegister
│   │   └── SetRelTargetWritesOnlyDRegister
│   ├── EdgeTrigger 型轴命令 (5 项)
│   │   ├── TriggerAbsMoveOnlyEdgeTrigger
│   │   ├── TriggerRelMoveOnlyEdgeTrigger
│   │   ├── ZeroAbsoluteCommandEdgeTrigger
│   │   ├── SetRelativeZeroCommandEdgeTrigger
│   │   └── ClearRelativeZeroCommandEdgeTrigger
│   ├── 组级命令 (5 项)
│   │   ├── GantryCouplingCommandLevelType
│   │   ├── GantryUncouplingCommandLevelType
│   │   ├── GantryPowerCommandSharesM0
│   │   ├── EmergencyStopCommandLevelType
│   │   └── ReleaseEmergencyStopCommandLevelType
│   ├── 向后兼容：MoveCommand (2 项)
│   │   ├── MoveCommandAbsoluteBackwardCompat
│   │   └── MoveCommandRelativeBackwardCompat
│   └── 错误路径 (2 项)
│       ├── SendWithoutDeviceReturnsDisconnected
│       └── StopCommandWithoutDeviceReturnsError
└── main()                                  (标准 GTest entry point)
```

### 4.3 依赖关系

```
test_modbus_system_driver_send
  ├── infrastructure/plc/ModbusSystemDriver.h   (待 GREEN 阶段实现 send 方法体)
  ├── domain/command/SystemCommand.h            (SystemCommand variant)
  ├── domain/entity/Axis.h                      (AxisCommand variant)
  ├── infrastructure/plc/protocol/PlcDevice.h    (需将 writeBool/writeFloat 改为 virtual)
  ├── tests/infrastructure/mock/MockPlcDevice.h  (MockPlcDevice)
  └── gtest_main + gmock                         (已有 — external/googletest)
```

### 4.4 Mock 设计

使用 `StrictMock<MockPlcDevice>` 确保所有 `writeBool` / `writeFloat` 调用都被预期，不存在遗漏的写操作。`MockPlcDevice` 定义如下：

```cpp
// tests/infrastructure/mock/MockPlcDevice.h
#pragma once

#include "gmock/gmock.h"
#include "infrastructure/plc/protocol/PlcDevice.h"

class MockPlcDevice : public protocol::PlcDevice {
public:
    MOCK_METHOD(CommunicationResult, writeBool,
                (const protocol::RegisterInfo& reg, bool value), (override));
    MOCK_METHOD(CommunicationResult, writeFloat,
                (const protocol::RegisterInfo& reg, float value), (override));
};
```

---

## 5. RED 步骤 —— 测试用例编写

> ⚠️ **TDD 规范**：此步骤编写的测试文件必须先编译通过但运行失败（因为 `send()` 当前为 stub：`return {}`），然后再在 GREEN 步骤中实现完整分派逻辑使测试通过。

### 5.1 测试夹具

```cpp
class ModbusSystemDriverSendTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockDevice_ = std::make_unique<StrictMock<MockPlcDevice>>();
        driver_ = std::make_unique<plc::ModbusSystemDriver>();
        driver_->setDevice(mockDevice_.get());
    }

    void TearDown() override {
        driver_.reset();
        mockDevice_.reset();
    }

    std::unique_ptr<StrictMock<MockPlcDevice>> mockDevice_;
    std::unique_ptr<plc::ModbusSystemDriver> driver_;
};
```

### 5.2 Level 型轴命令测试

#### 测试用例 L1：使能命令 —— 写 Coil true

```cpp
TEST_F(ModbusSystemDriverSendTest, EnableCommandWritesCoil) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, EnableCommand{true}};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L2：掉电命令 —— 写 Coil false

```cpp
TEST_F(ModbusSystemDriverSendTest, DisableCommandWritesCoilFalse) {
    SystemCommand cmd = AxisCommandWithId{AxisId::Y, EnableCommand{false}};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::y_axis::command::ENABLE_REQUEST, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L3：正向 Jog 命令

```cpp
TEST_F(ModbusSystemDriverSendTest, JogForwardCommandWritesCoil) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, JogCommand{Direction::Forward, true}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L4：反向 Jog 命令

```cpp
TEST_F(ModbusSystemDriverSendTest, JogBackwardCommandWritesCoil) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Y, JogCommand{Direction::Backward, true}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::y_axis::command::JOG_BACKWARD, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L5：Jog 停止命令 —— 同时写两个 Coil 为 false

```cpp
TEST_F(ModbusSystemDriverSendTest, JogStopCommandDeactivatesBothCoils) {
    // JogCommand{Forward, active=false} → 走与 StopCommand 不同的语义：
    //   仅写 JOG_FWD=false，不写 JOG_BWD
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, JogCommand{Direction::Forward, false}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    // 不应写 JOG_BWD
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L6：StopCommand —— 同时清除两个 Jog Coil

```cpp
TEST_F(ModbusSystemDriverSendTest, StopCommandWritesBothJogCoilsOff) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, StopCommand{}};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::X1_JOG_BACKWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 L7：空命令 monostate —— 无任何写入

```cpp
TEST_F(ModbusSystemDriverSendTest, MonostateEmptyCommandNoWrite) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, std::monostate{}};
    // StrictMock 不应有任何 writeBool/writeFloat 调用
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

### 5.3 D-Register 型轴命令测试

#### 测试用例 D1：设置 Jog 速度

```cpp
TEST_F(ModbusSystemDriverSendTest, SetJogVelocityWritesFloat) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Y, SetJogVelocityCommand{150.0}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::y_axis::command::JOG_SPEED, 150.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 D2：设置 Move 速度

```cpp
TEST_F(ModbusSystemDriverSendTest, SetMoveVelocityWritesFloat) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Z, SetMoveVelocityCommand{200.0}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::z_axis::command::MOVE_SPEED, 200.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 D3：SetAbsTarget —— 仅写 D 寄存器，不触发

> ⚠️ **v4.0 核心测试**：验证 `SetAbsTargetCommand` **仅写 ABS_TARGET D 寄存器**，不产生任何 EdgeTrigger 入队。

```cpp
TEST_F(ModbusSystemDriverSendTest, SetAbsTargetWritesOnlyDRegister) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, SetAbsTargetCommand{123.45}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::x_axis::command::ABS_TARGET, 123.45f))
        .WillOnce(Return(CommunicationResult::Sent()));
    // 不应有任何 writeBool 调用（无 M40 触发）
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 D4：SetRelTarget —— 仅写 D 寄存器，不触发

```cpp
TEST_F(ModbusSystemDriverSendTest, SetRelTargetWritesOnlyDRegister) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Y, SetRelTargetCommand{-50.0}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::y_axis::command::REL_TARGET, -50.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 5.4 EdgeTrigger 型轴命令测试

> ⚠️ **v4.0 核心测试**：验证 Trigger 类命令**仅走 EdgeTrigger 路径**，不写任何 D 寄存器。

#### 测试用例 E1：TriggerAbsMove —— 仅 EdgeTrigger，不写 D 寄存器

```cpp
TEST_F(ModbusSystemDriverSendTest, TriggerAbsMoveOnlyEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, TriggerAbsMoveCommand{}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 测试用例 E2：TriggerRelMove —— 仅 EdgeTrigger

```cpp
TEST_F(ModbusSystemDriverSendTest, TriggerRelMoveOnlyEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Z, TriggerRelMoveCommand{}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::z_axis::command::REL_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 测试用例 E3：ZeroAbsolute —— 走 CLEAR_ABS_POS 边沿触发

> ⚠️ **v4.0 变化**：`ZeroAbsoluteCommand` 不再经 HOME_TRIGGER，而是映射到 `CLEAR_ABS_POS` (M30~M33)。

```cpp
TEST_F(ModbusSystemDriverSendTest, ZeroAbsoluteCommandEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, ZeroAbsoluteCommand{}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::CLEAR_ABS_POS, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 测试用例 E4：SetRelativeZero —— 走 SET_REL_ZERO 边沿触发

```cpp
TEST_F(ModbusSystemDriverSendTest, SetRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Y, SetRelativeZeroCommand{}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::y_axis::command::SET_REL_ZERO, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 测试用例 E5：ClearRelativeZero —— 走 CLEAR_REL_ZERO 边沿触发

```cpp
TEST_F(ModbusSystemDriverSendTest, ClearRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::R, ClearRelativeZeroCommand{}
    };
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::r_axis::command::CLEAR_REL_ZERO, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

### 5.5 组级命令测试

#### 测试用例 G1：龙门联动

```cpp
TEST_F(ModbusSystemDriverSendTest, GantryCouplingCommandLevelType) {
    SystemCommand cmd = GantryCouplingCommand{/*enableCoupling=*/true};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::LINKAGE_ENABLE, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // Level 型，不进入队列
}
```

#### 测试用例 G2：龙门解耦

```cpp
TEST_F(ModbusSystemDriverSendTest, GantryUncouplingCommandLevelType) {
    SystemCommand cmd = GantryCouplingCommand{/*enableCoupling=*/false};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::LINKAGE_ENABLE, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 G3：龙门电机上电 —— 与 X 轴 Enable 共用 M0

```cpp
TEST_F(ModbusSystemDriverSendTest, GantryPowerCommandSharesM0WithXEnable) {
    SystemCommand cmd = GantryPowerCommand{/*enable=*/true};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 G4：龙门电机掉电

```cpp
TEST_F(ModbusSystemDriverSendTest, GantryPowerOffCommand) {
    SystemCommand cmd = GantryPowerCommand{/*enable=*/false};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 测试用例 G5：急停触发 —— Level 型，不进入 EdgeTrigger 队列

> ⚠️ **v4.0 核心测试**：`EmergencyStopCommand` 是 Level 型，直接 `writeBool`，不经过 `sendEdgeTrigger`。

```cpp
TEST_F(ModbusSystemDriverSendTest, EmergencyStopCommandLevelType) {
    SystemCommand cmd = EmergencyStopCommand{/*active=*/true};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 G6：解除急停 —— Level 型

```cpp
TEST_F(ModbusSystemDriverSendTest, ReleaseEmergencyStopCommandLevelType) {
    SystemCommand cmd = EmergencyStopCommand{/*active=*/false};
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::system_global::command::ESTOP_TRIG
