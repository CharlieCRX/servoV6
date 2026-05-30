# SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤

> 版本：v2.0  
> 日期：2026-05-30  
> 项目：servoV6  
> 前置设计文档：[SystemCommand寄存器映射与Domain-Infrastructure对接设计.md](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md) (v4.0)

---

## 目录

1. [TDD 总体策略](#1-tdd-总体策略)
2. [测试文件规划](#2-测试文件规划)
3. [阶段一：寄存器选择器单元测试](#3-阶段一寄存器选择器单元测试)
4. [阶段二：EdgeTrigger 脉冲管理单元测试](#4-阶段二edgetrigger-脉冲管理单元测试)
5. [阶段三：状态推导引擎单元测试](#5-阶段三状态推导引擎单元测试)
6. [阶段四：命令分派单元测试](#6-阶段四命令分派单元测试)
7. [阶段五：反馈轮询集成测试](#7-阶段五反馈轮询集成测试)
8. [阶段六：系统级集成测试](#8-阶段六系统级集成测试)
9. [CMakeLists 变更清单](#9-cmakelists-变更清单)
10. [实施顺序与里程碑](#10-实施顺序与里程碑)

---

## 1. TDD 总体策略

### 1.1 依赖关系分层

```
┌──────────────────────────────────────────────────────┐
│  阶段六：系统级集成测试（替换 FakePLC）                 │
│  ├── 依赖：ModbusSystemDriver 完整实现                 │
│  └── 使用 FakePLC 作为 Mock 对比参考                    │
├──────────────────────────────────────────────────────┤
│  阶段五：反馈轮询集成测试（pollFeedback）               │
│  ├── 依赖：PlcDevice + Mock IModbusClient              │
│  └── 测试反馈链路：read → translate → dispatch         │
├──────────────────────────────────────────────────────┤
│  阶段四：命令分派单元测试（send）                       │
│  ├── 依赖：Mock PlcDevice                             │
│  └── 测试：AxisCommand / GroupCommand → 寄存器写入      │
├──────────────────────────────────────────────────────┤
│  阶段三：状态推导引擎单元测试（deriveAxisState）         │
│  └── 纯函数，无外部依赖，可直接测试                      │
├──────────────────────────────────────────────────────┤
│  阶段二：EdgeTrigger 脉冲管理单元测试                   │
│  └── 依赖：Mock PlcDevice（验证 TRUE→150ms→FALSE）     │
├──────────────────────────────────────────────────────┤
│  阶段一：寄存器选择器单元测试（regCmd* / regFb*）        │
│  └── 纯数据映射，无外部依赖，可直接测试                  │
└──────────────────────────────────────────────────────┘
```

### 1.2 RED-GREEN-REFACTOR 节奏

每个测试步骤严格遵循：

1. **RED**：先写测试，编译通过但运行失败（预期行为尚未实现）
2. **GREEN**：编写最小实现代码，使测试通过
3. **REFACTOR**：消除重复、改善设计，保持测试绿色

### 1.3 Mock 策略

| 被 Mock 对象 | Mock 方式 | 用途 |
|-------------|-----------|------|
| `IModbusClient` | 接口已有纯虚函数，手写 Mock（或 GoogleMock） | 模拟 Modbus 通讯层 |
| `PlcDevice` | 将 `writeBool` / `writeFloat` / `readBool` / `readInt16` 等方法设为 virtual，手写 Mock | 模拟寄存器读写 |
| `SystemContext` | 已有构造函数，直接构造真实对象 | 验证 `pollFeedback` 分发链路 |

### 1.4 设计文档 v4.0 关键变化（vs v3.0 TDD）

| 项目 | v3.0 TDD 文档 | v4.0 TDD 文档（本版） |
|------|-------------|---------------------|
| Move 命令 | `MoveCommand` 含 `MoveType` 字段 | 废弃；拆为 4 个独立命令 |
| 绝对定位 | `sendMoveCommand` 两步合并 | `SetAbsTargetCommand` + `TriggerAbsMoveCommand` 独立 |
| 相对定位 | `sendMoveCommand` 两步合并 | `SetRelTargetCommand` + `TriggerRelMoveCommand` 独立 |
| EmergencyStop | EdgeTrigger 型 | **Level 型**（直接 writeBool） |
| GantryPower | 独立寄存器 | 与 X 轴 Enable 共用 M0 |
| ZeroAbsolute | 经 HOME_TRIGGER | 经 **CLEAR_ABS_POS** (M30~M33) |
| 状态推导 | 仅 D100 + 运动 Coil | 新增 **ALARM_CODE** 冗余兜底 |
| 反馈 relZeroAbsPos | 从 PLC 读取 | **Domain 层闭环计算**（Driver 填 0.0） |
| 测试文件命名 | `test_system_driver_*.cpp` | `test_modbus_system_driver_*.cpp`（明确指向 Modbus 实现） |
| 阶段数量 | 4 阶段 | 6 阶段（新增寄存器选择器 + EdgeTrigger 独立阶段） |

---

## 2. 测试文件规划

```
tests/
├── infrastructure/
│   ├── test_modbus_system_driver_registers.cpp   ← 阶段一
│   ├── test_modbus_system_driver_edge_trigger.cpp ← 阶段二
│   ├── test_modbus_system_driver_state_derive.cpp ← 阶段三
│   ├── test_modbus_system_driver_send.cpp         ← 阶段四
│   ├── test_modbus_system_driver_poll_feedback.cpp← 阶段五
│   └── test_modbus_system_driver_integration.cpp  ← 阶段六
```

---

## 3. 阶段一：寄存器选择器单元测试

### 3.1 目标

验证 `ModbusSystemDriver` 中所有 `regCmd*` 和 `regFb*` 方法，对于每个 `AxisId` 输入返回正确的 `RegisterInfo`（寄存器地址 + 区域类型）。

### 3.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_registers.cpp`

### 3.3 RED 步骤 —— 先写测试

#### 测试用例 1：Enable 命令寄存器映射

验证：X→M0, Y→M1, Z→M2, R→M3；X1/X2 fallback 到 M0（本版本不开放 X1/X2 独立控制）。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, EnableRegisterMapping) {
    EXPECT_EQ(driver.regCmdEnable(AxisId::X).address, 0);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X).region, ModbusRegion::Coil);
    EXPECT_EQ(driver.regCmdEnable(AxisId::Y).address, 1);
    EXPECT_EQ(driver.regCmdEnable(AxisId::Z).address, 2);
    EXPECT_EQ(driver.regCmdEnable(AxisId::R).address, 3);
    // X1/X2 fallback to X (龙门模式)
    EXPECT_EQ(driver.regCmdEnable(AxisId::X1).address, 0);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X2).address, 0);
}
```

#### 测试用例 2：Jog 命令寄存器映射

验证：X→M50/M51 (X1_JOG_FWD/BWD), Y→M54/M55, Z→M56/M57, R→M58/M59。均为 Coil。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, JogForwardRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X).address, 50);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Y).address, 54);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Z).address, 56);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::R).address, 58);
    // X1/X2 fallback to X
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X1).address, 50);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X2).address, 50);
}

TEST_F(ModbusSystemDriverRegisterTest, JogBackwardRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::X).address, 51);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::Y).address, 55);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::Z).address, 57);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::R).address, 59);
}
```

#### 测试用例 3：SetAbsTarget / SetRelTarget D 寄存器映射

验证：ABS_TARGET: X→D20, Y→D24, Z→D28, R→D32；REL_TARGET: X→D22, Y→D26, Z→D30, R→D34。均为 HoldingRegister。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, AbsRelTargetRegisterMapping) {
    // ABS_TARGET
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::X).address, 20);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::X).region, ModbusRegion::HoldingRegister);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Y).address, 24);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Z).address, 28);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::R).address, 32);
    // REL_TARGET
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::X).address, 22);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::Y).address, 26);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::Z).address, 30);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::R).address, 34);
}
```

#### 测试用例 4：TriggerAbsMove / TriggerRelMove M 寄存器映射

验证：ABS_MOVE_TRIGGER: X→M40, Y→M42, Z→M44, R→M46；REL_MOVE_TRIGGER: X→M41, Y→M43, Z→M45, R→M47。均为 Coil。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, AbsRelTriggerRegisterMapping) {
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X).address, 40);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X).region, ModbusRegion::Coil);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::Y).address, 42);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::Z).address, 44);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::R).address, 46);
    EXPECT_EQ(driver.regCmdRelTrigger(AxisId::X).address, 41);
    EXPECT_EQ(driver.regCmdRelTrigger(AxisId::Y).address, 43);
    EXPECT_EQ(driver.regCmdRelTrigger(AxisId::Z).address, 45);
    EXPECT_EQ(driver.regCmdRelTrigger(AxisId::R).address, 47);
}
```

