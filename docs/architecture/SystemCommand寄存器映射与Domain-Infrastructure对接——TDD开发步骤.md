# SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤

> 版本：v1.0  
> 日期：2026-05-28  
> 项目：servoV6  
> 前置设计文档：[SystemCommand寄存器映射与Domain-Infrastructure对接设计.md](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md)

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

验证 `ModbusSystemDriver` 中所有 `regCmd*` 和 `regFb*` 方法，对于每个 `AxisId` 输入返回正确的 `RegisterInfo`（寄存器地址 + 类型）。

### 3.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_registers.cpp`

### 3.3 RED 步骤 —— 先写测试

#### 测试用例 1：Enable 命令寄存器映射

验证：X→M0, Y→M1, Z→M2, R→M3；X1/X2 fallback 到 M0。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, EnableRegisterMapping) {
    EXPECT_EQ(driver.regCmdEnable(AxisId::X).address, 0);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X).region, ModbusRegion::Coil);
    EXPECT_EQ(driver.regCmdEnable(AxisId::Y).address, 1);
    EXPECT_EQ(driver.regCmdEnable(AxisId::Z).address, 2);
    EXPECT_EQ(driver.regCmdEnable(AxisId::R).address, 3);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X1).address, 0);
    EXPECT_EQ(driver.regCmdEnable(AxisId::X2).address, 0);
}
```

#### 测试用例 2：Jog 命令寄存器映射

验证：X→M50/M51 (X1_JOG_FWD/BWD), Y→M54/M55, Z→M56/M57, R→M58/M59。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, JogForwardRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X).address, 50);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Y).address, 54);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::Z).address, 56);
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::R).address, 58);
}
TEST_F(ModbusSystemDriverRegisterTest, JogBackwardRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::X).address, 51);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::Y).address, 55);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::Z).address, 57);
    EXPECT_EQ(driver.regCmdJogBwd(AxisId::R).address, 59);
}
```

#### 测试用例 3：Move 目标寄存器映射

验证：ABS_TARGET: X→D20, Y→D24, Z→D28, R→D32；REL_TARGET: X→D22, Y→D26, Z→D30, R→D34。均为 HoldingRegister。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, MoveTargetRegisterMapping) {
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::X).address, 20);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Y).address, 24);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::Z).address, 28);
    EXPECT_EQ(driver.regCmdAbsTarget(AxisId::R).address, 32);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::X).address, 22);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::Y).address, 26);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::Z).address, 30);
    EXPECT_EQ(driver.regCmdRelTarget(AxisId::R).address, 34);
}
```

#### 测试用例 4：Move 触发器寄存器映射

验证：ABS_MOVE_TRIGGER: X→M40, Y→M42, Z→M44, R→M46；REL_MOVE_TRIGGER: X→M41, Y→M43, Z→M45, R→M47。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, MoveTriggerRegisterMapping) {
    EXPECT_EQ(driver.regCmdAbsTrigger(AxisId::X).address, 40);
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

验证：SET_REL_ZERO: M14~M17, CLEAR_REL_ZERO: M18~M21, CLEAR_ABS_POS: M30~M33。

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

验证：JOG_SPEED: D1000/D1004/D1008/D1012, MOVE_SPEED: D1002/D1006/D1010/D1014。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, VelocityRegisterMapping) {
    EXPECT_EQ(driver.regCmdJogSpeed(AxisId::X).address, 1000);
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

```cpp
TEST_F(ModbusSystemDriverRegisterTest, FeedbackRegisterMapping) {
    EXPECT_EQ(driver.regFbAbsPos(AxisId::X).address, 120);
    EXPECT_EQ(driver.regFbRelPos(AxisId::X).address, 122);
    EXPECT_EQ(driver.regFbState(AxisId::X).address, 100);
    EXPECT_EQ(driver.regFbState(AxisId::Y).address, 101);
    EXPECT_EQ(driver.regFbAlarmCode(AxisId::X).address, 110);
    EXPECT_EQ(driver.regFbAbsMoving(AxisId::X).address, 110);
    EXPECT_EQ(driver.regFbRelMoving(AxisId::X).address, 111);
    EXPECT_EQ(driver.regFbJogging(AxisId::X).address, 112);
}
```

#### 测试用例 8：本版本排除的寄存器

验证：X2 独立控制寄存器不暴露，HOME_TRIGGER 不暴露，X1/X2 fallback 到龙门。

```cpp
TEST_F(ModbusSystemDriverRegisterTest, ExcludedRegistersInCurrentVersion) {
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X1).address, 50);  // fallback to X
    EXPECT_EQ(driver.regCmdJogFwd(AxisId::X2).address, 50);
}
```

### 3.4 GREEN 步骤 —— 实现代码

在 `infrastructure/plc/ModbusSystemDriver.h` 中实现所有 `regCmd*` / `regFb*` 方法，每个方法内部为 `switch-case`，从 `RegisterRegistry` 获取已注册的 `RegisterInfo` 引用。

需要为 `RegisterRegistry` 新增一个 `get(const RegisterInfo& key)` 方法，通过地址 + 区域反查。

### 3.5 REFACTOR 步骤

- 抽取宏 `AXIS_ENUM_TO_REG(FAMILY, FIELD)` 减少重复
- 确认 X1/X2 fallback 行为有注释

---

## 4. 阶段二：EdgeTrigger 脉冲管理单元测试

### 4.1 目标

验证边沿触发协议：`writeBool(true)` → 记录时间戳 → 150ms后 → `servicePendingEdgeTriggers()` → `writeBool(false)`。

### 4.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_edge_trigger.cpp`

