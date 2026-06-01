# 阶段四：命令分派单元测试 —— TDD 详细开发文档 (Part 2/2)

> 版本：v1.0  
> 日期：2026-05-30  
> 项目：servoV6  
> 前置文档：
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接设计 (v4.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接设计.md)
> - [SystemCommand 寄存器映射与 Domain-Infrastructure 对接 —— TDD 开发步骤 (v2.0)](./SystemCommand寄存器映射与Domain-Infrastructure对接——TDD开发步骤.md)
> - [阶段三：状态推导引擎单元测试 —— TDD 详细开发文档 (v1.0)](./阶段三-状态推导引擎TDD详细开发文档.md)
> 
> 📘 **前篇**：[阶段四 命令分派单元测试 Part 1/2 (目标-架构-规则-规划)](./阶段四-命令分派单元测试TDD详细开发文档-Part1.md)

---

## 目录 (Part 2)

5. [RED 步骤 —— 测试用例编写](#5-red-步骤--测试用例编写)
   - 5.1 [测试夹具](#51-测试夹具)
   - 5.2 [Level 型轴命令测试](#52-level-型轴命令测试)
   - 5.3 [D-Register 型轴命令测试](#53-d-register-型轴命令测试)
   - 5.4 [EdgeTrigger 型轴命令测试](#54-edgetrigger-型轴命令测试)
   - 5.5 [组级命令测试](#55-组级命令测试)
   - 5.6 [向后兼容：MoveCommand](#56-向后兼容movecommand)
   - 5.7 [错误路径测试](#57-错误路径测试)
   - 5.8 [测试用例统计](#58-测试用例统计)
6. [GREEN 步骤 —— 生产代码实现](#6-green-步骤--生产代码实现)
7. [REFACTOR 步骤](#7-refactor-步骤)
8. [CMakeLists 变更](#8-cmakelists-变更)
9. [构建与运行命令](#9-构建与运行命令)
10. [验收检查清单](#10-验收检查清单)

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

#### 测试用例 L5：Jog 停止（单 Coil deactivation）

```cpp
TEST_F(ModbusSystemDriverSendTest, JogStopCommandDeactivatesSingleCoil) {
    // JogCommand{Forward, active=false} → 仅写 JOG_FWD=false，不写 JOG_BWD
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
                writeBool(plc::reg::system_global::command::ESTOP_TRIGGER, false))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 0);
}
```

### 5.6 向后兼容：MoveCommand（deprecated）

#### 测试用例 BC1：MoveCommand Absolute —— writeFloat + sendEdgeTrigger

```cpp
TEST_F(ModbusSystemDriverSendTest, MoveCommandAbsoluteBackwardCompat) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::X, MoveCommand{MoveType::Absolute, 250.0f, GlobalSpeed::High}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::x_axis::command::ABS_TARGET, 250.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::x_axis::command::ABS_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

#### 测试用例 BC2：MoveCommand Relative —— writeFloat + sendEdgeTrigger

```cpp
TEST_F(ModbusSystemDriverSendTest, MoveCommandRelativeBackwardCompat) {
    SystemCommand cmd = AxisCommandWithId{
        AxisId::Y, MoveCommand{MoveType::Relative, -100.0f, GlobalSpeed::Medium}
    };
    EXPECT_CALL(*mockDevice_,
                writeFloat(plc::reg::y_axis::command::REL_TARGET, -100.0f))
        .WillOnce(Return(CommunicationResult::Sent()));
    EXPECT_CALL(*mockDevice_,
                writeBool(plc::reg::y_axis::command::REL_MOVE_TRIGGER, true))
        .WillOnce(Return(CommunicationResult::Sent()));
    auto result = driver_->send(cmd);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(driver_->pendingEdgeCount(), 1);
}
```

### 5.7 错误路径测试

#### 测试用例 ERR1：未设置设备时发送 —— 返回错误

```cpp
TEST_F(ModbusSystemDriverSendTest, SendWithoutDeviceReturnsDisconnected) {
    auto noDeviceDriver = std::make_unique<plc::ModbusSystemDriver>();
    SystemCommand cmd = AxisCommandWithId{AxisId::X, EnableCommand{true}};
    auto result = noDeviceDriver->send(cmd);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.isDisconnected());
}
```

#### 测试用例 ERR2：未设置设备时 StopCommand —— 返回错误

```cpp
TEST_F(ModbusSystemDriverSendTest, StopCommandWithoutDeviceReturnsError) {
    auto noDeviceDriver = std::make_unique<plc::ModbusSystemDriver>();
    SystemCommand cmd = AxisCommandWithId{AxisId::X, StopCommand{}};
    auto result = noDeviceDriver->send(cmd);
    EXPECT_FALSE(result.ok());
}
```

### 5.8 测试用例统计

| 分类 | 测试项数量 | 测试项编号 |
|------|----------|-----------|
| Level 型轴命令 | 7 | L1 ~ L7 |
| D-Register 型轴命令 | 4 | D1 ~ D4 |
| EdgeTrigger 型轴命令 | 5 | E1 ~ E5 |
| 组级命令 | 6 | G1 ~ G6 |
| 向后兼容（MoveCommand） | 2 | BC1 ~ BC2 |
| 错误路径 | 2 | ERR1 ~ ERR2 |
| **合计** | **26** | — |

### 5.9 测试用例设计原理

#### 5.9.1 覆盖逻辑

| 分类 | 核心验证点 |
|------|-----------|
| **Level 型** (L1~L7) | `writeBool` 正确路由到对应轴寄存器 × 正确 true/false 值；`std::monostate` 不产生任何写入；`StopCommand` 同时写两个 Coil 为 false |
| **D-Register 型** (D1~D4) | `writeFloat` 正确路由 × 正确 float 值；**v4.0 核心**：`SetAbsTarget` / `SetRelTarget` 不触发 EdgeTrigger |
| **EdgeTrigger 型** (E1~E5) | **v4.0 核心**：Trigger 类命令仅走 EdgeTrigger 路径，不写 D 寄存器；`ZeroAbsolute` 映射到 `CLEAR_ABS_POS`；所有 EdgeTrigger 命令写入后 `pendingEdgeCount() == 1` |
| **组级命令** (G1~G6) | 组级命令在顶层 visit 中分派，不经过 `sendAxisCommand`；`EmergencyStop` 是 Level 型不入队；`GantryPower` 与 X 轴 Enable 共用 M0 |
| **向后兼容** (BC1~BC2) | `MoveCommand` 的 `writeFloat + sendEdgeTrigger` 双步路由仍正常工作 |
| **错误路径** (ERR1~ERR2) | 设备未初始化时返回 `Disconnected` 错误 |

#### 5.9.2 覆盖率指标

| 维度 | 目标 | 实现方式 |
|------|------|---------|
| **行覆盖** | 100% | `send(SystemCommand)` 的 `std::visit` 每个 lambda 分支均被触发 |
| **分支覆盖** | 100% | `sendAxisCommand()` 的 `std::visit` 每个 variant handler 均被触发 |
| **命令类型** | 全覆盖 | Level / D-Register / EdgeTrigger / Monostate / Deprecated 全部覆盖 |
| **轴覆盖** | X/Y/Z/R 四轴 | 在测试中分布覆盖 (L1/X, L2/Y, D2/Z, E5/R 等) |
| **错误路径** | 2 项 | 未设置设备时 Level 型与 StopCommand 的返回值 |
| **Mock 验证** | `StrictMock` | 未声明的 `EXPECT_CALL` 会导致测试失败，防止"漏写"或"多写" |

---

## 6. GREEN 步骤 —— 生产代码实现

> TDD 规范：此步骤实现 `send()` 和 `sendAxisCommand()` 使 §5 的全部 26 项测试通过。

### 6.1 实现前提

| 前提 | 状态 | 备注 |
|------|------|------|
| `PlcDevice::writeBool` 为 virtual | ⚠️ 需修改 | 将 `PlcDevice` 的 `writeBool` / `writeFloat` 声明为 `virtual` |
| `ModbusSystemDriver::sendEdgeTrigger` 已实现 | ✅ 阶段二已完成 | 返回 `CommunicationResult` 并将 Coil 入队 |
| `ModbusSystemDriver::pendingEdgeCount()` 已实现 | ✅ 阶段二已完成 | 返回待处理队列大小 |
| `regCmd*` 选择器已实现 | ✅ 阶段一已完成 | 所有轴 × 寄存器组合的选择器 |
| `regGantryCoupling()` / `regEmergencyStopTrigger()` 已实现 | ✅ 阶段一已完成 | 组级寄存器选择器 |

### 6.2 步骤 1：修改 PlcDevice 为可 Mock

**文件**：`infrastructure/plc/protocol/PlcDevice.h`

将 `writeBool` / `writeFloat` 声明为 `virtual`：

```cpp
// 修改前：
CommunicationResult writeBool(const RegisterInfo& reg, bool value);
CommunicationResult writeFloat(const RegisterInfo& reg, float value);

// 修改后：
virtual CommunicationResult writeBool(const RegisterInfo& reg, bool value);
virtual CommunicationResult writeFloat(const RegisterInfo& reg, float value);
```

> ⚠️ 如果 `PlcDevice` 是 header-only 且非 virtual，需要添加 `virtual` 关键字。如果已有 `virtual` 则跳过本步骤。

### 6.3 步骤 2：实现 sendAxisCommand() 内部方法

在 `ModbusSystemDriver` 类中添加 `sendAxisCommand()` private 方法。

**文件**：`infrastructure/plc/ModbusSystemDriver.h`

在类的 private 区域添加：

```cpp
private:
    /**
     * @brief 分派轴级命令到对应的 Modbus 寄存器操作
     *
     * 根据 AxisCommand variant 的类型，路由到 writeBool / writeFloat / sendEdgeTrigger。
     *
     * @param id  目标轴 ID (X/Y/Z/R)
     * @param cmd 轴命令 variant
     * @return CommunicationResult 写入结果
     *
     * @note 此方法不检查设备的连接状态，由调用方 send() 统一检查
     */
    CommunicationResult sendAxisCommand(AxisId id, const AxisCommand& cmd);
```

### 6.4 步骤 3：实现 sendAxisCommand() 方法体

```cpp
CommunicationResult ModbusSystemDriver::sendAxisCommand(AxisId id, const AxisCommand& cmd) {
    return std::visit(
        overloaded{
            // ────────── 空命令：直接返回成功 ──────────
            [](std::monostate) -> CommunicationResult {
                return CommunicationResult::Sent();
            },

            // ────────── Level 型：writeBool ──────────
            [this, id](const EnableCommand& ec) -> CommunicationResult {
                return m_device->writeBool(regCmdEnable(id), ec.active);
            },
            [this, id](const JogCommand& jc) -> CommunicationResult {
                RegisterInfo reg = (jc.direction == Direction::Forward)
                    ? regCmdJogFwd(id)
                    : regCmdJogBwd(id);
                return m_device->writeBool(reg, jc.active);
            },
            [this, id](const StopCommand&) -> CommunicationResult {
                auto r1 = m_device->writeBool(regCmdJogFwd(id), false);
                auto r2 = m_device->writeBool(regCmdJogBwd(id), false);
                return r1.ok() ? r2 : r1;  // 返回第一个失败的结果
            },

            // ────────── D-Register 型：writeFloat ──────────
            [this, id](const SetJogVelocityCommand& vc) -> CommunicationResult {
                return m_device->writeFloat(regCmdJogSpeed(id), vc.velocity);
            },
            [this, id](const SetMoveVelocityCommand& vc) -> CommunicationResult {
                return m_device->writeFloat(regCmdMoveSpeed(id), vc.velocity);
            },
            [this, id](const SetAbsTargetCommand& sc) -> CommunicationResult {
                return m_device->writeFloat(regCmdAbsTarget(id), sc.target);
            },
            [this, id](const SetRelTargetCommand& sc) -> CommunicationResult {
                return m_device->writeFloat(regCmdRelTarget(id), sc.distance);
            },

            // ────────── EdgeTrigger 型：sendEdgeTrigger ──────────
            [this, id](const TriggerAbsMoveCommand&) -> CommunicationResult {
                return sendEdgeTrigger(regCmdAbsTrigger(id));
            },
            [this, id](const TriggerRelMoveCommand&) -> CommunicationResult {
                return sendEdgeTrigger(regCmdRelTrigger(id));
            },
            [this, id](const ZeroAbsoluteCommand&) -> CommunicationResult {
                return sendEdgeTrigger(regCmdClearAbsPos(id));
            },
            [this, id](const SetRelativeZeroCommand&) -> CommunicationResult {
                return sendEdgeTrigger(regCmdSetRelZero(id));
            },
            [this, id](const ClearRelativeZeroCommand&) -> CommunicationResult {
                return sendEdgeTrigger(regCmdClearRelZero(id));
            },

            // ────────── Deprecated: MoveCommand ──────────
            [this, id](const MoveCommand& mc) -> CommunicationResult {
                if (mc.type == MoveType::Absolute) {
                    auto r1 = m_device->writeFloat(regCmdAbsTarget(id), mc.target);
                    if (!r1.ok()) return r1;
                    return sendEdgeTrigger(regCmdAbsTrigger(id));
                } else {
                    auto r1 = m_device->writeFloat(regCmdRelTarget(id), mc.target);
                    if (!r1.ok()) return r1;
                    return sendEdgeTrigger(regCmdRelTrigger(id));
                }
            }
        },
        cmd
    );
}
```

### 6.5 步骤 4：实现 send() 顶层分派

将 `send()` 从 stub 替换为完整实现：

**修改前**（stub）：
```cpp
CommunicationResult send(const SystemCommand&) override {
    return {};  // stub
}
```

**修改后**：
```cpp
CommunicationResult send(const SystemCommand& cmd) override {
    // 前置条件：必须有已连接的设备
    if (!m_device) {
        return CommunicationResult::Disconnected();
    }

    return std::visit(
        overloaded{
            // ────────── 轴级命令：委托给 sendAxisCommand ──────────
            [this](const AxisCommandWithId& axi) -> CommunicationResult {
                return sendAxisCommand(axi.id, axi.cmd);
            },

            // ────────── 组级命令：直接 writeBool ──────────
            [this](const GantryCouplingCommand& gc) -> CommunicationResult {
                return m_device->writeBool(regGantryCoupling(), gc.enableCoupling);
            },
            [this](const GantryPowerCommand& gp) -> CommunicationResult {
                return m_device->writeBool(regCmdEnable(AxisId::X), gp.enable);
            },
            [this](const EmergencyStopCommand& es) -> CommunicationResult {
                return m_device->writeBool(regEmergencyStopTrigger(), es.active);
            }
        },
        cmd
    );
}
```

### 6.6 实现要点

1. **`overloaded` 模式**：使用 C++17 的 `overloaded` 辅助模板（继承多个 lambda）实现 `std::visit` 的 visitor。确保项目中已有此辅助定义（通常在 `domain/command/SystemCommand.h` 或公共工具头文件中）。

2. **空命令不检查设备**：`std::monostate` 分支在 `sendAxisCommand` 内部直接返回 `Sent()`，不经过 `m_device` 访问。但顶层 `send()` 的设备检查在前，所以空命令在无设备时也会返回 `Disconnected`——这符合预期（没有设备不应该发送任何命令）。

3. **StopCommand 写两个 Coil**：必须同时写 `JOG_FWD=false` 和 `JOG_BWD=false`。若任一失败，返回第一个失败的结果。

4. **MoveCommand 废弃但保留**：`MoveType::Absolute` 路径先写 `ABS_TARGET` D 寄存器再触发 EdgeTrigger；`Relative` 同理。保留了 `GlobalSpeed` 字段但当前实现中不处理速度选择（速度已在独立的 `SetMoveVelocityCommand` 中设置）。

5. **错误传播**：`writeBool` / `writeFloat` / `sendEdgeTrigger` 的返回值逐级向上传播。D-Register 型失败时对应的 EdgeTrigger 不发送（MoveCommand 分支已处理）。

6. **`GantryPowerCommand` 复用 X 轴 M0**：因为龙门模式下 X 轴使能就是整个龙门电机上电，物理上同一继电器回路。

### 6.7 预期 RED→GREEN 转换结果

```
RED 阶段（当前）：
[ RUN      ] ModbusSystemDriverSendTest.EnableCommandWritesCoil
[  FAILED  ] ModbusSystemDriverSendTest.EnableCommandWritesCoil  ← send() 是 stub，mock 期望不被满足
...
[  FAILED  ] 26 tests, 26 FAILED

GREEN 阶段（实现后）：
[ RUN      ] ModbusSystemDriverSendTest.EnableCommandWritesCoil
[       OK ] ModbusSystemDriverSendTest.EnableCommandWritesCoil
...
[ RUN      ] ModbusSystemDriverSendTest.StopCommandWithoutDeviceReturnsError
[       OK ] ModbusSystemDriverSendTest.StopCommandWithoutDeviceReturnsError
[  PASSED  ] 26 tests.
```

---

## 7. REFACTOR 步骤

### 7.1 本阶段的重构要点

在所有 26 项测试通过后，检查以下方面：

| 检查项 | 操作 | 状态 |
|--------|------|------|
| `overloaded` 辅助模板 | 确认 `overloaded` 在项目公共头文件中定义，避免每个使用 `std::visit` 的地方重复定义 | ⚠️ 待检查 |
| `sendAxisCommand` 可见性 | 确认方法为 `private`，仅在 `send()` 内部调用 | ✅ 设计为 private |
| `sendAxisCommand` 可测试性 | 考虑是否添加 `friend class ModbusSystemDriverSendTest` 以允许直接测试内部方法（当前通过 `send()` 间接测试已足够） | 🔵 可选 |
| Mock 文件位置 | 确认 `MockPlcDevice` 放在 `tests/infrastructure/mock/` 目录下 | ⚠️ 待创建 |
| 寄存器名称一致性 | 确认代码中使用的寄存器常量名与阶段一注册的名称一致 | ✅ 复用阶段一产物 |
| 命名规范 | 确认 `sendAxisCommand` 与 `send` 方法名区分清晰，不产生歧义 | ✅ |
| gmock 链接 | 测试 target 需链接 `gmock`（`MOCK_METHOD` 宏依赖） | ⚠️ 待 CMakeLists 确认 |

### 7.2 后续阶段可做但非必须的重构

以下内容可在阶段五（完整 `pollFeedback` 集成测试）实施：

1. **将 `sendAxisCommand` 提取为独立 free function**：如果将来 `FakeAxisDriver` 或其他 Driver 也需要复用轴命令分派逻辑，可将其从 `ModbusSystemDriver` 的 private 方法重构为 `plc` 命名空间下的 free function。当前暂留 private 以保持封装性。

2. **添加 `[[deprecated]]` 标记到 MoveCommand 分支**：在 `sendAxisCommand` 的 `MoveCommand` lambda 上添加编译器警告标记，提示调用方迁移到新的四命令接口。

3. **引入命令日志**：在 `send()` 和 `sendAxisCommand()` 中添加 LOG_TRACE，记录每次分派的命令类型、目标轴、寄存器地址和写入值。此功能应独立于分派逻辑本身。

### 7.3 不引入的重构

以下曾经被考虑但明确排除：

| 方案 | 排除理由 |
|------|---------|
| 使用命令模式（Command Pattern）替代 `std::visit` | 增加类的数量（每种命令一个 execute 方法），代码量膨胀，当前 variant + visit 已经足够清晰 |
| 将 `sendAxisCommand` 改为 public | 破坏封装，`send()` 是唯一的公共入口，内部方法不应暴露 |
| 预计算 reg 选择器到查找表 | `std::visit` 已在编译期确定分支，运行时无 dispatch 开销。查表增加维护成本 |
| 为每个 Gantry 组级命令创建独立 `sendGantry` 方法 | 组级命令只有 3 种，种类少，在顶层 visit 中直接分发已足够简洁 |
| 合并 `EnableCommand` 和 `GantryPowerCommand` 为通用 `PowerCommand` | 两者语义不同（单轴使能 vs 龙门电机上电），合并会混淆领域概念 |

---

## 8. CMakeLists 变更

### 8.1 文件：`tests/CMakeLists.txt`

在阶段三的 test target 之后追加：

```cmake
# ═══════════════════════════════════════════════════════════════════
# 阶段四：命令分派单元测试
# ═══════════════════════════════════════════════════════════════════
add_executable(test_modbus_system_driver_send
    infrastructure/test_modbus_system_driver_send.cpp
)

target_link_libraries(test_modbus_system_driver_send PRIVATE
    gtest_main
    gmock
)

target_include_directories(test_modbus_system_driver_send PRIVATE
    ${PROJECT_SOURCE_DIR}
)

# 注册到 CTest
add_test(
    NAME ModbusSystemDriver.Send
    COMMAND test_modbus_system_driver_send
)
```

### 8.2 依赖说明

```
test_modbus_system_driver_send
  ├── gtest_main                           (链接 — GTest 框架)
  ├── gmock                                (链接 — MOCK_METHOD / EXPECT_CALL 宏)
  ├── ${PROJECT_SOURCE_DIR}                (include — 使头文件路径可解析)
  ├── infrastructure/plc/ModbusSystemDriver.h   (被测试的生产代码)
  ├── domain/command/SystemCommand.h            (SystemCommand variant)
  ├── domain/entity/Axis.h                      (AxisCommand variant)
  └── infrastructure/plc/protocol/PlcDevice.h    (MockPlcDevice 的基类)
```

**注意**：此 target 必须链接 `gmock`（不仅是 `gtest_main`），因为测试文件使用了 `MOCK_METHOD` 宏、`EXPECT_CALL` 和 `StrictMock`。

### 8.3 Mock 文件创建

**文件**：`tests/infrastructure/mock/MockPlcDevice.h`

```cpp
// =============================================================================
// MockPlcDevice — PlcDevice 的 Mock 实现
//
// 用于命令分派测试，验证 writeBool / writeFloat 的调用参数与次数。
// =============================================================================

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

## 9. 构建与运行命令

### 9.1 构建

```bash
# 在 build 目录下执行
cd build
cmake .. -G "MinGW Makefiles"
cmake --build . --target test_modbus_system_driver_send
```

### 9.2 运行

```bash
# 运行全部命令分派测试
ctest -R ModbusSystemDriver.Send --output-on-failure

# 或直接运行可执行文件
./tests/test_modbus_system_driver_send

# 只运行 Level 型轴命令测试
./tests/test_modbus_system_driver_send --gtest_filter="*Enable*:*Jog*:*Stop*:*Monostate*"

# 只运行 D-Register 型轴命令测试
./tests/test_modbus_system_driver_send --gtest_filter="*SetJogVelocity*:*SetMoveVelocity*:*SetAbsTarget*:*SetRelTarget*"

# 只运行 EdgeTrigger 型轴命令测试
./tests/test_modbus_system_driver_send --gtest_filter="*Trigger*:*ZeroAbsolute*:*SetRelativeZero*:*ClearRelativeZero*"

# 只运行组级命令测试
./tests/test_modbus_system_driver_send --gtest_filter="*Gantry*:*EmergencyStop*:*Release*"

# 只运行 v4.0 核心解耦测试（四命令定位）
./tests/test_modbus_system_driver_send --gtest_filter="*OnlyDRegister*:*OnlyEdgeTrigger*"

# 只运行向后兼容测试
./tests/test_modbus_system_driver_send --gtest_filter="*BackwardCompat*"

# 只运行错误路径测试
./tests/test_modbus_system_driver_send --gtest_filter="*WithoutDevice*"
```

### 9.3 TDD 完整流程

```
步骤 1 (RED)：     编写测试文件 → 编译 → 运行 → 26/26 FAIL ❌
                   (因为 send() 当前为 stub: return {})
                   验证点：
                   - 编译通过（头文件依赖正确、MockPlcDevice 编译通过）
                   - 所有测试 FAIL（mock 期望 writeBool/writeFloat 被调用但未被满足）

步骤 2 (GREEN)：   实现 send() + sendAxisCommand() → 编译 → 运行 → 26/26 PASS ✅
                   验证点：
                   - 所有 EXPECT_CALL 均被满足（无"漏写"）
                   - 无 unexpected call（无"多写"）
                   - 寄存器地址与阶段一的 regCmd* 选择器一致
                   - pendingEdgeCount() 与 EdgeTrigger 命令数量匹配

步骤 3 (REFACTOR)：检查代码结构/命名/复用 → 运行 → 26/26 PASS ✅
                   (确认重构未破坏任何测试)
```

### 9.4 RED 阶段验证命令（StrictMock 行为验证）

```
# StrictMock 在析构时验证所有 EXPECT_CALL 都被满足。
# 如果 send() stub 不调用 writeBool/writeFloat，则：
#   - EXPECT_CALL 声明了但从未被调用
#   - StrictMock 析构时触发 FAIL
# 
# 这正是 RED 阶段期望的行为 —— 所有 26 项测试 FAIL。

# 也可以单独运行一个测试验证 StrictMock 行为：
./tests/test_modbus_system_driver_send --gtest_filter="*EnableCommandWritesCoil*"
# 预期输出：FAIL — mock function writeBool was never called
```

---

## 10. 验收检查清单

### 10.1 必须通过的检查

- [ ] **Mock 文件创建**：`tests/infrastructure/mock/MockPlcDevice.h` 文件已创建
- [ ] **测试文件创建**：`tests/infrastructure/test_modbus_system_driver_send.cpp` 文件已创建（含全部 26 项测试）
- [ ] **PlcDevice virtual 化**：`PlcDevice::writeBool` / `writeFloat` 已声明为 `virtual`
- [ ] **生产代码实现**：`send()` 和 `sendAxisCommand()` 方法体已实现（替换 stub）
- [ ] **CMakeLists 更新**：`tests/CMakeLists.txt` 已添加 `test_modbus_system_driver_send` target
- [ ] **编译通过**：`cmake --build . --target test_modbus_system_driver_send` 无错误

#### Level 型轴命令测试（7 项）
- [ ] `EnableCommandWritesCoil` PASS
- [ ] `DisableCommandWritesCoilFalse` PASS
- [ ] `JogForwardCommandWritesCoil` PASS
- [ ] `JogBackwardCommandWritesCoil` PASS
- [ ] `JogStopCommandDeactivatesSingleCoil` PASS
- [ ] `StopCommandWritesBothJogCoilsOff` PASS
- [ ] `MonostateEmptyCommandNoWrite` PASS

#### D-Register 型轴命令测试（4 项）
- [ ] `SetJogVelocityWritesFloat` PASS
- [ ] `SetMoveVelocityWritesFloat` PASS
- [ ] `SetAbsTargetWritesOnlyDRegister` PASS（v4.0 核心）
- [ ] `SetRelTargetWritesOnlyDRegister` PASS（v4.0 核心）

#### EdgeTrigger 型轴命令测试（5 项）
- [ ] `TriggerAbsMoveOnlyEdgeTrigger` PASS（v4.0 核心）
- [ ] `TriggerRelMoveOnlyEdgeTrigger` PASS（v4.0 核心）
- [ ] `ZeroAbsoluteCommandEdgeTrigger` PASS
- [ ] `SetRelativeZeroCommandEdgeTrigger` PASS
- [ ] `ClearRelativeZeroCommandEdgeTrigger` PASS

#### 组级命令测试（6 项）
- [ ] `GantryCouplingCommandLevelType` PASS
- [ ] `GantryUncouplingCommandLevelType` PASS
- [ ] `GantryPowerCommandSharesM0WithXEnable` PASS
- [ ] `GantryPowerOffCommand` PASS
- [ ] `EmergencyStopCommandLevelType` PASS（v4.0 核心）
- [ ] `ReleaseEmergencyStopCommandLevelType` PASS

#### 向后兼容测试（2 项）
- [ ] `MoveCommandAbsoluteBackwardCompat` PASS
- [ ] `MoveCommandRelativeBackwardCompat` PASS

#### 错误路径测试（2 项）
- [ ] `SendWithoutDeviceReturnsDisconnected` PASS
- [ ] `StopCommandWithoutDeviceReturnsError` PASS

- [ ] **总测试数**：26 tests PASSED，0 FAILED

### 10.2 代码质量检查

- [ ] `send()` 和 `sendAxisCommand()` 使用 `std::visit` + `overloaded` 模式
- [ ] 每种 `AxisCommand` variant 在 `sendAxisCommand` 中都有对应的 handler
- [ ] `std::monostate` handler 直接返回 `Sent()`，不访问设备
- [ ] `StopCommand` handler 写两个 Coil（`JOG_FWD=false` 和 `JOG_BWD=false`）
- [ ] `SetAbsTargetCommand` / `SetRelTargetCommand` 仅调用 `writeFloat`，不调用 `sendEdgeTrigger`
- [ ] `TriggerAbsMoveCommand` / `TriggerRelMoveCommand` 仅调用 `sendEdgeTrigger`，不调用 `writeFloat`
- [ ] `ZeroAbsoluteCommand` 映射到 `CLEAR_ABS_POS` 而非 `HOME_TRIGGER`
- [ ] `EmergencyStopCommand` 直接调用 `writeBool`（Level 型），不经过 `sendEdgeTrigger`
- [ ] `GantryPowerCommand` 使用 `regCmdEnable(AxisId::X)`（与 X 轴 Enable 共用 M0）
- [ ] `MoveCommand` handler 同时调用 `writeFloat` 和 `sendEdgeTrigger`
- [ ] `send()` 在 `m_device == nullptr` 时返回 `Disconnected()`
- [ ] 方法注释完整体现路由规则（含 `@param` / `@return` / `@note`）

### 10.3 回归检查

- [ ] 已有测试不受影响：`ctest` 全量运行，阶段一/二/三的测试保持 PASS
- [ ] 阶段一（`test_modbus_system_driver_registers`）不受影响
- [ ] 阶段二（`test_modbus_system_driver_edge_trigger`）不受影响
- [ ] 阶段三（`test_modbus_system_driver_state_derive`）不受影响

### 10.4 文档检查

- [ ] 本 TDD 文档 Part1/Part2 均已放置在 `docs/architecture/` 目录下
- [ ] Part1 包含 §1~§4（目标-架构-规则-规划），Part2 包含 §5~§10（RED-REFACTOR-验收）
- [ ] Part1 末尾的目录中仅列出 §1~§4，且有指向 Part2 的链接
- [ ] Part2 开头的目录中列出 §5~§10，且有指向 Part1 的链接
- [ ] 文档中所有代码示例与实际文件一致
- [ ] 测试用例编号 L1~L7, D1~D4, E1~E5, G1~G6, BC1~BC2, ERR1~ERR2 与代码一一对应
- [ ] 路由规则与设计文档 §4.5 + §4.6 一致
- [ ] Mock 设计（`MockPlcDevice`）与 Part1 §4.4 一致

---

## 附录 A：与其他阶段的接口约定

### A.1 阶段五依赖（pollFeedback 集成）

阶段五将实现 `ModbusSystemDriver::pollFeedback()` 完整链路。届时：

- `send()` / `sendAxisCommand()` 方法已通过本阶段验证，可直接复用
- `regCmd*` 选择器（阶段一）和 `sendEdgeTrigger`（阶段二）均已就位
- 阶段五的测试可通过 FakePLC 设置特定反馈寄存器值，验证 `pollFeedback` → `deriveAxisState()` 完整链路

### A.2 阶段依赖图（更新）

```
阶段一 (寄存器注册)            ─── ✅ 已完成
  ├── regCmd* 选择器
  └── regFb* 选择器

阶段二 (边沿触发)              ─── ✅ 已完成
  ├── sendEdgeTrigger()
  └── servicePendingEdgeTriggers()

阶段三 (状态推导)              ─── ✅ 已完成
  └── deriveAxisState() 纯函数

阶段四 (命令分派)              ─── 🚧 本阶段
  ├── send(SystemCommand)
  └── sendAxisCommand(AxisId, AxisCommand)

阶段五 (pollFeedback 集成)     ─── 待开发
  ├── 复用阶段一 regFb* 选择器
  ├── 复用阶段三 deriveAxisState()
  └── 集成 FakePLC + FakeClock 端到端测试
```

### A.3 与 FakeAxisDriver 的关系

`FakeAxisDriver` (`infrastructure/FakeAxisDriver.h`) 也可以复用 `send()` / `sendAxisCommand()` 的结构（通过实现 `ISystemDriver` 接口）：

```cpp
// FakeAxisDriver 中可以有不同的 send 实现（例如直接修改内部状态而非写 Modbus）
// 但 SystemCommand variant 的分派逻辑与此完全相同
```

---

## 附录 B：SystemCommand variant 参考

（来自 `domain/command/SystemCommand.h`，仅供参考）

```cpp
using SystemCommand = std::variant<
    AxisCommandWithId,          // 轴级命令 (AxisId + AxisCommand)
    GantryCouplingCommand,      // 龙门联动/解耦
    GantryPowerCommand,         // 龙门电机上电/掉电
    EmergencyStopCommand        // 急停触发/解除
>;

struct AxisCommandWithId {
    AxisId id;
    AxisCommand cmd;
};
```

---

## 附录 C：AxisCommand variant 参考

（来自 `domain/entity/Axis.h`，仅供参考）

```cpp
using AxisCommand = std::variant<
    std::monostate,               // 空命令（无操作）
    EnableCommand,                // 使能/掉电
    JogCommand,                   // 点动（正向/反向）
    StopCommand,                  // 停止
    SetJogVelocityCommand,        // 设置点动速度
    SetMoveVelocityCommand,       // 设置定位速度
    SetAbsTargetCommand,          // 设置绝对目标位置
    TriggerAbsMoveCommand,        // 触发绝对定位
    SetRelTargetCommand,          // 设置相对目标距离
    TriggerRelMoveCommand,        // 触发相对定位
    ZeroAbsoluteCommand,          // 绝对零点清除
    SetRelativeZeroCommand,       // 设置相对零点
    ClearRelativeZeroCommand,     // 清除相对零点
    MoveCommand                   // [deprecated] 兼容旧接口
>;
```

---

> **文档结束 — Part 2/2**  
> 下一步：将本阶段的测试文件和 `send()` / `sendAxisCommand()` 实现按 TDD 三步法（RED → GREEN → REFACTOR）实际编写并验证通过。