#### 测试用例 5：EdgeTrigger 命令寄存器映射

验证：SET_REL_ZERO: M14~M17, CLEAR_REL_ZERO: M18~M21, CLEAR_ABS_POS: M30~M33。均为 Coil。

> ⚠️ v4.0 变化：`ZeroAbsoluteCommand` 直接映射到 `CLEAR_ABS_POS` (M30~M33)，不再经 `HOME_TRIGGER`。本版本不开放回原点，`HOME_TRIGGER` 不注册。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, EdgeTriggerRegisterMapping) {
    EXPECT_EQ(driver.regCmdSetRelZero(AxisId::X).address, 14);
    EXPECT_EQ(driver.regCmdSetRelZero(AxisId::Y).address, 15);
    EXPECT_EQ(driver.regCmdSetRelZero(AxisId::Z).address, 16);
    EXPECT_EQ(driver.regCmdSetRelZero(AxisId::R).address, 17);

    EXPECT_EQ(driver.regCmdClearRelZero(AxisId::X).address, 18);
    EXPECT_EQ(driver.regCmdClearRelZero(AxisId::Y).address, 19);
    EXPECT_EQ(driver.regCmdClearRelZero(AxisId::Z).address, 20);
    EXPECT_EQ(driver.regCmdClearRelZero(AxisId::R).address, 21);

    EXPECT_EQ(driver.regCmdClearAbsPos(AxisId::X).address, 30);
    EXPECT_EQ(driver.regCmdClearAbsPos(AxisId::Y).address, 31);
    EXPECT_EQ(driver.regCmdClearAbsPos(AxisId::Z).address, 32);
    EXPECT_EQ(driver.regCmdClearAbsPos(AxisId::R).address, 33);
}
```

#### 测试用例 6：速度寄存器映射

验证：JOG_SPEED: D1000/D1004/D1008/D1012, MOVE_SPEED: D1002/D1006/D1010/D1014。均为 HoldingRegister。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, VelocityRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::X).address, 1000);
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::X).region, ModbusRegion::HoldingRegister);
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::Y).address, 1004);
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::Z).address, 1008);
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::R).address, 1012);

    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::X).address, 1002);
    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::Y).address, 1006);
    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::Z).address, 1010);
    EXPECT_EQ(driver.regCmdMoveSpeed(AxisId::R).address, 1014);
}
```

#### 测试用例 7：反馈寄存器映射

验证：ABS_POS: D120/D124/D128/D132, REL_POS: D122/D126/D130/D134, STATE: D100~D103, ALARM_CODE: D110~D113, 运动 Coil: M110~M121。

> 龙门 X 轴反馈以 X1 寄存器为准。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, FeedbackRegisterMapping) {
    // 位置反馈
    EXPECT_EQ(driver.regFbAbsPos(AxisId::X).address, 120);
    EXPECT_EQ(driver.regFbAbsPos(AxisId::Y).address, 124);
    EXPECT_EQ(driver.regFbRelPos(AxisId::X).address, 122);
    EXPECT_EQ(driver.regFbRelPos(AxisId::Y).address, 126);

    // 状态寄存器
    EXPECT_EQ(driver.regFbState(AxisId::X).address, 100);
    EXPECT_EQ(driver.regFbState(AxisId::Y).address, 101);
    EXPECT_EQ(driver.regFbState(AxisId::Z).address, 102);
    EXPECT_EQ(driver.regFbState(AxisId::R).address, 103);

    // 报警码
    EXPECT_EQ(driver.regFbAlarmCode(AxisId::X).address, 110);
    EXPECT_EQ(driver.regFbAlarmCode(AxisId::Y).address, 111);

    // 运动 Coil（均为 Coil 区域）
    EXPECT_EQ(driver.regFbAbsMoving(AxisId::X).address, 110);
    EXPECT_EQ(driver.regFbAbsMoving(AxisId::X).region, ModbusRegion::Coil);
    EXPECT_EQ(driver.regFbRelMoving(AxisId::X).address, 111);
    EXPECT_EQ(driver.regFbJogging(AxisId::X).address, 112);

    EXPECT_EQ(driver.regFbAbsMoving(AxisId::Y).address, 113);
    EXPECT_EQ(driver.regFbRelMoving(AxisId::Y).address, 114);
    EXPECT_EQ(driver.regFbJogging(AxisId::Y).address, 115);
}
```

#### 测试用例 8：组级命令寄存器映射

验证：LINKAGE_ENABLE (M4), ESTOP_TRIGGER (M80), ESTOP_ACTIVE (M130), GANTRY_ERROR_CODE (D180), LINKAGE_STATE (M125)。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, GroupCommandRegisterMapping) {
    // 龙门联动 (M4)
    EXPECT_EQ(driver.regGantryCoupling().address, 4);
    EXPECT_EQ(driver.regGantryCoupling().region, ModbusRegion::Coil);

    // 急停触发 (M80) — Level 型
    EXPECT_EQ(driver.regEmergencyStopTrigger().address, 80);
    EXPECT_EQ(driver.regEmergencyStopTrigger().region, ModbusRegion::Coil);
}
```

#### 测试用例 9：本版本排除的寄存器

验证：X2 独立控制寄存器（M52/M53/M70/M71/D40/D42）不暴露；HOME_TRIGGER（M10~M13）不暴露；X1/X2 fallback 到龙门。