### 4.3 Mock 设计

将 `sendEdgeTrigger` 和 `servicePendingEdgeTriggers` 暴露为 `public`（或 `protected` + `FRIEND_TEST`），允许测试直接调用。同时暴露 `advanceTime()` 和 `pendingEdgeCount()` 以控制时间推进。

引入 `IClock` 接口用于时间注入：

```cpp
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() const = 0;
};
```

### 4.4 RED 步骤 —— 先写测试

#### 测试用例 1：单次 EdgeTrigger 完整生命周期

```cpp
TEST_F(ModbusSystemDriverEdgeTriggerTest, SingleEdgeTriggerFullLifecycle) {
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;

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
    const auto& regA = plc::reg::x_axis::command::CLEAR_ABS_POS;   // M30
    const auto& regB = plc::reg::y_axis::command::CLEAR_ABS_POS;   // M31

    EXPECT_CALL(*mockDevice_, writeBool(regA, true)).WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, true)).WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regA, false)).WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(regB, false)).WillOnce(Return(CommunicationResult::Sent()));

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
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;

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
    const auto& reg = plc::reg::x_axis::command::CLEAR_ABS_POS;

    EXPECT_CALL(*mockDevice_, writeBool(reg, true)).WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_, writeBool(reg, false)).WillOnce(Return(CommunicationResult::NetworkError()));

    driver_->sendEdgeTrigger(reg);
    driver_->advanceTime(std::chrono::milliseconds(150));
    driver_->servicePendingEdgeTriggers();

    EXPECT_EQ(driver_->pendingEdgeCount(), 0);  // 清空，防止内存泄漏
}
```

### 4.5 GREEN 步骤 —— 实现代码

实现 `sendEdgeTrigger()`、`servicePendingEdgeTriggers()` 和在设计文档 §4.7 中已列出的逻辑。引入 `IClock` 接口，测试环境注入 `FakeClock`，生产环境注入 `SteadyClock`。

### 4.6 REFACTOR 步骤

- 提取 `IClock` 接口
- 考虑 `PendingEdge` 是否需要 `retryCount` 字段

---

## 5. 阶段三：状态推导引擎单元测试

### 5.1 目标

验证 `deriveAxisState()` 方法的多信号融合逻辑，覆盖所有 D100 状态值与 Coil 信号的组合。这是**纯函数**，无外部依赖，可直接进行参数化测试。

### 5.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_state_derive.cpp`

### 5.3 测试策略

Mock `PlcDevice::readInt16` / `readBool`，设置固定的 PLC 信号组合，断言 `deriveAxisState()` 输出正确的 `AxisState`。

