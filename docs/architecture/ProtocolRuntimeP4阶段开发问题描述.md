# P4：PlcDevice 架构重新设计文档

—— 基于 IModbusClient 读写合并 + CommunicationResult 错误返回机制

---

## 一、设计问题回顾

### 1.1 当前 P4 实现的三个缺陷

| #    | 问题               | 现状                                                         | 后果                                                         |
| ---- | ------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 1    | **读接口缺失**     | `IModbusClient` 只有 FC05/FC06/FC10（写），没有 FC01/FC02/FC03/FC04（读） | `PlcPoller::poll()` 无法调用 `client.readCoils()` / `client.readHoldingRegisters()` 构建 `PlcSnapshot` |
| 2    | **void 返回陷阱**  | 所有写接口返回 `void`                                        | 网线被拔、PLC 掉电、异常响应时，上层 `device.writeBool(...)` 无法感知失败 |
| 3    | **错误信息不可达** | 即使底层 Modbus 驱动知道 Exception Code，也无法向上传递      | 工业现场故障诊断无从下手                                     |

### 1.2 用户决策

- **读写合并在 IModbusClient**：统一管理连接状态，一个对象处理所有 Modbus 事务。
- **返回值借鉴 CommunicationResult**：`ISystemDriver.h` 中已有的 `CommunicationResult` 枚举了 L1~L4 错误层级（NetworkError / Timeout / ProtocolError / InvalidResponse / Disconnected / Busy），直接在 IModbusClient 中复用。

---

## 二、架构设计总览

### 2.1 分层位置

```
ISystemDriver (infrastructure/)
  └─ send(SystemCommand) → CommunicationResult
  └─ pollFeedback(SystemContext)

ModbusAxisDriver (未来实现 ISystemDriver)
  └─ 持有 IModbusClient*  ──┐

IModbusClient (infrastructure/plc/protocol/)  ← 本次重新设计
  ├─ readCoils(...)           → CommunicationResult
  ├─ readHoldingRegisters(...)→ CommunicationResult
  ├─ writeSingleCoil(...)     → CommunicationResult
  ├─ writeSingleRegister(...) → CommunicationResult
  └─ writeMultipleRegisters(...)→ CommunicationResult

PlcPoller ← 使用 IModbusClient 读接口
PlcDevice ← 使用 IModbusClient 写接口，透传 CommunicationResult
```

### 2.2 核心原则

1. **CommunicationResult 是统一货币**：所有跨进程/网络边界通信的返回值。
2. **读接口补充完整**：FC01（读线圈）、FC03（读保持寄存器），覆盖 PlcPoller 所有需求。
3. **写接口返回 CommunicationResult**：PlcDevice 透传到底层结果，业务层可判断成功/失败/可重试。
4. **不破坏 v5 的类型安全**：PlcDevice 的读通道仍然走 `const RegisterInfo&` + Snapshot 管线；写通道在类型安全基础上增加错误返回。

---

## 三、IModbusClient 新接口设计

### 3.1 头文件