> ⚠️ 本版本不开放 X1、X2 独立控制，也不开放回原点操作。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, ExcludedRegistersInCurrentVersion) {
    // X1/X2 fallback 到龙门
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X1).address, 50);  // fallback to X
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X2).address, 50);

    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::X1).address, 20);
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X1).address, 40);

    // 断言 HOME_TRIGGER 未注册：实现中不应提供 regCmdHomeTrigger 方法
    // （编译期保证，编译器找不到该符号即视为通过）
}
```

### 3.4 GREEN 步骤 —— 实现代码

在 `infrastructure/plc/ModbusSystemDriver.h` 中实现所有 `regCmd*` / `regFb*` 方法，每个方法内部为 `switch-case`，从 `RegisterRegistry` 获取已注册的 `RegisterInfo` 引用。

需要为 `RegisterRegistry` 新增一个 `get(const RegisterInfo& key)` 方法，通过地址 + 区域反查。

> ⚠️ 注意：不提供 `regCmdHomeTrigger` — 本版本不开放回原点。`ZeroAbsoluteCommand` 使用 `regCmdClearAbsPos`。

### 3.5 REFACTOR 步骤

- 抽取宏 `AXIS_ENUM_TO_REG(FAMILY, FIELD)` 减少 switch-case 重复
- 确认 X1/X2 fallback 行为有注释

---

## 4. 阶段二：EdgeTrigger 脉冲管理单元测试

### 4.1 目标

验证边沿触发协议：`writeBool(true)` → 记录时间戳 → 150ms后 → `servicePendingEdgeTriggers()` → `writeBool(false)`。

### 4.2 需要 EdgeTrigger 的命令（v4.0）

| 命令 | X 寄存器 | Y/Z/R 对应 |
|------|---------|-----------|
| `ZeroAbsoluteCommand` | CLEAR_ABS_POS (M30) | M31/M32/M33 |
| `SetRelativeZeroCommand` | SET_REL_ZERO (M14) | M15/M16/M17 |
| `ClearRelativeZeroCommand` | CLEAR_REL_ZERO (M18) | M19/M20/M21 |
| `TriggerAbsMoveCommand` | ABS_MOVE_TRIGGER (M40) | M42/M44/M46 |
| `TriggerRelMoveCommand` | REL_MOVE_TRIGGER (M41) | M43/M45/M47 |

> ⚠️ v4.0 变化：`EmergencyStopCommand` 改为 Level 型，不再使用 EdgeTrigger。`ZeroAbsoluteCommand` 使用 CLEAR_ABS_POS 而非 HOME_TRIGGER。

### 4.3 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_edge_trigger.cpp`

### 4.4 Mock 设计

将 `sendEdgeTrigger` 和 `servicePendingEdgeTriggers` 暴露为 `public`（或 `protected` + `FRIEND_TEST`），允许测试直接调用。同时暴露 `advanceTime()` 和 `pendingEdgeCount()` 以控制时间推进。

引入 `IClock` 接口用于时间注入：

```cpp
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};
```

### 4.5 RED 步骤 —— 先写测试

#### 测试用例 1：单次 EdgeTrigger 完整生命周期

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, SingleEdgeTriggerFullLifecycle) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;  // M30

    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    auto result = driver_->sendEdgeTrigger(reg);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 时间未到 150ms，不应发送 OFF
    driver_->servicePendingEdgeTriggers();
    // StrictMock 保证无多余调用

    // 推进 150ms → 应发送 OFF
    driver_->advanceTime(std::chrono::milliseconds(150));
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 2：多个并发 EdgeTrigger 独立计时

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, MultipleConcurrentEdgeTriggers) {
    const auto& regA = plc::reg::x_axis::command::CLEAR_ABS_POS;     // M30
    const auto& regB = plc::reg::y_axis::command::CLEAR_ABS_POS;     // M31

    EXPECT_CALL(*mockDevice_, writeBool(regA, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regA, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(regA);
    driver_->advanceTime(std::chrono::milliseconds(10));
    driver_->sendEdgeTrigger(regB);
    EXPECT_EQ(driver_->pendingEdgeCount(), 2);

    driver_->advanceTime(std::chrono::milliseconds(140));
    driver_->servicePendingEdgeTriggers();  // regA 到时间，regB 差 10ms
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    driver_->advanceTime(std::chrono::milliseconds(10));
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 3：写入失败不应入队

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, EdgeTriggerWriteFailDoesNotEnqueue) {
    const auto& reg = plc::reg::x_axis::command::ABS_MOVE_TRIGGER;  // M40

    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::NetworkError()));
    // writeBool(false) 不应被调用

    auto result = driver_->sendEdgeTrigger(reg);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 4：OFF 脉冲写入失败仍清空队列

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, OffPulseFailureStillClearsQueue) {
    const auto& reg = plc::reg::x_axis::command::REL_MOVE_TRIGGER;  // M41

    EXPECT_CALL(*mockDevice_, writeBool(reg, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::NetworkError()));

    driver_->sendEdgeTrigger(reg);
    driver_->advanceTime(std::chrono::milliseconds(150));
    driver_->servicePendingEdgeTriggers();

    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 清空，防止内存泄漏
}
```

#### 测试用例 5：TriggerAbsMove / TriggerRelMove 均走 EdgeTrigger

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, MoveTriggerCommandsViaEdgeTrigger) {
    const auto& absTrig = plc::reg::x_axis::command::ABS_MOVE_TRIGGER;   // M40
    const auto& relTrig = plc::reg::x_axis::command::REL_MOVE_TRIGGER;   // M41

    EXPECT_CALL(*mockDevice_, writeBool(absTrig, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(absTrig, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    driver_->sendEdgeTrigger(absTrig);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    driver_->advanceTime(std::chrono::milliseconds(150));
    driver_->servicePendingEdgeTriggers();
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 4.6 GREEN 步骤 —— 实现代码

实现 `sendEdgeTrigger()`、`servicePendingEdgeTriggers()` 和在设计文档 §4.7 中已列出的逻辑。引入 `IClock` 接口，测试环境注入 `FakeClock`，生产环境注入 `SteadyClock`。

### 4.7 REFACTOR 步骤

- 提取 `IClock` 接口
- 考虑 `PendingEdge` 是否需要 `retryCount` 字段（当前版本不需要）
- 确认 `sendEdgeTrigger` 在 `sendAxisCommand()` 中正确用于：`ZeroAbsoluteCommand`、`SetRelativeZeroCommand`、`ClearRelativeZeroCommand`、`TriggerAbsMoveCommand`、`TriggerRelMoveCommand`

---

## 5. 阶段三：状态推导引擎单元测试

### 5.1 目标

验证 `deriveAxisState()` 方法的多信号融合逻辑，覆盖所有 D100 状态值、ALARM_CODE 与 Coil 信号的组合。这是**纯函数**，无外部依赖，可直接进行参数化测试。

### 5.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_state_derive.cpp`

### 5.3 推导规则回顾（v4.0）

```
输入: d100State, alarmCode, absMoving, relMoving, jogging

推导:
  if d100State == 3 || alarmCode != 0  → AxisState::Error     (双条件 OR)
  if d100State == 0                    → AxisState::Disabled   (未使能)

  // d100State ∈ {1, 2} 时，由 Coil 信号决定精确子状态
  if absMoving           → AxisState::MovingAbsolute
  if relMoving           → AxisState::MovingRelative
  if jogging             → AxisState::Jogging
  else                   → AxisState::Idle            (使能但无运动信号)
```

> ⚠️ v4.0 变化：`d100State == 3` **或** `alarmCode != 0` 任一成立即判定 Error。`alarmCode != 0` 是冗余兜底，覆盖 ALARM_CODE 先到达而 D100 尚未翻转的过渡场景。

### 5.4 测试用例清单与预期结果