### 5.4 测试用例清单与预期结果

| 编号 | D100 | AlarmCode | ABS_MOVING | REL_MOVING | JOGGING | 预期 AxisState |
|------|------|-----------|------------|------------|---------|----------------|
| S1 | 0 | 0 | false | false | false | **Disabled** |
| S2 | 0 | 5 | false | false | false | **Error** (alarmCode != 0 优先) |
| S3 | 3 | 0 | false | false | false | **Error** |
| S4 | 3 | 0 | true | false | false | **Error** (Error 高于运动) |
| S5 | 1 | 0 | true | false | false | **MovingAbsolute** |
| S6 | 1 | 0 | false | true | false | **MovingRelative** |
| S7 | 1 | 0 | false | false | true | **Jogging** |
| S8 | 1 | 0 | false | false | false | **Idle** |
| S9 | 2 | 0 | false | false | false | **Idle** |
| S10 | 2 | 0 | true | false | false | **MovingAbsolute** |
| S11 | 1 | 3 | false | false | false | **Error** (alarmCode != 0) |

#### 参数化测试（S1-S11）

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

// 使用 TEST_P 参数化测试覆盖全部 11 种组合
struct StateTestParam {
    int16_t d100; int16_t alarm;
    bool absM, relM, jog;
    AxisState expected;
};

class StateDeriveParamTest
    : public ModbusSystemDriverStateDeriveTest,
      public ::testing::WithParamInterface<StateTestParam> {};

INSTANTIATE_TEST_SUITE_P(AllCombinations, StateDeriveParamTest,
    ::testing::Values(
        StateTestParam{0, 0, false, false, false, AxisState::Disabled},
        StateTestParam{0, 5, false, false, false, AxisState::Error},
        StateTestParam{3, 0, false, false, false, AxisState::Error},
        StateTestParam{3, 0, true,  false, false, AxisState::Error},
        StateTestParam{1, 0, true,  false, false, AxisState::MovingAbsolute},
        StateTestParam{1, 0, false, true,  false, AxisState::MovingRelative},
        StateTestParam{1, 0, false, false, true,  AxisState::Jogging},
        StateTestParam{1, 0, false, false, false, AxisState::Idle},
        StateTestParam{2, 0, false, false, false, AxisState::Idle},
        StateTestParam{2, 0, true,  false, false, AxisState::MovingAbsolute},
        StateTestParam{1, 3, false, false, false, AxisState::Error}
    ));

TEST_P(StateDeriveParamTest, CorrectDerivation) {
    const auto& p = GetParam();
    setPlcSignals(AxisId::X, p.d100, p.alarm, p.absM, p.relM, p.jog);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::X), p.expected);
}
```

#### 回归测试：D100 1↔2 抖跳不误判

```cpp
TEST_F(ModbusSystemDriverStateDeriveTest, D100JitterBetween1And2_StillIdle) {
    // 模拟多圈编码器导致 D100 在 1 和 2 间跳变
    // 运动 Coil 全为 OFF → 始终判定为 Idle
    for (int i = 0; i < 100; ++i) {
        int16_t d100 = (i % 2 == 0) ? 1 : 2;
        setPlcSignals(AxisId::X, d100, /*alarm=*/0,
                      false, false, false);
        EXPECT_EQ(driver_->deriveAxisState(AxisId::X), AxisState::Idle) << "i=" << i;
    }
}
```

#### 边界测试：AlarmCode 为负数

```cpp
TEST_F(ModbusSystemDriverStateDeriveTest, NegativeAlarmCode_IsError) {
    setPlcSignals(AxisId::X, 1, -1, false, false, false);
    EXPECT_EQ(driver_->deriveAxisState(AxisId::X), AxisState::Error);
}
```

### 5.5 GREEN 步骤 —— 实现代码

实现设计文档 §4.8.3 中的多信号融合优先级链：

```
if d100State == 3 || alarmCode != 0  → AxisState::Error
if d100State == 0                    → AxisState::Disabled
if absMoving           → AxisState::MovingAbsolute
if relMoving           → AxisState::MovingRelative
if jogging             → AxisState::Jogging
else                   → AxisState::Idle
```

### 5.6 覆盖率检查清单

- [x] D100 = 0, 1, 2, 3
- [x] AlarmCode = 0, 正数, 负数
- [x] 三个运动 Coil 每个独立 ON
- [x] Error 优先级高于 Disabled 和所有运动状态
- [x] D100=1↔2 抖跳不误判 Idle
- [ ] 全部 Y/Z/R 轴（通过参数化测试扩展）

---

## 6. 阶段四：命令分派单元测试

### 6.1 目标

验证 `send()` 方法对每种 `SystemCommand` 变体正确分派到对应的 `PlcDevice::write*` 调用。

### 6.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_send.cpp`