```cpp
// infrastructure/plc/protocol/IModbusClient.h
#pragma once
#include "infrastructure/ISystemDriver.h"  // CommunicationResult
#include <vector>
#include <cstdint>

namespace plc::protocol {

/**
 * @brief Modbus 客户端抽象接口（读写合并）
 *
 * @details
 * 为 PlcPoller（读）和 PlcDevice（写）提供统一的 Modbus 传输能力。
 * 所有方法返回 CommunicationResult，表达通讯层面的成功/失败。
 *
 * 读通道:
 *   - readCoils()           FC01 — 读线圈
 *   - readHoldingRegisters() FC03 — 读保持寄存器
 *
 * 写通道:
 *   - writeSingleCoil()      FC05 — 写单个线圈
 *   - writeSingleRegister()  FC06 — 写单个保持寄存器
 *   - writeMultipleRegisters() FC10 — 写多个保持寄存器
 *
 * 设计原则:
 *   1. CommunicationResult 只表达"Modbus 帧是否成功送达/收到有效响应"
 *   2. 不表达 PLC 执行结果——那是 pollFeedback 的职责（参照 ISystemDriver 设计）
 *   3. 读接口通过引用参数返回 payload，CommunicationResult 表达通讯结果
 */
class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    // ═══════════════════════════════════════
    //  读通道 — PlcPoller 使用
    // ═══════════════════════════════════════

    /// @brief FC01 — 读线圈 (Read Coils)
    /// @param startAddress 起始线圈地址
    /// @param count 读取数量
    /// @param[out] payload 位数据（MSB 打包），仅在 CommunicationResult::ok() 时有效
    /// @return 通讯结果，含成功/失败/可重试信息
    virtual CommunicationResult readCoils(
        uint16_t startAddress,
        uint16_t count,
        std::vector<uint8_t>& payload) = 0;

    /// @brief FC03 — 读保持寄存器 (Read Holding Registers)
    /// @param startAddress 起始寄存器地址
    /// @param count 读取数量
    /// @param[out] payload 16 位寄存器值数组，仅在 CommunicationResult::ok() 时有效
    /// @return 通讯结果
    virtual CommunicationResult readHoldingRegisters(
        uint16_t startAddress,
        uint16_t count,
        std::vector<uint16_t>& payload) = 0;

    // ═══════════════════════════════════════
    //  写通道 — PlcDevice 使用
    // ═══════════════════════════════════════

    /// @brief FC05 — 写单个线圈 (Write Single Coil)
    /// @param address 线圈地址
    /// @param value   true = ON (0xFF00), false = OFF (0x0000)
    /// @return 通讯结果
    virtual CommunicationResult writeSingleCoil(
        uint16_t address,
        bool value) = 0;

    /// @brief FC06 — 写单个保持寄存器 (Write Single Holding Register)
    /// @param address 寄存器地址
    /// @param value   16 位值
    /// @return 通讯结果
    virtual CommunicationResult writeSingleRegister(
        uint16_t address,
        uint16_t value) = 0;

    /// @brief FC10 — 写多个保持寄存器 (Write Multiple Holding Registers)
    /// @param startAddress 起始寄存器地址
    /// @param values       连续的 16 位值序列
    /// @return 通讯结果
    virtual CommunicationResult writeMultipleRegisters(
        uint16_t startAddress,
        const std::vector<uint16_t>& values) = 0;
};

} // namespace plc::protocol
```

### 3.2 接口选择说明

**为什么不把读/写分离为 IModbusReader / IModbusWriter？**

| 方案           | 优点                                                         | 缺点                                                         |
| -------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 分离接口       | 接口隔离原则 (ISP) 干净                                      | 连接状态管理需要二者协调；实际实现者（一个 TCP 连接）必须同时实现两个接口；上层使用时需要持有两个指针或做多重继承 |
| **合并接口** ✓ | 一个对象 = 一个 TCP 连接；连接状态统一管理；PlcPoller 和 PlcDevice 可以共用同一个 client 实例 | 接口方法较多（5 个公开方法），但仍然是高内聚的——它们都是 Modbus 协议的不同功能码 |

**选择合并**：在工业现场，`IModbusClient` 的一个实现实例背后就是一个 TCP 连接。读/写分离意味着两个接口背后是同一个物理连接，分离徒增复杂性而无实际收益。

---

## 四、PlcDevice 调整

### 4.1 写接口返回值变更

```cpp
// 变更前（v5 当前）:
void writeBool(const RegisterInfo& reg, bool v);
void writeInt16(const RegisterInfo& reg, int16_t v);
void writeFloat(const RegisterInfo& reg, float v);
void writeValue(const RegisterInfo& reg, const PlcValue& value);

// 变更后:
CommunicationResult writeBool(const RegisterInfo& reg, bool v);
CommunicationResult writeInt16(const RegisterInfo& reg, int16_t v);
CommunicationResult writeFloat(const RegisterInfo& reg, float v);
CommunicationResult writeValue(const RegisterInfo& reg, const PlcValue& value);
```

### 4.2 内部 dispatchWrite 调整

```cpp
CommunicationResult dispatchWrite(
    const RegisterInfo& reg,
    const std::vector<uint16_t>& encoded)
{
    ensureTransport();
    switch (reg.area) {
        case RegisterArea::Coil: {
            bool state = (encoded.size() > 0 && encoded[0] != 0);
            return m_client->writeSingleCoil(reg.address, state);
        }
        case RegisterArea::HoldingReg: {
            if (encoded.size() == 1)
                return m_client->writeSingleRegister(reg.address, encoded[0]);
            else
                return m_client->writeMultipleRegisters(reg.address, encoded);
        }
    }
    return CommunicationResult::Sent; // unreachable, 但保持编译器 happy
}
```

### 4.3 业务层使用模式

```cpp
// 主循环中下发命令：
auto result = m_device.writeFloat(
    plc::reg::y_axis::command::ABS_TARGET, 200.0f);

if (!result.ok()) {
    if (result.retryable()) {
        // Timeout / Busy → 下一轮重试
        TRACE_WARN("Write retryable: {}", result.diagnostic);
    } else {
        // NetworkError / ProtocolError / Disconnected → 报警
        TRACE_ERROR("Write failed: {}", result.diagnostic);
        m_alarmSignal.trigger(Alarm::PLC_COMMUNICATION_LOST);
    }
}
```