| 编号 | D100 | AlarmCode | ABS_MOVING | REL_MOVING | JOGGING | 预期 AxisState | 注释 |
|------|------|-----------|------------|------------|---------|----------------|------|
| S1 | 0 | 0 | false | false | false | **Disabled** | 未使能 |
| S2 | 0 | 5 | false | false | false | **Error** | alarmCode ≠ 0 优先于 Disabled |
| S3 | 0 | -1 | false | false | false | **Error** | alarmCode 负数也判定 Error |
| S4 | 3 | 0 | false | false | false | **Error** | D100=3 是 PLC 标准报警码 |
| S5 | 3 | 0 | true | false | false | **Error** | Error 高于运动信号 |
| S6 | 3 | 7 | true | true | false | **Error** | D100=3 且 alarmCode≠0 双重确认 |
| S7 | 1 | 0 | true | false | false | **MovingAbsolute** | 绝对定位进行中 |
| S8 | 1 | 0 | false | true | false | **MovingRelative** | 相对定位进行中 |
| S9 | 1 | 0 | false | false | true | **Jogging** | 点动进行中 |
| S10 | 1 | 0 | false | false | false | **Idle** | 使能静止 |
| S11 | 2 | 0 | false | false | false | **Idle** | D100=2 但无运动 Coil |
| S12 | 2 | 0 | true | false | false | **MovingAbsolute** | D100=2 + ABS_MOVING |
| S13 | 2 | 0 | false | true | false | **MovingRelative** | D100=2 + REL_MOVING |
| S14 | 1 | 3 | false | false | false | **Error** | alarmCode ≠ 0 触发 Error |
| S15 | 2 | 5 | true | false | false | **Error** | alarmCode ≠ 0 优先级最高 |

#### 参数化测试（S1-S15）

```cpp
class ModbusSystemDriverStateDeriveTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver_ = std::make_unique<ModbusSystemDriver>(plc::protocol::INOVANCE_PROFILE);
        mockDevice_ = std::make_unique<NiceMock<MockPlcDevice>>();
        driver_->setDevice(mockDevice_.get());
    }

    void setPlcSignals(AxisId id, int16_t d100, int16_t alarm,
                       bool absM, bool relM, bool jog) {
        ON_CALL(*mockDevice_, readInt16(driver_->regFbState(id)))
            .WillByDefault(Return(d100));
        ON_CALL(*mockDevice_, readInt16(driver_->regFbAlarmCode(id)))
            .WillByDefault(Return(alarm));
        ON_CALL(*mockDevice_, readBool(driver_->regFbAbsMoving(id)))
            .WillByDefault(Return(absM));
        ON_CALL(*mockDevice_, readBool(driver_->regFbRelMoving(id)))
            .WillByDefault(Return(relM));
        ON_CALL(*mockDevice_, readBool(driver_->regFbJogging(id)))
            .WillByDefault(Return(jog));
    }

    std::unique_ptr<ModbusSystemDriver> driver_;
    std::unique_ptr<NiceMock<MockPlcDevice>> mockDevice_;
};

// 使用 TEST_P 参数化测试覆盖全部 15 种组合
struct StateTestParam {
    int16_t d100;
    int16_t alarm;
    bool absM, relM, jog;
    AxisState expected;
};

class StateDeriveParamTest
    : public ModbusSystemDriverStateDeriveTest,
      public ::testing::WithParamInterface<StateTestParam> {};

INSTANTIATE_TEST_SUITE_P(AllCombinations, StateDeriveParamTest,
    ::testing::Values(
        StateTestParam{0, 0, false, false, false, AxisState::Disabled},      // S1
        StateTestParam{0, 5, false, false, false, AxisState::Error},         // S2
        StateTestParam{0, -1, false, false, false, AxisState::Error},        // S3
        StateTestParam{3, 0, false, false, false, AxisState::Error},         // S4
        StateTestParam{3, 0, true,  false, false, AxisState::Error},         // S5
        StateTestParam{3, 7, true,  true,  false, AxisState::Error},         // S6
        StateTestParam{1, 0, true,  false, false, AxisState::MovingAbsolute},// S7
        StateTestParam{1, 0, false, true,  false, AxisState::MovingRelative},// S8
        StateTestParam{1, 0, false, false, true,  AxisState::Jogging},       // S9
        StateTestParam{1, 0, false, false, false, AxisState::Idle},          // S10
        StateTestParam{2, 0, false, false, false, AxisState::Idle},          // S11
        StateTestParam{2, 0, true,  false, false, AxisState::MovingAbsolute},// S12
        StateTestParam{2, 0, false, true,  false, AxisState::MovingRelative},// S13
        StateTestParam{1, 3, false, false, false, AxisState::Error},         // S14
        StateTestParam{2, 5, true,  false, false, AxisState::Error}          // S15
    ));

TEST_P(StateDeriveParamTest, DeriveAxisState) {
    auto& p = GetParam();
    setPlcSignals(AxisId::X, p.d100, p.alarm, p.absM, p.relM, p.jog);
    AxisState actual = driver_->deriveAxisState(AxisId::X);
    EXPECT_EQ(actual, p.expected)
        << "D100=" << p.d100 << " Alarm=" << p.alarm
        << " ABS_M=" << p.absM << " REL_M=" << p.relM << " JOG=" << p.jog;
}
```

#### 补充测试用例：多圈编码器抖动场景

验证 D100 在 1↔2 之间抖动时，不会被误判为运动状态。

```cpp
TEST_F(ModbusSystemDriverStateDeriveTest, D100JitterDoesNotFalseTrigger {
    // D100=2（运动态）但 AlarmCode=0 且无运动 Coil → 应为 Idle
    setPlcSignals(AxisId::X, 2, 0, false, false, false);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::X), AxisState::Idle);

    // D100=1 但有 JOGGING Coil ON → 应为 Jogging
    setPlcSignals(AxisId::X, 1, 0, false, false, true);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::X), AxisState::Jogging);
}
```

#### 补充测试用例：四轴独立推导

```cpp
TEST_F(ModbusSystemDriverStateDeriveTest, AllFourAxesIndependentDerive) {
    setPlcSignals(AxisId::X, 1, 0, false, false, false);  // Idle
    setPlcSignals(AxisId::Y, 1, 0, true,  false, false);  // MovingAbsolute
    setPlcSignals(AxisId::Z, 0, 0, false, false, false);  // Disabled
    setPlcSignals(AxisId::R, 3, 0, false, false, false);  // Error

    EXPECT_EQ(driver_->deriveAxisState(AxisId::X), AxisState::Idle);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::Y), AxisState::MovingAbsolute);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::Z), AxisState::Disabled);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::R), AxisState::Error);
}
```

### 5.5 GREEN 步骤 —— 实现代码

实现 `deriveAxisState(AxisId id)` 方法，内部逻辑严格按照 §5.3 推导规则。由于此方法是纯函数，可以直接放到 `ModbusSystemDriver` 的 `private` 方法中（或提取为独立 free function 以便测试）。

**推荐**：提取为独立 free function，签名如下：

```cpp
// 文件：infrastructure/plc/AxisStateDeriver.h
AxisState deriveAxisState(int16_t d100State, int16_t alarmCode,
                          bool absMoving, bool relMoving, bool jogging);
```

这样阶段三的测试可以直接调用纯函数，无需构造 `ModbusSystemDriver` 实例。

### 5.6 REFACTOR 步骤