### 6.3 测试策略

Mock `PlcDevice` 的 `writeBool`、`writeFloat` 方法，对每种 `SystemCommand` 变体验证：
1. 调用了正确的寄存器
2. 传入了正确的值
3. `CommunicationResult` 正确传递

### 6.4 测试用例

#### Level 型命令

| 测试名称 | 命令构造 | 期望调用 | 期望值 |
|---------|---------|---------|-------|
| SendEnableOn_X | `AxisCommandWithId{X, EnableCommand{true}}` | `writeBool(M0, true)` | Sent |
| SendEnableOff_Y | `AxisCommandWithId{Y, EnableCommand{false}}` | `writeBool(M1, false)` | Sent |
| SendJogFwdOn_Z | `AxisCommandWithId{Z, JogCommand{Forward, true}}` | `writeBool(M56, true)` | Sent |
| SendJogBwdOn_R | `AxisCommandWithId{R, JogCommand{Backward, true}}` | `writeBool(M59, true)` | Sent |
| SendStop_X | `AxisCommandWithId{X, StopCommand{}}` | `writeBool(M50, false)` + `writeBool(M51, false)` | Sent |
| SendSetJogVelocity | `AxisCommandWithId{X, SetJogVelocity{50.0}}` | `writeFloat(D1000, 50.0f)` | Sent |
| SendSetMoveVelocity | `AxisCommandWithId{Y, SetMoveVelocity{100.0}}` | `writeFloat(D1006, 100.0f)` | Sent |
| SendGantryCoupling (ON) | `GantryCouplingCommand{true}` | `writeBool(M4, true)` | Sent |
| SendGantryCoupling (OFF) | `GantryCouplingCommand{false}` | `writeBool(M4, false)` | Sent |
| SendGantryPower (ON) | `GantryPowerCommand{true}` | `writeBool(M0, true)` | Sent |
| SendEmergencyStop (触发) | `EmergencyStopCommand{true}` | `writeBool(M80, true)` | Sent |
| SendEmergencyStop (解除) | `EmergencyStopCommand{false}` | `writeBool(M80, false)` | Sent |

#### EdgeTrigger 型命令（验证经由 sendEdgeTrigger）

| 测试名称 | 命令构造 | 期望 EdgeTrigger 寄存器 |
|---------|---------|------------------------|
| SendMoveAbsolute_X | `AxisCommandWithId{X, MoveCommand{Absolute, 100.0}}` | 先 `writeFloat(D20, 100.0f)` → 再 `sendEdgeTrigger(M40)` |
| SendMoveRelative_Y | `AxisCommandWithId{Y, MoveCommand{Relative, 50.0}}` | 先 `writeFloat(D26, 50.0f)` → 再 `sendEdgeTrigger(M43)` |
| SendZeroAbsolute_Z | `AxisCommandWithId{Z, ZeroAbsoluteCommand{}}` | `sendEdgeTrigger(M32)` |
| SendSetRelativeZero_R | `AxisCommandWithId{R, SetRelativeZeroCommand{}}` | `sendEdgeTrigger(M17)` |
| SendClearRelativeZero_X | `AxisCommandWithId{X, ClearRelativeZeroCommand{}}` | `sendEdgeTrigger(M18)` |

#### 通讯失败传播

| 测试名称 | 模拟条件 | 期望返回 |
|---------|---------|---------|
| SendWriteFailure_Enable | `writeBool` 返回 NetworkError | NetworkError |
| SendMoveFailure_TargetWrite | `writeFloat` 返回 Timeout | Timeout |
| SendMoveFailure_TriggerWrite | `writeFloat` OK 但 `sendEdgeTrigger` 返回 Busy | Busy |