### 4.4 异常策略调整

当前 PlcDevice 在以下情况抛异常：

| 场景               | 当前                          | 调整后                                                    |
| ------------------ | ----------------------------- | --------------------------------------------------------- |
| 写 ReadOnly 寄存器 | `throw std::invalid_argument` | **保持不变**：这是编程错误，应在开发阶段暴露              |
| 写时无 Transport   | `throw std::runtime_error`    | **保持不变**：这是配置错误，不是运行时通讯失败            |
| 读时无 Snapshot    | `throw std::out_of_range`     | 新增：`CommunicationResult::InvalidResponse` 或保持抛异常 |

**设计原则**：
- **抛异常** = 编程/配置错误，不应在运行时发生
- **返回 CommunicationResult** = 运行时通讯失败，属于正常事件（网线松动、PLC 重启）
- `CommunicationResult::ok()` + `CommunicationResult::retryable()` 两个 bool 让业务层决策

---

## 五、PlcPoller 与 IModbusClient 的配合（填补读接口缺失）

### 5.1 PlcPoller::poll() 完整实现（v4 7.9 节补全）

```cpp
PlcSnapshot PlcPoller::poll(
    IModbusClient& client,
    const RegisterRegistry& registry,
    RegisterGroup group) const
{
    auto blocks = buildPollPlan(registry, group);
    bool allSuccess = true;
    RawBitSnapshot bits;
    RawWordSnapshot words;
    uint64_t ts = currentTimeMs();

    for (auto& block : blocks) {
        if (block.area == RegisterArea::Coil) {
            std::vector<uint8_t> payload;
            auto result = client.readCoils(
                block.startAddress, block.totalRegisterCount, payload);

            if (!result.ok()) {
                allSuccess = false;
                TRACE_WARN("FC01 read failed: {}", result.diagnostic);
                continue;
            }
            bits = RawBitSnapshot(
                block.startAddress, block.totalRegisterCount, payload);
        } else {
            std::vector<uint16_t> payload;
            auto result = client.readHoldingRegisters(
                block.startAddress, block.totalRegisterCount, payload);

            if (!result.ok()) {
                allSuccess = false;
                TRACE_WARN("FC03 read failed: {}", result.diagnostic);
                continue;
            }
            words = RawWordSnapshot(block.startAddress, payload);
        }
    }

    return PlcSnapshot(std::move(bits), std::move(words), allSuccess, ts);
}
```

### 5.2 关键：PlcPoller 现在能编译了

在 v4 7.9 节中，`client.readCoils(block.startAddress, block.totalRegisterCount)` 被假设存在，但 P4 的 IModbusClient 中没有定义。重新设计后，读接口在 IModbusClient 中正式定义，PlcPoller 的实现基础完整。

---

## 六、完整数据流（读 + 写）

### 6.1 读通道（反馈链路，每 10ms）

```
T0: 主循环 tick()
      │
      ▼
    PlcPoller::poll(client, registry, Feedback)
      │
      ├─→ client.readCoils(101, 30, payload)    → CommunicationResult::Sent
      │      payload = [0x01, 0x00, ...]
      │      bits = RawBitSnapshot(101, 30, payload)
      │
      ├─→ client.readHoldingRegisters(100, 40, payload)  → Sent
      │      words = RawWordSnapshot(100, payload)
      │
      └─→ 产出 PlcSnapshot{bits, words, complete=true, ts}
              │
              ▼
    PlcDevice::updateSnapshot(snap)
              │
              ▼  (业务层)
    float pos = device.readFloat(y_axis::feedback::ABS_POSITION)
              │
              ▼  Codec::decode → PlcValue{150.0f}
```

### 6.2 写通道（命令下发链路）

```
    device.writeFloat(y_axis::command::ABS_TARGET, 200.0f)
      │
      ├─ validateWritable(reg)                → OK / throw
      ├─ RegisterCodec::encode(200.0f, reg)   → [0x0000, 0x4348] (CDAB)
      │
      └─ dispatchWrite(reg, [0x0000, 0x4348])
            │
            └─→ client.writeMultipleRegisters(24, [0x0000, 0x4348])
                    │
                    ├─ [成功] → CommunicationResult::Sent
                    ├─ [超时] → CommunicationResult::Timeout (retryable)
                    ├─ [断线] → CommunicationResult::NetworkError
                    └─ [异常] → CommunicationResult::ProtocolError{code=0x02}

    返回给业务层:
      if (!result.ok() && !result.retryable())
          → 触发 PLC_COMMUNICATION_LOST 告警
```