- 确认 Error 判断条件为 `d100State == 3 || alarmCode != 0`（双条件 OR）
- 确认 Disabled 判断仅依赖 `d100State == 0`（在 Error 之后）
- 确认 D100 的 1↔2 抖跳已被 Coil 信号消除

---

## 6. 阶段四：命令分派单元测试

### 6.1 目标

验证 `send(SystemCommand)` 顶层分派以及 `sendAxisCommand()` 内部正确路由到对应的 `writeBool` / `writeFloat` / `sendEdgeTrigger`。

### 6.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_send.cpp`

### 6.3 Mock 设计

使用 `StrictMock<MockPlcDevice>` 确保所有 `writeBool` / `writeFloat` 调用都被预期，不存在遗漏。`send()` 方法内部通过 `m_device` 调用注册器读写。

### 6.4 测试用例清单

#### 6.4.1 Level 型轴命令

| 测试项 | SystemCommand | 预期行为 |
|--------|--------------|---------|
| 使能 | `AxisCommandWithId{X, EnableCommand{true}}` | `writeBool(M0, true)` |
| 掉电 | `AxisCommandWithId{Y, EnableCommand{false}}` | `writeBool(M1, false)` |
| X轴正向Jog | `AxisCommandWithId{X, JogCommand{Fwd, true}}` | `writeBool(M50, true)` |
| X轴反向Jog | `AxisCommandWithId{X, JogCommand{Bwd, true}}` | `writeBool(M51, true)` |
| 停止Jog | `AxisCommandWithId{X, StopCommand{}}` | `writeBool(M50, false)` + `writeBool(M51, false)` |
| 设置Jog速度 | `AxisCommandWithId{X, SetJogVelocity{100.0}}` | `writeFloat(D1000, 100.0f)` |
| 设置Move速度 | `AxisCommandWithId{X, SetMoveVelocity{200.0}}` | `writeFloat(D1002, 200.0f)` |

```cpp
TEST_F(ModbusSystemDriverSendTest, EnableCommandWritesCoil) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, EnableCommand{true}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

TEST_F(ModbusSystemDriverSendTest, StopCommandWritesBothJogCoilsOff) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, StopCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::X1_JOG_BACKWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

TEST_F(ModbusSystemDriverSendTest, SetJogVelocityWritesFloat) {
    SystemCommand cmd = AxisCommandWithId{AxisId::Y, SetJogVelocityCommand{150.0}};
    EXPECT_CALL(*mockDevice_, writeFloat(plc::reg::y_axis::command::JOG_SPEED, 150.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 6.4.2 四命令独立定位（v4.0 核心变化）

> ⚠️ v4.0 变化：`MoveCommand` 废弃，拆为 4 个独立命令。

```cpp
// SetAbsTarget → 仅写 D20 寄存器，不触发
TEST_F(ModbusSystemDriverSendTest, SetAbsTargetWritesOnlyDRegister) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, SetAbsTargetCommand{123.45}};
    EXPECT_CALL(*mockDevice_, writeFloat(plc::reg::x_axis::command::ABS_TARGET, 123.45f))
        .WillOnce(Return(CommunicationResult::Sent()));
    // 不应有任何 writeBool 调用（无 M40 触发）
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

// TriggerAbsMove → 仅发 M40 EdgeTrigger，不写 D 寄存器
TEST_F(ModbusSystemDriverSendTest, TriggerAbsMoveOnlyEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, TriggerAbsMoveCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}