### 6.5 GREEN 步骤 —— 实现代码

实现设计文档 §4.4（`send()`）、§4.5（`sendAxisCommand()`）、§4.6（组级命令）、§4.7（`sendMoveCommand()`）中的全部分派逻辑。

---

## 7. 阶段五：反馈轮询集成测试

### 7.1 目标

验证 `pollFeedback()` 完整链路：从 `PlcDevice` 读取 → 多信号融合推导 `AxisState` → 填充 `AxisFeedback` → 写入 `SystemContext::axis->applyFeedback()`。

### 7.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_poll_feedback.cpp`

### 7.3 测试策略

1. 构造真实的 `SystemContext`（含 Axis 实体和 EmergencyStopController）
2. Mock `PlcDevice` 设置反馈寄存器值
3. 调用 `pollFeedback(ctx)`
4. 验证 `Axis::getFeedback()` 返回预期的 `AxisFeedback`

### 7.4 测试用例

#### 用例 1：完整轴反馈写入

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, AxisFeedbackFullWrite) {
    // Arrange: 设置 PLC 反馈值
    setFbSignals(AxisId::X, /*d100=*/1, /*alarm=*/0,
                 /*absM=*/false, /*relM=*/false, /*jog=*/false);
    setFbFloat(AxisId::X, /*absPos*/150.5f, /*relPos*/30.2f);

    // Act
    driver_->pollFeedback(ctx_);

    // Assert: 从 SystemContext 读取 Axis 反馈
    const Axis* axis = ctx_.getAxis(AxisId::X);
    ASSERT_NE(axis, nullptr);
    const auto& fb = axis->getFeedback();
    EXPECT_EQ(fb.state, AxisState::Idle);
    EXPECT_DOUBLE_EQ(fb.absPos, 150.5);
    EXPECT_DOUBLE_EQ(fb.relPos, 30.2);
}
```

#### 用例 2：Error 状态传播

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, ErrorStatePropagation) {
    setFbSignals(AxisId::Y, /*d100=*/3, /*alarm=*/5,
                 false, false, false);
    driver_->pollFeedback(ctx_);
    EXPECT_EQ(ctx_.getAxis(AxisId::Y)->getFeedback().state, AxisState::Error);
}
```

#### 用例 3：系统级反馈（急停、龙门状态）

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, SystemFeedback) {
    setFbSystem(/*estopActive=*/true, /*gantryError=*/0, /*linkageState=*/true);
    driver_->pollFeedback(ctx_);
    EXPECT_TRUE(ctx_.getEmergencyStopActive());
    EXPECT_TRUE(ctx_.getGantryCoupled());
}
```

#### 用例 4：通信失败时保留上次已知值

```cpp
TEST_F(ModbusSystemDriverPollFeedbackTest, CommunicationFailureRetainsLastKnown) {
    // 第一次轮询成功
    setFbSignals(AxisId::X, 1, 0, false, false, false);
    setFbFloat(AxisId::X, 100.0f, 20.0f);
    driver_->pollFeedback(ctx_);
    EXPECT_DOUBLE_EQ(ctx_.getAxis(AxisId::X)->getFeedback().absPos, 100.0);

    // 第二次轮询模拟设备通信失败
    driver_->setDeviceStateTrusted(false);
    driver_->pollFeedback(ctx_);
    EXPECT_DOUBLE_EQ(ctx_.getAxis(AxisId::X)->getFeedback().absPos, 100.0);
}
```

### 7.5 GREEN 步骤 —— 实现代码

实现设计文档 §4.9 中的 `pollFeedback()`、`readAxisFeedback()`、`readSystemFeedback()` 方法。

---

## 8. 阶段六：系统级集成测试

### 8.1 目标

使用 `ModbusSystemDriver` 替换 `FakeAxisDriver`，通过 `FakePLC` 作为 Mock PLC 验证端到端行为一致性。

### 8.2 测试文件

**文件**：`tests/infrastructure/test_modbus_system_driver_integration.cpp`

### 8.3 测试策略

1. 构造 `ModbusSystemDriver`，注入 `PlcDevice`（底层连接 `FakePLC` 或 `MockModbusClient`）
2. 通过 `send()` 发送一系列 `SystemCommand`
3. 调用 `pollFeedback()` 读取真实寄存器变化
4. 验证 `SystemContext` 中实体状态与预期一致

### 8.4 测试用例

#### 用例 1：Enable → Move → 反馈链路端到端

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, EnableMoveFeedbackLoop) {
    // Step 1: 使能 X 轴
    auto result = driver_->send(SystemCommand{
        AxisCommandWithId{AxisId::X, EnableCommand{true}}});
    EXPECT_TRUE(result.ok());

    // Step 2: FakePLC 模拟响应 D100=1, ABS_MOVING=ON
    fakePlc_->setStateRegister(AxisId::X, 1);
    fakePlc_->setCoil(plc::reg::x_axis::feedback::ABS_MOVING, true);
    driver_->pollFeedback(ctx_);

    // Step 3: 发送 MoveAbsolute 命令
    result = driver_->send(SystemCommand{
        AxisCommandWithId{AxisId::X, MoveCommand{Absolute, 200.0}}});
    EXPECT_TRUE(result.ok());

    // Step 4: 再次轮询，验证位置反馈
    fakePlc_->setFloat(plc::reg::x_axis::feedback::ABS_POSITION, 200.0f);
    driver_->pollFeedback(ctx_);
    EXPECT_DOUBLE_EQ(ctx_.getAxis(AxisId::X)->getFeedback().absPos, 200.0);
}
```