---

## 七、TDD 测试更新

### 7.1 FakeModbusClient 更新

测试中的 FakeModbusClient 需要：

1. **读接口实现**：可编程返回预设 payload 和 CommunicationResult
2. **写接口返回值**：可编程控制返回 Success/Timeout/Error
3. **调用记录**：保留现有 Spy 功能（计数、参数记录）

```cpp
class FakeModbusClient : public IModbusClient {
public:
    // ===== 可编程返回配置 =====
    CommunicationResult nextReadResult  = CommunicationResult{CommunicationResult::Status::Sent};
    CommunicationResult nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};

    std::vector<uint8_t>  programmedCoilPayload;    // readCoils 返回的数据
    std::vector<uint16_t> programmedRegisterPayload; // readHoldingRegisters 返回的数据

    // ===== 写调用记录（现有） =====
    int coilWriteCount = 0;
    int singleRegWriteCount = 0;
    int multiRegWriteCount = 0;
    uint16_t lastCoilAddress = 0;
    bool lastCoilValue = false;
    // ... 其余记录字段

    // ===== 读通道 =====
    CommunicationResult readCoils(uint16_t startAddress, uint16_t count,
                                   std::vector<uint8_t>& payload) override {
        readCoilCount++;
        if (nextReadResult.ok())
            payload = programmedCoilPayload;
        return nextReadResult;
    }

    CommunicationResult readHoldingRegisters(uint16_t startAddress, uint16_t count,
                                              std::vector<uint16_t>& payload) override {
        readRegCount++;
        if (nextReadResult.ok())
            payload = programmedRegisterPayload;
        return nextReadResult;
    }

    // ===== 写通道（返回 nextWriteResult） =====
    CommunicationResult writeSingleCoil(uint16_t address, bool value) override {
        coilWriteCount++;
        lastCoilAddress = address;
        lastCoilValue = value;
        return nextWriteResult;
    }
    // ... 其余写方法同理
};
```

### 7.2 新增测试用例

```cpp
// ===== 读接口测试（Poller 依赖） =====
TEST(FakeModbusClientTest, ReadCoils_ReturnsPayload_OnSuccess) {
    FakeModbusClient client;
    client.programmedCoilPayload = {0x0F}; // 1111 0000
    std::vector<uint8_t> payload;
    auto result = client.readCoils(0, 8, payload);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(payload.size(), 1);
    EXPECT_EQ(payload[0], 0x0F);
}

TEST(FakeModbusClientTest, ReadCoils_ReturnsNetworkError_PayloadUnchanged) {
    FakeModbusClient client;
    client.nextReadResult = CommunicationResult{
        CommunicationResult::Status::NetworkError, 0, "ECONNREFUSED"
    };
    std::vector<uint8_t> payload = {0x00};
    auto result = client.readCoils(0, 8, payload);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::NetworkError);
    EXPECT_EQ(payload[0], 0x00); // payload 未被修改
}

TEST(FakeModbusClientTest, ReadHoldingRegisters_ReturnsNetworkError) {
    // 类似
}

// ===== 写返回值测试 =====
TEST(PlcDeviceWriteTest, WriteBool_ReturnsCommunicationResult_OnSuccess) {
    FakeModbusClient client;
    client.nextWriteResult = CommunicationResult{CommunicationResult::Status::Sent};
    PlcDevice device(TEST_PROFILE);
    device.bindTransport(&client);

    auto result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.status, CommunicationResult::Status::Sent);
}

TEST(PlcDeviceWriteTest, WriteBool_ReturnsTimeout_Retryable) {
    FakeModbusClient client;
    client.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::Timeout, 0, "read timeout 500ms"
    };
    PlcDevice device(TEST_PROFILE);
    device.bindTransport(&client);

    auto result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    EXPECT_FALSE(result.ok());
    EXPECT_TRUE(result.retryable());
}

TEST(PlcDeviceWriteTest, WriteFloat_ReturnsNetworkError_NotRetryable) {
    FakeModbusClient client;
    client.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::NetworkError, 0, "ECONNRESET"
    };
    PlcDevice device(TEST_PROFILE);
    device.bindTransport(&client);

    auto result = device.writeFloat(plc::reg::y_axis::command::ABS_TARGET, 200.0f);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.retryable());
    EXPECT_TRUE(result.isNetworkIssue());
}

TEST(PlcDeviceWriteTest, WriteBool_ReturnsProtocolError_WithExceptionCode) {
    FakeModbusClient client;
    client.nextWriteResult = CommunicationResult{
        CommunicationResult::Status::ProtocolError, 0x02, "Illegal Data Address"
    };
    PlcDevice device(TEST_PROFILE);
    device.bindTransport(&client);

    auto result = device.writeBool(plc::reg::x_axis::command::ENABLE_REQUEST, true);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.exceptionCode, 0x02);
}
```