// SetRelTarget → 仅写 D22 寄存器
TEST_F(ModbusSystemDriverSendTest, SetRelTargetWritesOnlyDRegister) {
    SystemCommand cmd = AxisCommandWithId{AxisId::Y, SetRelTargetCommand{-50.0}};
    EXPECT_CALL(*mockDevice_, writeFloat(plc::reg::y_axis::command::REL_TARGET, -50.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

// TriggerRelMove → 仅发 M41 EdgeTrigger
TEST_F(ModbusSystemDriverSendTest, TriggerRelMoveOnlyEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{AxisId::Z, TriggerRelMoveCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::z_axis::command::REL_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 6.4.3 EdgeTrigger 型轴命令

```cpp
TEST_F(ModbusSystemDriverSendTest, ZeroAbsoluteCommandViaClearAbsPos) {
    SystemCommand cmd = AxisCommandWithId{AxisId::X, ZeroAbsoluteCommand{}};
    // v4.0: 使用 CLEAR_ABS_POS (M30)，不再经 HOME_TRIGGER
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::CLEAR_ABS_POS, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}

TEST_F(ModbusSystemDriverSendTest, SetRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{AxisId::Y, SetRelativeZeroCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::y_axis::command::SET_REL_ZERO, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}

TEST_F(ModbusSystemDriverSendTest, ClearRelativeZeroCommandEdgeTrigger) {
    SystemCommand cmd = AxisCommandWithId{AxisId::R, ClearRelativeZeroCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::r_axis::command::CLEAR_REL_ZERO, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}
```

#### 6.4.4 组级命令

```cpp
// GantryCoupling → writeBool(M4, ...)
TEST_F(ModbusSystemDriverSendTest, GantryCouplingCommandLevelType) {
    SystemCommand cmd = GantryCouplingCommand{/*enableCoupling=*/true};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::LINKAGE_ENABLE, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

// GantryPower → writeBool(M0, ...) 与 X Enable 共用
TEST_F(ModbusSystemDriverSendTest, GantryPowerCommandSharesM0WithXEnable) {
    SystemCommand cmd = GantryPowerCommand{/*enable=*/true};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
}

// EmergencyStop → Level 型，直接 writeBool(M80, ...)，不使用 EdgeTrigger
TEST_F(ModbusSystemDriverSendTest, EmergencyStopCommandLevelType) {
    SystemCommand cmd = EmergencyStopCommand{/*active=*/true};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    // 确保未进入 EdgeTrigger 队列
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}

TEST_F(ModbusSystemDriverSendTest, ReleaseEmergencyStopCommandLevelType) {
    SystemCommand cmd = EmergencyStopCommand{/*active=*/false};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 6.4.5 向后兼容：MoveCommand（已废弃）

> ⚠️ MoveCommand 分支保留仅用于向后兼容，测试确认其行为后，后续可安全删除。

```cpp
TEST_F(ModbusSystemDriverSendTest, MoveCommandAbsoluteBackwardCompatibility) {
    // MoveCommand 含 MoveType::Absolute → 写 D20 + EdgeTrigger M40
    AxisCommand mc = MoveCommand{MoveType::Absolute, 300.0, 0.0};
    SystemCommand cmd = AxisCommandWithId{AxisId::X, mc};

    EXPECT_CALL(*mockDevice_, writeFloat(plc::reg::x_axis::command::ABS_TARGET, 300.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));

    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

### 6.5 GREEN 步骤 —— 实现代码

实现设计文档 §4.4 `send()` 和 §4.5 `sendAxisCommand()` 中的完整分派逻辑（`std::visit` 嵌套）。同时实现 §4.6 组级命令。

### 6.6 REFACTOR 步骤

- `sendAxisCommand()` 中确认 `ZeroAbsoluteCommand` 走 `regCmdClearAbsPos`，不走 `regCmdHomeTrigger`
- `MoveCommand` 分支加 `[[deprecated]]` 注释
- 确认 `EmergencyStopCommand` 是 `writeBool` 而非 `sendEdgeTrigger`
- 确认 `GantryPowerCommand` 映射到 `regCmdEnable(AxisId::X)`（即 M0）

---

## 7. 阶段五：反馈轮询集成测试

### 7.1 目标

验证 `pollFeedback(SystemContext&)` 完整链路：
1. `servicePendingEdgeTriggers()`
2. `m_poller.buildRequest()` → `m_poller.poll()` → `m_device.updateSnapshot()`
3. `readAxisFeedback()` → `deriveAxisState()` → 构造 `AxisFeedback` → `axis->applyFeedback()`
4. `readSystemFeedback()` → `ctx.setEmergencyStopActive()` / `ctx.setGantryErrorCode()` / `ctx.setGantryCoupled()`

### 7.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_poll_feedback.cpp`

### 7.3 Mock 设计

同时 Mock `IModbusClient` 和 `PlcDevice`。`PollRequest` / `PlcSnapshot` 直接构造而不需要 mock。

### 7.4 测试用例清单

#### 测试用例 1：正常反馈轮询 → SystemContext 更新

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, NormalFeedbackUpdatesSystemContext) {
    // GIVEN: PLC 返回 X 轴 Idle 状态 + 已知位置
    //        Y 轴 Disabled, Z 轴 MovingAbsolute, R 轴 Error
    //        EStop 未激活, 龙门未联动, 误差码=0

    setupMockPlcResponses({
        // X axis signals
        { /* D100 */ 1, /* ALARM */ 0, /* ABS_M */ false, /* REL_M */ false,
          /* JOG */ false, /* ABS_POS */ 150.0f, /* REL_POS */ 25.0f },
        // Y axis signals
        { /* D101 */ 0, /* ALARM */ 0, /* ABS_M */ false, /* REL_M */ false,
          /* JOG */ false, /* ABS_POS */ 0.0f,   /* REL_POS */ 0.0f },
        // Z axis signals
        { /* D102 */ 1, /* ALARM */ 0, /* ABS_M */ true,  /* REL_M */ false,
          /* JOG */ false, /* ABS_POS */ 200.0f, /* REL_POS */ 50.0f },
        // R axis signals
        { /* D103 */ 3, /* ALARM */ 12,/* ABS_M */ false, /* REL_M */ false,
          /* JOG */ false, /* ABS_POS */ 0.0f,   /* REL_POS */ 0.0f },
        // System: ESTOP_ACTIVE=false, LINKAGE_STATE=false, GANTRY_ERROR_CODE=0
    });

    // WHEN: pollFeedback 被调用
    SystemContext ctx;
    Axis x, y, z, r;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    ctx.registerAxis(AxisId::Y, &y, "Group1");
    ctx.registerAxis(AxisId::Z, &z, "Group1");
    ctx.registerAxis(AxisId::R, &r, "Group1");

    driver_->pollFeedback(ctx);

    // THEN: 所有轴状态正确注入
    EXPECT_EQ(x.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(x.currentAbsolutePosition(), 150.0);
    EXPECT_DOUBLE_EQ(x.currentRelativePosition(), 25.0);

    EXPECT_EQ(y.state(), AxisState::Disabled);
    EXPECT_EQ(z.state(), AxisState::MovingAbsolute);
    EXPECT_EQ(r.state(), AxisState::Error);

    EXPECT_FALSE(ctx.isEmergencyStopActive());
    EXPECT_FALSE(ctx.isGantryCoupled());
    EXPECT_EQ(ctx.gantryErrorCode(), 0);
}
```

#### 测试用例 2：龙门联动反馈 → SystemContext 联动状态更新

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, GantryCoupledFeedback) {
    setupMockPlcResponses(/*LINKAGE_STATE=*/ true, /*GANTRY_ERROR_CODE=*/ 0x0001);
    SystemContext ctx;
    driver_->pollFeedback(ctx);

    EXPECT_TRUE(ctx.isGantryCoupled());
    EXPECT_EQ(ctx.gantryErrorCode(), 0x0001);
}
```

#### 测试用例 3：急停状态反馈 → SystemContext 急停标志

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, EmergencyStopActiveFeedback) {
    setupMockPlcResponses(/*ESTOP_ACTIVE=*/ true);
    SystemContext ctx;
    driver_->pollFeedback(ctx);

    EXPECT_TRUE(ctx.isEmergencyStopActive());
}
```

#### 测试用例 4：pollFeedback 先调度 EdgeTrigger 再读取反馈

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, EdgeTriggersServicedBeforeFeedbackRead) {
    // GIVEN: 有一个待处理 EdgeTrigger（M30 ON 已过 150ms）
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;
    driver_->sendEdgeTrigger(reg);
    driver_->advanceTime(std::chrono::milliseconds(150));

    // 期望先写 M30=false，再读取 PLC 反馈
    EXPECT_CALL(*mockDevice_, writeBool(reg, false))
        .WillOnce(Return(CommunicationResult::Sent()));

    // ... 后续读取调用 ...

    SystemContext ctx;
    driver_->pollFeedback(ctx);

    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 5：relZeroAbsPos 不从 PLC 读取（v4.0 变化）

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, RelZeroAbsPosNotReadFromPlc) {
    // v4.0: relZeroAbsPos 由 Domain 层 SetRelativeZero 闭环计算，
    //       Driver 在 AxisFeedback 中填写 0.0
    setupMockPlcResponses(/*ABS_POS=*/ 150.0);

    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    driver_->pollFeedback(ctx);

    // relZeroAbsPos 应由 Domain 层闭环计算，Driver 不提供
    // 验证 Axis 内部已有自己的默认值（0.0）
    EXPECT_DOUBLE_EQ(x.relativeZeroAbsolutePosition(), 0.0);
}
```

#### 测试用例 6：stateUntrusted 时不注入反馈

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, StateUntrustedDoesNotInjectFeedback) {
    // GIVEN: PlcDevice::isStateTrusted() 返回 false
    EXPECT_CALL(*mockDevice_, isStateTrusted())
        .WillRepeatedly(Return(false));

    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");

    auto before = x.state();
    driver_->pollFeedback(ctx);

    // 状态不应改变
    EXPECT_EQ(x.state(), before);
}
```

### 7.5 GREEN 步骤 —— 实现代码

实现设计文档 §4.9 `pollFeedback()`、`readAxisFeedback()`、`readSystemFeedback()` 和 `deriveAxisState()` 方法。

### 7.6 REFACTOR 步骤

- 确认 `relZeroAbsPos` 填 0.0（由 Domain 层闭环计算）
- 确认 `pollFeedback` 在 `m_device.isStateTrusted()` 为 false 时提前返回
- 确认龙门 X 轴反馈以 X1 寄存器为准
- 确认 `posLimit` / `negLimit` 字段添加 TODO 注释（当前从 SOFT_LIMIT_STATE 读取，后续实现）

---

## 8. 阶段六：系统级集成测试

### 8.1 目标

端到端验证 `ModbusSystemDriver` → `SystemContext` → `Axis` 的完整链路，确保命令发送和反馈轮询正确协作。

### 8.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_integration.cpp`

### 8.3 测试策略

使用 `FakeAxisDriver` 的现有集成测试作为参考，将 `FakeAxisDriver` 替换为 `ModbusSystemDriver + MockPlcDevice + MockIModbusClient`。

### 8.4 测试用例清单

#### 测试用例 1：使能 → 反馈确认 → 轴状态变为 Idle

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, EnableThenPollYieldsIdleState) {
    // Step 1: 发送使能命令
    SystemCommand enableCmd = AxisCommandWithId{AxisId::X, EnableCommand{true}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(enableCmd);
    EXPECT_TRUE(result.ok());

    // Step 2: 模拟 PLC 返回 D100=1（使能状态），无运动 Coil
    setupPlcFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                     /*ABS_M=*/false, /*REL_M=*/false, /*JOG=*/false,
                     /*ABS_POS=*/100.0, /*REL_POS=*/0.0);

    // Step 3: pollFeedback
    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    driver_->pollFeedback(ctx);

    // Step 4: 断言轴状态
    EXPECT_EQ(x.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(x.currentAbsolutePosition(), 100.0);
}
```

#### 测试用例 2：绝对定位四命令流程（SetAbsTarget → TriggerAbsMove → 运动反馈 → 完成）

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, AbsoluteMoveFourCommandFlow) {
    // Step 1: SetAbsTarget — 仅写 D20
    SystemCommand setTarget = AxisCommandWithId{AxisId::X, SetAbsTargetCommand{500.0}};
    EXPECT_CALL(*mockDevice_, writeFloat(plc::reg::x_axis::command::ABS_TARGET, 500.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_TRUE(driver_->send(setTarget).ok());

    // Step 2: TriggerAbsMove — 仅发 M40 EdgeTrigger
    SystemCommand trigger = AxisCommandWithId{AxisId::X, TriggerAbsMoveCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_TRUE(driver_->send(trigger).ok());

    // Step 3: 模拟运动中反馈 (ABS_MOVING=true)
    setupPlcFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                     /*ABS_M=*/true, /*REL_M=*/false, /*JOG=*/false,
                     /*ABS_POS=*/250.0, /*REL_POS=*/0.0);

    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    driver_->pollFeedback(ctx);
    EXPECT_EQ(x.state(), AxisState::MovingAbsolute);

    // Step 4: 模拟运动完成 (ABS_MOVING=false, D100=1 → Idle)
    setupPlcFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                     /*ABS_M=*/false, /*REL_M=*/false, /*JOG=*/false,
                     /*ABS_POS=*/500.0, /*REL_POS=*/0.0);
    driver_->pollFeedback(ctx);
    EXPECT_EQ(x.state(), AxisState::Idle);
    EXPECT_DOUBLE_EQ(x.currentAbsolutePosition(), 500.0);
}
```

#### 测试用例 3：循环 Tick 下 EdgeTrigger 自动清除

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, EdgeTriggerAutoClearOverTickLoop) {
    // send ZeroAbsolute → EdgeTrigger 入队
    SystemCommand cmd = AxisCommandWithId{AxisId::X, ZeroAbsoluteCommand{}};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::CLEAR_ABS_POS, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->send(cmd);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 第一个 tick：时间未到 150ms，不清除
    driver_->pollFeedback(ctx_);
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);

    // 推进时间 + 第二个 tick：发送 OFF
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::CLEAR_ABS_POS, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->advanceTime(std::chrono::milliseconds(150));
    driver_->pollFeedback(ctx_);
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

#### 测试用例 4：报表状态触发 → SystemContext 更新

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, AlarmStateUpdatesSystemContext) {
    // 模拟 PLC 返回 ALARM_CODE=5, D100=3
    setupPlcFeedback(AxisId::X, /*D100=*/3, /*ALARM=*/5,
                     /*ABS_M=*/false, /*REL_M=*/false, /*JOG=*/false,
                     /*ABS_POS=*/0.0, /*REL_POS=*/0.0);

    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    driver_->pollFeedback(ctx);
    EXPECT_EQ(x.state(), AxisState::Error);
}
```

#### 测试用例 5：急停命令 Level 型（不触发 EdgeTrigger）

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, EmergencyStopLevelTypeNoEdgeTrigger) {
    // 发送急停 → 直接写 M80
    SystemCommand stopCmd = EmergencyStopCommand{true};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->send(stopCmd);
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 无 EdgeTrigger

    // 轮询反馈看到 ESTOP_ACTIVE
    setupPlcFeedback(/*ESTOP_ACTIVE=*/ true);
    SystemContext ctx;
    driver_->pollFeedback(ctx);
    EXPECT_TRUE(ctx.isEmergencyStopActive());

    // 解除急停
    SystemCommand releaseCmd = EmergencyStopCommand{false};
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    driver_->send(releaseCmd);
}
```

#### 测试用例 6：StopCommand 送入后轴状态变化验证

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, StopCommandClearsBothJogCoils {
    // X 轴 Jogging → Stop
    setupPlcFeedback(AxisId::X, /*D100=*/1, /*ALARM=*/0,
                     /*ABS_M=*/false, /*REL_M=*/false, /*JOG=*/true,
                     /*ABS_POS=*/0.0, /*REL_POS=*/0.0);

    SystemContext ctx;
    Axis x;
    ctx.registerAxis(AxisId::X, &x, "Group1");
    driver_->pollFeedback(ctx);
    EXPECT_EQ(x.state(), AxisState::Jogging);

    // 发送 Stop
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::X1_JOG_FORWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(plc::reg::x_axis::command::X1_JOG_BACKWARD, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    SystemCommand stopCmd = AxisCommandWithId{AxisId::X, StopCommand{}};
    driver_->send(stopCmd);
}
```

### 8.5 GREEN 步骤 —— 实现代码

`pollFeedback` / `readAxisFeedback` / `readSystemFeedback` 已在阶段五实现，阶段六聚焦于验证完整链路的正确性。

---

## 9. CMakeLists 变更清单

### 9.1 新增源文件

```cmake
# infrastructure/CMakeLists.txt
add_library(infrastructure
    # ... 现有文件 ...
    plc/ModbusSystemDriver.h       # 新增
    plc/ModbusSystemDriver.cpp     # 新增
    plc/AxisStateDeriver.h         # 新增（状态推导纯函数）
    plc/AxisStateDeriver.cpp       # 新增
)
```

### 9.2 新增测试文件

```cmake
# tests/CMakeLists.txt
add_test_target(test_modbus_system_driver_registers
    infrastructure/test_modbus_system_driver_registers.cpp
)
add_test_target(test_modbus_system_driver_edge_trigger
    infrastructure/test_modbus_system_driver_edge_trigger.cpp
)
add_test_target(test_modbus_system_driver_state_derive
    infrastructure/test_modbus_system_driver_state_derive.cpp
)
add_test_target(test_modbus_system_driver_send
    infrastructure/test_modbus_system_driver_send.cpp
)
add_test_target(test_modbus_system_driver_poll_feedback
    infrastructure/test_modbus_system_driver_poll_feedback.cpp
)
add_test_target(test_modbus_system_driver_integration
    infrastructure/test_modbus_system_driver_integration.cpp
)
target_link_libraries(test_modbus_system_driver_registers gtest_main infrastructure domain)
target_link_libraries(test_modbus_system_driver_edge_trigger gtest_main infrastructure)
target_link_libraries(test_modbus_system_driver_state_derive gtest_main infrastructure domain)
target_link_libraries(test_modbus_system_driver_send gtest_main infrastructure domain)
target_link_libraries(test_modbus_system_driver_poll_feedback gtest_main infrastructure domain)
target_link_libraries(test_modbus_system_driver_integration gtest_main infrastructure domain)
```

### 9.3 新增依赖

```cmake
# tests/CMakeLists.txt — Mock 支持
# 需要 GTest/GMock (已集成于 external/googletest)
# 模拟 PlcDevice 的 virtual 方法：
#   - writeBool() / writeFloat()
#   - readBool() / readInt16() / readFloat()
#   - isStateTrusted()
```

---

## 10. 实施顺序与里程碑

### 10.1 实施顺序

```
Day 1 (阶段一 + 阶段二)
  ├── 08:00-10:00  阶段一 RED：寄存器选择器测试用例编写
  ├── 10:00-12:00  阶段一 GREEN/REFACTOR：regCmd*/regFb* 方法实现
  ├── 13:00-15:00  阶段二 RED：EdgeTrigger 脉冲管理测试
  └── 15:00-17:00  阶段二 GREEN/REFACTOR：sendEdgeTrigger + servicePendingEdgeTriggers

Day 2 (阶段三 + 阶段四)
  ├── 08:00-10:00  阶段三 RED：状态推导参数化测试
  ├── 10:00-11:00  阶段三 GREEN/REFACTOR：AxisStateDeriver 纯函数
  ├── 11:00-15:00  阶段四 RED：命令分派测试（Level 型 + 四命令定位 + EdgeTrigger 型 + 组级）
  └── 15:00-17:00  阶段四 GREEN/REFACTOR：send() + sendAxisCommand()

Day 3 (阶段五 + 阶段六)
  ├── 08:00-12:00  阶段五 RED+GREEN：pollFeedback 反馈轮询集成
  ├── 13:00-15:00  阶段六 RED：系统级集成测试
  ├── 15:00-17:00  阶段六 GREEN/REFACTOR：修复集成问题 + CMakeLists 更新
  └── 17:00-18:00  Full Build & All Tests Green
```

### 10.2 里程碑定义

| 里程碑 | 完成标准 | 方法验证 |
|--------|---------|---------|
| M1: 寄存器映射完成 | 阶段一全部 test case 通过 | `ctest -R test_modbus_system_driver_registers` |
| M2: EdgeTrigger 完成 | 阶段二全部 test case 通过 | `ctest -R test_modbus_system_driver_edge_trigger` |
| M3: 状态推导完成 | 阶段三 15 项参数化 + 3 项补充全部通过 | `ctest -R test_modbus_system_driver_state_derive` |
| M4: 命令分派完成 | 阶段四全部 test case 通过（含 4 命令定位 + Level/EdgeTrigger 区分） | `ctest -R test_modbus_system_driver_send` |
| M5: 反馈轮询完成 | 阶段五全部 test case 通过 | `ctest -R test_modbus_system_driver_poll_feedback` |
| M6: 系统集成完成 | 阶段六全部 test case 通过 | `ctest -R test_modbus_system_driver_integration` |
| M7: Full Regression | 全部 6 个测试 target + 现有测试全绿 | `ctest` |

### 10.3 风险与注意事项

| 风险项 | 等级 | 缓解措施 |
|--------|------|---------|
| PlcDevice 方法非 virtual → 无法 Mock | 🔴 高 | 将 `writeBool`/`writeFloat`/`readBool`/`readInt16`/`isStateTrusted` 改为 virtual |
| RegisterRegistry 无 `get()` 反查方法 | 🟡 中 | 阶段一 GREEN 前新增 `RegisterInfo get(const RegisterInfo& key)` |
| PollRequest 构造复杂 | 🟡 中 | 阶段五提供辅助函数 `buildStandardPollRequest()` |
| X1/X2 独立控制未来扩展 | 🟢 低 | 当前 fallback 到 X，预留注释标注未来寄存器地址 |
| HOME_TRIGGER 未注册 | 🟢 低 | 编译期保证（不提供 `regCmdHomeTrigger`），ZeroAbsolute 用 CLEAR_ABS_POS |

---

## 附录 A：v4.0 与 v3.0 测试差异清单

| 测试层面 | v3.0（旧） | v4.0（新） | 影响 |
|---------|-----------|-----------|------|
| 寄存器映射 | 不经测试，直接实现在 send 内部 | ❗ 阶段一独立测试所有 regCmd/regFb | 新增 30+ test case |
| EdgeTrigger | 混合在 send 测试中 | ❗ 阶段二独立测试生命周期 | 新增 5 test case |
| MoveCommand | `sendMoveCommand` 一次测试覆盖 | 拆为 4 test case：SetAbsTarget / TriggerAbsMove / SetRelTarget / TriggerRelMove | 拆分 + 新增 |
| EmergencyStop | 测试 EdgeTrigger 生命周期 | ❗ 测试 writeBool Level 型 + 无 EdgeTrigger 入队 | 2 test case 变更 |
| ZeroAbsolute | 映射 HOME_TRIGGER | ❗ 映射 CLEAR_ABS_POS | 寄存器地址变更 |
| 状态推导 | 仅 8 种组合 | ❗ 15 种参数化 + ALARM_CODE 冗余 | 新增 alarmCode 字段 |
| relZeroAbsPos | 从 PLC 读取测试 | ❗ 验证 Driver 填 0.0 | 断言变更 |

## 附录 B：MockPlcDevice 接口定义

```cpp
// tests/infrastructure/mock/MockPlcDevice.h
#pragma once

#include "gmock/gmock.h"
#include "infrastructure/plc/PlcDevice.h"

class MockPlcDevice : public PlcDevice {
public:
    MOCK_METHOD(CommunicationResult, writeBool,
                (const RegisterInfo& reg, bool value), (override));
    MOCK_METHOD(CommunicationResult, writeFloat,
                (const RegisterInfo& reg, float value), (override));
    MOCK_METHOD(std::optional<bool>, readBool,
                (const RegisterInfo& reg), (const, override));
    MOCK_METHOD(std::optional<int16_t>, readInt16,
                (const RegisterInfo& reg), (const, override));
    MOCK_METHOD(std::optional<float>, readFloat,
                (const RegisterInfo& reg), (const, override));
    MOCK_METHOD(bool, isStateTrusted, (), (const, override));
};
```

## 附录 C：IClock 接口定义

```cpp
// infrastructure/utils/IClock.h
#pragma once

#include <chrono>

class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};

class SteadyClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override {
        return std::chrono::steady_clock::now();
    }
};

// tests/infrastructure/mock/FakeClock.h
class FakeClock : public IClock {
public:
    std::chrono::steady_clock::time_point now() const override { return now_; }
    void advance(std::chrono::milliseconds ms) { now_ += ms; }
private:
    std::chrono::steady_clock::time_point now_{};
};
```

## 附录 D：构建与运行命令

```bash
# 配置（在 build 目录下）
cd f:/project/servoV6/build
cmake ..

# 构建全部
cmake --build . --target all

# 运行全部测试
ctest --output-on-failure

# 运行特定阶段测试
ctest -R test_modbus_system_driver_registers --output-on-failure
ctest -R test_modbus_system_driver_state_derive --output-on-failure
ctest -R test_modbus_system_driver_send --output-on-failure

# 运行单项测试（例）
./tests/infrastructure/test_modbus_system_driver_state_derive \
    --gtest_filter='*D100Jitter*'
```