#### 用例 2：急停触发 → 状态传播 → 解除恢复

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, EmergencyStopLifecycle) {
    // 触发急停
    driver_->send(SystemCommand{EmergencyStopCommand{true}});
    fakePlc_->setCoil(plc::reg::system::feedback::EMERGENCY_STOP_ACTIVE, true);
    driver_->pollFeedback(ctx_);
    EXPECT_TRUE(ctx_.getEmergencyStopActive());

    // 解除急停
    driver_->send(SystemCommand{EmergencyStopCommand{false}});
    fakePlc_->setCoil(plc::reg::system::feedback::EMERGENCY_STOP_ACTIVE, false);
    driver_->pollFeedback(ctx_);
    EXPECT_FALSE(ctx_.getEmergencyStopActive());
}
```

#### 用例 3：兼容性验证 —— 与 FakeAxisDriver 行为一致

```cpp
TEST_F(ModbusSystemDriverIntegrationTest, BehaviorParityWithFakeAxisDriver) {
    // 使用同一 FakePLC 实例，分别通过 FakeAxisDriver 和 ModbusSystemDriver
    // 执行相同的命令序列，然后对比 Axis 反馈结果
    // (具体实现取决于 FakeAxisDriver 的暴露程度)
}
```

### 8.5 GREEN 步骤

无需新增代码（所有实现在前面阶段已完成），仅需确保 `FakePLC` 的寄存器响应与 PLC 协议一致。

---

## 9. CMakeLists 变更清单

### 9.1 新增测试文件

`tests/CMakeLists.txt` 中新增：

```cmake
add_executable(unit_tests
    # ... 已有测试文件 ...

    # ================ 阶段一：寄存器选择器 ================
    infrastructure/test_modbus_system_driver_registers.cpp

    # ================ 阶段二：EdgeTrigger 脉冲管理 ================
    infrastructure/test_modbus_system_driver_edge_trigger.cpp

    # ================ 阶段三：状态推导引擎 ================
    infrastructure/test_modbus_system_driver_state_derive.cpp

    # ================ 阶段四：命令分派 ================
    infrastructure/test_modbus_system_driver_send.cpp

    # ================ 阶段五：反馈轮询 ================
    infrastructure/test_modbus_system_driver_poll_feedback.cpp

    # ================ 阶段六：系统级集成 ================
    infrastructure/test_modbus_system_driver_integration.cpp
)
```

### 9.2 可能需要的新增编译目标

如果 `ModbusSystemDriver` 作为独立模块编译，需在 `infrastructure/CMakeLists.txt` 中新增编译配置：

```cmake
# infrastructure/CMakeLists.txt 新增
add_library(modbus_system_driver
    plc/ModbusSystemDriver.cpp
    plc/ModbusSystemDriver.h
)
target_link_libraries(modbus_system_driver
    PUBLIC infrastructure  # ISystemDriver, PlcDevice, Protocol 等
)
```

若 `ModbusSystemDriver` 直接编译到 `infrastructure` 库中，则无需新增目标。

### 9.3 测试依赖

所有测试文件需要链接 Google Test（现有 `unit_tests` 已链接 `GTest::gtest`、`GTest::gtest_main`），以及项目库 `infrastructure`、`domain`、`application`。

---

## 10. 实施顺序与里程碑

### 10.1 推荐实施顺序

```
Milestone 1: 寄存器层就绪
  ├── 实现 regCmd* / regFb* 方法（§3.4）
  ├── 实现 RegisterRegistry::get() 反查
  └── 阶段一测试全部 GREEN  ✓