### 7.3 现有测试维护

现有 Part 1（读取）、Part 4（Snapshot）、Part 5（Transport）测试**完全不受影响**，因为：
- 读通道仍然走 Snapshot 管线，不经过 IModbusClient 的写接口
- `const RegisterInfo&` 类型安全契约不变

现有 Part 2（写入）测试需要调整：
- `writeBool`/`writeFloat`/`writeValue` 的返回值从 `void` 变为 `CommunicationResult`
- 可以继续忽略返回值（现有测试不依赖它），或增加简单的 `.ok()` 断言

---

## 八、文件变更清单

| #    | 文件                                                | 变更类型   | 说明                                                         |
| ---- | --------------------------------------------------- | ---------- | ------------------------------------------------------------ |
| 1    | `infrastructure/plc/protocol/IModbusClient.h`       | **重写**   | 新增 readCoils/readHoldingRegisters；所有接口返回 CommunicationResult |
| 2    | `infrastructure/plc/protocol/PlcDevice.h`           | **修改**   | writeValue/writeBool/writeInt16/writeFloat 返回 CommunicationResult；dispatchWrite 返回 CommunicationResult |
| 3    | `infrastructure/plc/protocol/PlcPoller.h`           | **可编译** | poll() 方法的 IModbusClient 参数现在有 readCoils/readHoldingRegisters 可用（P3 产物，本次不修改但依赖变更 1） |
| 4    | `tests/infrastructure/protocol/test_plc_device.cpp` | **修改**   | FakeModbusClient 实现读接口 + 返回 CommunicationResult；新增写错误返回值测试；现有写入测试微调 |

### 不受影响的文件

| 文件                                          | 原因                                 |
| --------------------------------------------- | ------------------------------------ |
| `RegisterMetadata.h` / `RegisterAddressAll.h` | 元数据层无变更                       |
| `RegisterCodec.h`                             | 纯编解码，不涉及传输                 |
| `PlcSnapshot.h` / `MemorySnapshot.h`          | 快照层无变更                         |
| `PlcValue.h`                                  | 值类型无变更                         |
| `ProtocolProfile.h` / `EndianPolicy.h`        | 配置层无变更                         |
| `RegisterRegistry.h`                          | 注册表无变更                         |
| `ISystemDriver.h`                             | CommunicationResult 定义已存在，不变 |
| Part 1/4/5 测试                               | 读/Snapshot/Transport 测试不受影响   |

---

## 九、设计对比总结

| 维度                        | 当前 P4 实现                                               | 重新设计后                                                 |
| --------------------------- | ---------------------------------------------------------- | ---------------------------------------------------------- |
| **读接口**                  | ❌ 不存在（注释说"通过 Snapshot 管线"但未提供实际调用路径） | ✅ readCoils / readHoldingRegisters，PlcPoller 可正常调用   |
| **写返回值**                | `void` — 无法感知失败                                      | `CommunicationResult` — 可判断 ok/retryable/isNetworkIssue |
| **错误信息**                | 异常（编程错误）+ 静默（通讯失败）                         | CommunicationResult.diagnostic 携带诊断字符串              |
| **接口数量**                | 3 个纯写方法                                               | 5 个方法（2 读 + 3 写），高内聚                            |
| **与 ISystemDriver 一致性** | 不一致（void vs CommunicationResult）                      | 完全一致，统一使用 CommunicationResult                     |
| **v5 类型安全**             | ✅                                                          | ✅ 不变                                                     |
| **PlcPoller 兼容性**        | ❌ 不可编译                                                 | ✅ 直接调用 client.readCoils/readHoldingRegisters           |
| **异常策略**                | 编程错误抛异常                                             | 不变——编程错误仍抛异常，通讯失败用返回值                   |

---

## 十、实施顺序

在 P4 范围内（不涉及 P3 实际代码的修改，只影响接口契约）：

1. **重写 IModbusClient.h**（读写合并 + CommunicationResult）
2. **更新 FakeModbusClient**（测试文件，实现新接口）
3. **修改 PlcDevice.h**（写接口返回 CommunicationResult，dispatchWrite 调整）
4. **补充/调整测试用例**（写错误返回值测试为核心新增项）
5. **编译验证**：确保 PlcPoller.h（P3）与新的 IModbusClient 兼容