Milestone 2: 脉冲管理就绪
  ├── 引入 IClock 接口 + FakeClock
  ├── 实现 sendEdgeTrigger / servicePendingEdgeTriggers
  └── 阶段二测试全部 GREEN  ✓

Milestone 3: 状态推导就绪
  ├── 实现 deriveAxisState() 多信号融合
  └── 阶段三测试全部 GREEN  ✓

Milestone 4: 命令分派就绪
  ├── 实现 send() / sendAxisCommand() / sendMoveCommand()
  └── 阶段四测试全部 GREEN  ✓

Milestone 5: 反馈轮询就绪
  ├── 实现 pollFeedback() / readAxisFeedback()
  └── 阶段五测试全部 GREEN  ✓

Milestone 6: 系统集成验证
  ├── 通过 FakePLC 端到端验证
  └── 阶段六测试全部 GREEN  ✓
```

### 10.2 每个里程碑的检查点

| 里程碑 | 测试通过数 | 关键风险 | 缓解措施 |
|--------|----------|---------|---------|
| M1 | ≥8 | 寄存器地址 typos | 阶段一测试精确覆盖每个地址 |
| M2 | ≥4 | 时间精度 vs 实时系统 | 引入 IClock 接口解耦 |
| M3 | ≥12 | D100 位掩码误判 | 参数化测试覆盖全部组合 |
| M4 | ≥15 | Move 命令双步骤原子性 | 独立测试 writeFloat→EdgeTrigger 链路 |
| M5 | ≥4 | 反馈丢失导致状态错误 | 通信失败保留上次已知值 |
| M6 | ≥3 | 硬件行为差异 | 用 FakePLC 作为 Mock 对比参考 |

### 10.3 预计工作量

| 阶段 | 测试用例数 | 预计工时 | 依赖 |
|------|----------|---------|------|
| 阶段一（寄存器） | 8 | 2h | 无 |
| 阶段二（EdgeTrigger） | 4 | 3h | IClock 接口改造 |
| 阶段三（状态推导） | 12 | 2h | 无 |
| 阶段四（命令分派） | 15 | 4h | 阶段一+二 |
| 阶段五（反馈轮询） | 4 | 3h | 阶段一+三 |
| 阶段六（集成） | 3 | 3h | 阶段四+五 |
| **总计** | **46** | **17h** | |

### 10.4 失败回滚策略

- 每个阶段独立编译运行，不影响已有系统
- 测试文件使用 `#ifdef ENABLE_MODBUS_SYSTEM_DRIVER_TESTS` 宏保护，开发期间默认关闭
- 全部阶段通过后，再取消 `#ifdef` 保护并合并到主 `unit_tests` 构建

---

> **TDD 核心原则总结**：
> 1. 先写测试，后写实现。确保每个测试在实现前都是 RED 状态
> 2. 最小实现使测试通过（GREEN），不增加未测试的代码
> 3. 每个阶段 REFACTOR 后再进入下一阶段
> 4. 测试文件独立，不依赖运行时硬件 / 真实 PLC 连接
