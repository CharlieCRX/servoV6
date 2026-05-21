# servoV6 — 基于 Modbus TCP 的 infrastructure 架构演进设计文档

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-21 | 初版：基于 Modbus TCP 的五层 infrastructure 架构设计 |

---

## 1. 设计目标

将当前 `infrastructure` 层从单一 `FakePLC` + `FakeAxisDriver` 的仿真架构，演进为：

> **面向长期演化、支持多 PLC、多协议、多设备组** 的工业级基础设施层。

核心原则：

1. **Domain / Application / Presentation 完全无感** — 协议替换时业务层零改动。
2. **TDD 全覆盖** — 每一层均有 Fake 实现，可独立测试。
3. **协议防腐层（ACL）** — PLC 寄存器布局、字节序、编码格式变更仅影响 protocol 层。
4. **工业现场鲁棒性** — 网络抖动、PLC 重启、超时重试、连接管理内聚在 transport/connection 层。

---

## 2. 架构总览 — 五层模型

```
┌──────────────────────────────────────────────────────────────────┐
│                     DOMAIN / APPLICATION                         │
│         Axis · Gantry · UseCase · Orchestrator · SystemCommand   │
│                                                                  │
│         永远不感知 PLC —— 只知道 ISystemDriver                     │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                  L1: DRIVER 集成层                               │
│                                                                  │
│   ModbusTcpDriver  (替代 FakeAxisDriver)                         │
│   职责：命令流调度 · 反馈流调度 · 错误收敛                         │
│                                                                  │
│   send(cmd) ──→ PlcRegisterMap ──→ BatchWritePlan ──→ IModbusClient
│   pollFeedback() ──→ RegisterBlock 批量读 ──→ PlcFeedbackSnapshot
│                          ──→ AxisFeedbackDecoder ──→ applyFeedback()
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                L2: PROTOCOL 协议层 (防腐层 ACL)                   │
│                                                                  │
│   RegisterAddress.h      — 统一寄存器地址常量定义                  │
│   RegisterBlock.h        — 连续寄存器块定义（批量读写优化）         │
│   RegisterCodec          — 数据类型编码（float/double/bitfield）   │
│   PlcRegisterMap         — 领域命令 → PLC 寄存器行为翻译            │
│   RegisterWrite / BatchWritePlan — 批量写优化                     │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│               L3: TRANSPORT 传输层                               │
│                                                                  │
│   IModbusClient          — 抽象接口                              │
│   FakeModbusClient       — 内存寄存器模拟（TDD 核心）              │
│   LibModbusClient        — 真实 socket 通讯                      │
│                                                                  │
│   只懂: uint16_t[] · Function Code · 寄存器地址                    │
│   不懂: Axis · Gantry · MoveCommand · 业务语义                    │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│              L4: CONNECTION 连接管理层                           │
│                                                                  │
│   ConnectionManager      — 连接生命周期管理                       │
│   职责：reconnect · heartbeat · timeout · socket state · retry    │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│              L5: FEEDBACK 反馈层                                 │
│                                                                  │
│   PlcFeedbackSnapshot    — 同一时刻一致性快照                     │
│   AxisFeedbackDecoder    — 寄存器 → AxisFeedback 解码            │
└──────────────────────────────────────────────────────────────────┘
```

### 数据流总览

**命令流（下行）**：
```
UI → UseCase → SystemCommand
    → ModbusTcpDriver::send()
        → PlcRegisterMap::translate(cmd) → BatchWritePlan
            → IModbusClient::writeHoldingRegisters(...)
                → PLC 物理设备
```

**反馈流（上行）**：
```
PLC 物理设备
    → IModbusClient::readHoldingRegisters(block)
        → PlcFeedbackSnapshot::fromRegisters(...)
            → AxisFeedbackDecoder::decode(...) → AxisFeedback
                → SystemContext: axis->applyFeedback()
                    → UI 刷新
```

---

## 3. 目录结构

```
infrastructure/
├── ISystemDriver.h                    # 已有，不变更接口签名
│
├── driver/
│   ├── FakeAxisDriver.h               # 保留，测试替身（内部重构为组合 FakeModbusClient）
│   ├── ModbusTcpDriver.h              # [新增] 真实 Modbus TCP 驱动
│   └── ModbusTcpDriver.cpp            # [新增]
│
├── plc/
│   ├── protocol/
│   │   ├── RegisterAddress.h          # [新增] 寄存器地址枚举/常量
│   │   ├── RegisterBlock.h            # [新增] 寄存器块定义
│   │   ├── RegisterCodec.h            # [新增] 数据类型编码
│   │   ├── RegisterCodec.cpp          # [新增]
│   │   ├── RegisterWrite.h            # [新增] 单次写操作
│   │   ├── RegisterRead.h             # [新增] 单次读操作
│   │   ├── BatchWritePlan.h           # [新增] 批量写计划
│   │   ├── PlcRegisterMap.h           # [新增] 协议翻译核心
│   │   └── PlcRegisterMap.cpp         # [新增]
│   │
│   ├── transport/
│   │   ├── IModbusClient.h            # [新增] Modbus 客户端抽象接口
│   │   ├── FakeModbusClient.h         # [新增] 内存寄存器模拟（TDD）
│   │   ├── LibModbusClient.h          # [新增] 真实 socket 通讯
│   │   └── LibModbusClient.cpp        # [新增]
│   │
│   ├── connection/
│   │   ├── ConnectionManager.h        # [新增] 连接生命周期管理
│   │   └── ConnectionManager.cpp      # [新增]
│   │
│   └── feedback/
│       ├── PlcFeedbackSnapshot.h      # [新增] 一致性反馈快照
│       ├── AxisFeedbackDecoder.h      # [新增] 寄存器 → 领域反馈解码
│       └── AxisFeedbackDecoder.cpp    # [新增]
│
tests/
├── infrastructure/
│   ├── test_fake_plc.cpp              # 保留
│   ├── test_system_integration.cpp    # 保留（改为使用 FakeModbusClient 组合）
│   ├── protocol/
│   │   ├── test_register_codec.cpp        # [新增]
│   │   ├── test_plc_register_map.cpp      # [新增]
│   │   └── test_batch_write_plan.cpp      # [新增]
│   ├── transport/
│   │   ├── test_fake_modbus_client.cpp    # [新增]
│   │   └── test_libmodbus_client.cpp      # [新增]（需硬件环境）
│   └── feedback/
│       ├── test_plc_feedback_snapshot.cpp  # [新增]
│       └── test_axis_feedback_decoder.cpp  # [新增]
```

---

## 4. 第一层：Driver 集成层

### 4.1 ISystemDriver（已有，不变更）

**路径**：`infrastructure/ISystemDriver.h`

**职责**：定义系统驱动的统一抽象接口。这是 Domain 层与基础设施层之间的**唯一合约边界**。

**关键接口**：

```cpp
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

**为什么不变**：
- `CommunicationResult` 已精确表达 Modbus TCP 的六类通讯失败
- `SystemCommand` 已是 variant 统一命令总线
- 接口的抽象层级已经足够——它不感知 transport、不感知 protocol，只感知"命令"与"反馈"

---

### 4.2 FakeAxisDriver（保留，内部重构）

**路径**：`infrastructure/driver/FakeAxisDriver.h`

**职责**：ISystemDriver 的测试替身，用于 TDD 集成测试。

**当前实现**：直接组合 `FakePLC`，在 `send()` 中调用 `FakePLC::onCommand()`，在 `pollFeedback()` 中调用 `FakePLC::tick()` + 反馈注入。

**演进方向**：内部重构为组合 `FakeModbusClient`，使其成为 `ModbusTcpDriver` 的**完全同构的 Fake 版本**：

```
FakeAxisDriver
  └── FakeModbusClient    (与 IModbusClient 同接口)
        └── unordered_map<uint16_t, uint16_t>  (模拟 PLC RAM)
```

**为何这样重构**：
- Fake 和 Real 走完全相同的 PlcRegisterMap → IModbusClient 路径
- 测试覆盖的是"协议行为"，而非"函数调用行为"
- 当 PLC 寄存器布局变更时，只需改 `RegisterAddress.h`，Fake 和 Real 同步生效

**保留的测试辅助接口**：
- `disconnect()` / `connect()` — 模拟网络通断
- `history` 记录 — 测试断言命令是否正确下发
- `has<T>()` / `lastForAxis()` — 历史查询

---

### 4.3 ModbusTcpDriver（新增）

**路径**：`infrastructure/driver/ModbusTcpDriver.h` / `.cpp`

**职责**：真实 Modbus TCP 驱动，**系统行为编排器**。

**核心职责**：
1. **命令流调度**：`send()` → `PlcRegisterMap` → `BatchWritePlan` → `IModbusClient`
2. **反馈流调度**：`pollFeedback()` → `RegisterBlock` 批量读取 → `PlcFeedbackSnapshot` → `AxisFeedbackDecoder` → `applyFeedback()`
3. **错误收敛**：将 `IModbusClient` 返回的底层错误映射为 `CommunicationResult`

**伪代码结构**：

```cpp
class ModbusTcpDriver : public ISystemDriver {
public:
    ModbusTcpDriver(std::unique_ptr<IModbusClient> client,
                    std::shared_ptr<ConnectionManager> connMgr);

    CommunicationResult send(const SystemCommand& cmd) override {
        if (!m_connectionMgr->isConnected())
            return {Disconnected, 0, "Not connected"};

        auto plan = m_registerMap.translate(cmd);
        auto result = m_client->executeBatchWrite(plan);
        return toCommunicationResult(result);
    }

    void pollFeedback(SystemContext& ctx) override {
        auto rawData = m_client->readHoldingRegisters(
            FEEDBACK_BLOCK.start, FEEDBACK_BLOCK.count);
        if (!rawData) return; // 通讯失败，保留上次值

        auto snapshot = PlcFeedbackSnapshot::fromRegisters(*rawData);

        // 注入急停
        ctx.emergencyStopController().applyFeedback(snapshot.emergencyStop);

        // 注入龙门
        GantryFeedback gf = m_feedbackDecoder.decodeGantry(snapshot);
        ctx.gantryPowerController().applyFeedback(gf);
        ctx.gantryCouplingController().applyFeedback(gf);

        // 注入各轴
        applyAxisFeedback(ctx, AxisId::X,  m_feedbackDecoder.decodeX(snapshot));
        applyAxisFeedback(ctx, AxisId::X1, m_feedbackDecoder.decodeX1(snapshot));
        applyAxisFeedback(ctx, AxisId::X2, m_feedbackDecoder.decodeX2(snapshot));
        applyAxisFeedback(ctx, AxisId::Y,  m_feedbackDecoder.decodeY(snapshot));
        applyAxisFeedback(ctx, AxisId::Z,  m_feedbackDecoder.decodeZ(snapshot));
        applyAxisFeedback(ctx, AxisId::R,  m_feedbackDecoder.decodeR(snapshot));
    }

private:
    std::unique_ptr<IModbusClient> m_client;
    std::shared_ptr<ConnectionManager> m_connectionMgr;
    PlcRegisterMap m_registerMap;
    AxisFeedbackDecoder m_feedbackDecoder;
};
```

**关键设计决策**：
- `ModbusTcpDriver` **不直接碰 socket**。socket 操作全部在 `IModbusClient` 内部。
- `ModbusTcpDriver` **不碰大小端、浮点编码**。编码全部在 `RegisterCodec` 内部。
- `ModbusTcpDriver` **不管理重连逻辑**。连接管理全部在 `ConnectionManager` 内部。

这确保了 Driver 层的代码**极薄**，只做数据流编排。

---

## 5. 第二层：Protocol 协议层（防腐层 ACL）

> **这是整个工业系统最核心的层。**
>
> 职责：完全隔离 PLC 协议，使上层代码不感知寄存器布局、字节序、编码格式。

---

### 5.1 RegisterAddress.h（新增）

**路径**：`infrastructure/plc/protocol/RegisterAddress.h`

**职责**：统一寄存器地址常量定义，消除代码中的魔数。

所有寄存器地址集中在此文件中，以 `constexpr` 命名空间常量组织。每个轴的命令寄存器、龙门命令寄存器、急停命令寄存器、反馈寄存器块各有一个独立命名空间。

**为什么必须独立**：
- 工业现场最常变化的就是寄存器布局。PLC 工程师说"X 轴整体后移 100"，只需要改这个文件。
- 地址散落在各处 → 项目会崩。
- 命名空间隔离每个轴的地址空间，防止混淆。

**关键命名空间**：
- `plc::reg::x_axis` / `x1_axis` / `x2_axis` / `y_axis` / `z_axis` / `r_axis` — 各轴命令寄存器
- `plc::reg::gantry` — 龙门命令寄存器（POWER_ENABLE, COUPLING_ENABLE）
- `plc::reg::emergency` — 急停命令寄存器（STOP_CMD）
- `plc::reg::feedback` — 反馈寄存器块基址和偏移

---

### 5.2 RegisterBlock.h（新增）

**路径**：`infrastructure/plc/protocol/RegisterBlock.h`

**职责**：定义寄存器块（连续地址区间），优化批量读写。

```cpp
struct RegisterBlock {
    uint16_t start;   // 起始地址
    uint16_t count;   // 寄存器数量
};

// 预定义块
constexpr RegisterBlock FULL_FEEDBACK_BLOCK { 2000, 192 };
constexpr RegisterBlock X1_FEEDBACK_BLOCK    { 2000, 32 };
constexpr RegisterBlock Y_FEEDBACK_BLOCK     { 2064, 32 };
```

**为什么重要**：Modbus TCP 最大性能关键在于**连续批量读**。一次 `readHoldingRegisters(2000, 192)` 远比 32 次单寄存器读取性能高几个数量级。`RegisterBlock` 将这种优化意图**显式编码在类型系统中**。

---

### 5.3 RegisterCodec（新增）

**路径**：`infrastructure/plc/protocol/RegisterCodec.h` / `.cpp`

**职责**：C++ 原生类型 ↔ `uint16_t[]` 的编码/解码。隔离大小端、浮点格式、高低字交换。

**支持的编码类型**：
- `bool` ↔ 1 个寄存器（0 / 1 位）
- `uint16_t` ↔ 1 个寄存器（直通）
- `int32_t` ↔ 2 个寄存器（含字节序）
- `float` ↔ 2 个寄存器（IEEE 754 + 字节序）
- `double` ↔ 4 个寄存器（IEEE 754 + 字节序）

**字节序枚举**：
```cpp
enum class Endianness {
    BigEndian,         // ABCD (标准 Modbus)
    LittleEndian,      // DCBA
    BigEndianSwap,     // CDAB (高低字交换)
    LittleEndianSwap   // BADC
};
```

**为什么必须隔离**：
- 不同厂商的 32 位浮点数在 Modbus 寄存器中的字节序各不相同
- BCD 码、非标浮点格式等边缘情况
- 固件升级后可能改变字序

将编解码集中在 `RegisterCodec` 中，意味着这些问题**只影响这一个文件**。

---

### 5.4 RegisterWrite.h & RegisterRead.h（新增）

**路径**：`infrastructure/plc/protocol/RegisterWrite.h` 和 `RegisterRead.h`

**职责**：描述单次寄存器写/读操作的数据结构（DTO）。

```cpp
struct RegisterWrite {
    uint16_t startAddress;
    std::vector<uint16_t> values;
};

struct RegisterRead {
    uint16_t startAddress;
    uint16_t count;
};
```

这两个是简单的数据载体，为 `BatchWritePlan` 和 `IModbusClient` 提供统一的操作描述。

---

### 5.5 BatchWritePlan.h（新增）

**路径**：`infrastructure/plc/protocol/BatchWritePlan.h`

**职责**：批量写操作计划，支持优化合并。

**核心方法**：
- `add(address, values)` — 添加一次写操作
- `mergeAdjacent()` — 合并相邻的写操作（如写 [1000, 2 words] + [1002, 4 words] → [1000, 6 words]）
- `split(maxWordsPerFrame)` — 分片（处理超过 PLC 单帧 123 words 限制的情况）

**为什么需要**：后期一定需要优化连续写。例如使能 + 目标位置 + 速度可以合并为一次 Modbus 帧。`BatchWritePlan` 提供了优化的**容器和策略点**。

---

### 5.6 PlcRegisterMap（新增 — 最核心模块）

**路径**：`infrastructure/plc/protocol/PlcRegisterMap.h` / `.cpp`

**职责**：**领域命令 → PLC 寄存器行为翻译**。这是整个系统最核心的模块。

**接口**：
```cpp
class PlcRegisterMap {
public:
    BatchWritePlan translate(const SystemCommand& cmd);

private:
    BatchWritePlan translateEnableCommand(AxisId, const EnableCommand&);
    BatchWritePlan translateJogCommand(AxisId, const JogCommand&);
    BatchWritePlan translateMoveCommand(AxisId, const MoveCommand&);
    BatchWritePlan translateStopCommand(AxisId, const StopCommand&);
    BatchWritePlan translateSetJogVelocity(AxisId, const SetJogVelocityCommand&);
    BatchWritePlan translateSetMoveVelocity(AxisId, const SetMoveVelocityCommand&);
    BatchWritePlan translateGantryCoupling(const GantryCouplingCommand&);
    BatchWritePlan translateGantryPower(const GantryPowerCommand&);
    BatchWritePlan translateEmergencyStop(const EmergencyStopCommand&);

    static uint16_t getCommandBaseAddress(AxisId id);
};
```

**示例：`translateMoveCommand`**：
```cpp
BatchWritePlan PlcRegisterMap::translateMoveCommand(AxisId id, const MoveCommand& cmd) {
    uint16_t base = getCommandBaseAddress(id);
    BatchWritePlan plan;

    // 1. 写模式寄存器 (0=绝对, 1=相对)
    plan.add(base + 12, {RegisterCodec::encodeBool(cmd.type == MoveType::Relative)});

    // 2. 写目标位置 (float32 → 2 words)
    auto posRegs = RegisterCodec::encodeFloat(
        static_cast<float>(cmd.target), Endianness::BigEndian);
    plan.add(base + 2, {posRegs[0], posRegs[1]});

    // 3. 写启动位
    plan.add(base + 10, {1});

    return plan;
}
```

**为什么这是整个系统最重要的模块**：

领域语义（`MoveCommand{target=100.5}`）≠ PLC 协议（"写模式=0，写位置高字=0x42C9，写位置低字=0x0000，写启动=1"）。

这个翻译层是"领域业务逻辑"与"硬件协议细节"之间的**唯一桥梁**。当 PLC 寄存器布局变化时，只改这个文件。

---

## 6. 第三层：Transport 传输层

> **职责：纯通讯。完全不懂 Axis、Gantry、MoveCommand。只懂 `uint16_t` 和 Modbus Function Code。**

---

### 6.1 IModbusClient.h（新增）

**路径**：`infrastructure/plc/transport/IModbusClient.h`

**职责**：Modbus 客户端的抽象接口。

```cpp
class IModbusClient {
public:
    virtual ~IModbusClient() = default;

    /// @brief 读保持寄存器（Function Code 03）
    virtual std::optional<std::vector<uint16_t>>
        readHoldingRegisters(uint16_t address, uint16_t count) = 0;

    /// @brief 写多个寄存器（Function Code 16）
    virtual ModbusResult
        writeHoldingRegisters(uint16_t address,
                              const std::vector<uint16_t>& values) = 0;

    /// @brief 批量执行写计划（默认逐条执行，子类可重写优化）
    virtual ModbusResult executeBatchWrite(const BatchWritePlan& plan);

    virtual std::string lastDiagnostic() const = 0;
};
```

**设计原则**：
- 接口只暴露 `uint16_t`，不暴露任何业务类型
- `ModbusResult` 是纯通讯层枚举（Ok / NetworkError / Timeout / ProtocolError / Busy / InvalidResponse）
- `executeBatchWrite` 默认逐条执行，子类可重写为合并优化

---

### 6.2 FakeModbusClient.h（新增 — TDD 核心）

**路径**：`infrastructure/plc/transport/FakeModbusClient.h`

**职责**：内存寄存器模拟，用于 TDD 测试。

**核心实现**：
- `std::unordered_map<uint16_t, uint16_t> m_registers` — 模拟 PLC RAM
- `readHoldingRegisters()` 从 map 读取连续寄存器
- `writeHoldingRegisters()` 写入 map
- 支持故障注入：`injectNextReadError()` / `injectNextWriteError()` — 模拟网络错误、超时、协议异常
- 支持直接读写：`setRegister()` / `getRegister()` — 测试初始化与断言

**为什么是 TDD 核心**：
- 与真实 `LibModbusClient` 完全同接口
- 测试覆盖的是"协议行为"，而非"socket 行为"
- 支持注入各种异常场景，实现完整的错误路径覆盖

---

### 6.3 LibModbusClient.h / .cpp（新增）

**路径**：`infrastructure/plc/transport/LibModbusClient.h` 和 `.cpp`

**职责**：真实 socket 通讯实现。

**实现要点**：
- 基于 **libmodbus** 库（开源 C 库，支持 Modbus TCP/RTU）
- 或自实现最小 Modbus TCP 帧封装（MBAP Header + Function Code + Data）
- `readHoldingRegisters()` → `modbus_read_registers()` 或 send/recv Modbus TCP 帧
- `writeHoldingRegisters()` → `modbus_write_registers()` 或 send/recv Modbus TCP 帧
- `executeBatchWrite()` 可重写为合并相邻写操作

**为什么这一层最不复杂**：socket 其实只是搬运工。真正的复杂性在 protocol 层（PLC 寄存器语义）和 connection 层（网络状态机）。

---

## 7. 第四层：Connection 连接管理层

### 7.1 ConnectionManager（新增，非常推荐）

**路径**：`infrastructure/plc/connection/ConnectionManager.h` / `.cpp`

**职责**：管理 PLC 连接的全生命周期。

**核心职责**：
1. **连接建立**：`connect(ip, port)` → TCP 三次握手
2. **连接断开**：`disconnect()` → 优雅关闭 socket
3. **心跳检测**：定期发送读请求验证 PLC 存活
4. **自动重连**：检测到断线后按退避策略重连
5. **超时管理**：socket read/write timeout 配置
6. **状态暴露**：`isConnected()` / `getState()` 供上层决策

**状态机**：
```
Disconnected → Connecting → Connected → HeartbeatLost → Reconnecting
                   ↑              ↓                         │
                   └──── Timeout / Error ←──────────────────┘
```

**为什么不要 Driver 管**：
- 网络状态机会越来越复杂。工业现场 PLC 重启、交换机抖动、网线松动是常态。
- 将连接管理独立出来，Driver 只关心"当前是否可通讯"。
- 未来如果切换为 EtherCAT 或 CANOpen，只需替换 `ConnectionManager` 实现。

**设计示例**：
```cpp
class ConnectionManager {
public:
    enum class State { Disconnected, Connecting, Connected, Reconnecting };

    bool connect(const std::string& host, uint16_t port);
    void disconnect();
    bool isConnected() const;
    State getState() const;

    // 给 IModbusClient 提供原生 socket fd（用于 libmodbus）
    int getSocketFd() const;

    // 心跳回调（由 Driver 的 pollFeedback 周期调用）
    void onHeartbeatSuccess();
    void onHeartbeatFailure();

private:
    void attemptReconnect();  // 退避重连策略
    State m_state = State::Disconnected;
    int m_socketFd = -1;
    std::string m_host;
    uint16_t m_port;
    int m_heartbeatFailCount = 0;
    int m_reconnectBackoffMs = 1000;  // 指数退避
};
```

---

## 8. 第五层：Feedback 反馈层

> **职责：将 PLC 寄存器原始数据解码为领域反馈对象，保证反馈的一致性。**

---

### 8.1 PlcFeedbackSnapshot（新增）

**路径**：`infrastructure/plc/feedback/PlcFeedbackSnapshot.h`

**职责**：**同一时刻**的 PLC 反馈一致性快照。

**为什么重要**：PLC 反馈必须在同一个 Modbus 读取周期内获取，保证所有轴的状态来自同一时刻。否则：
- X1 已更新 → X2 还没更新 → 龙门位置差瞬态错误
- 急停已触发 → 轴状态还没反映 → 安全状态不一致

**设计**：
```cpp
struct PlcFeedbackSnapshot {
    // 各轴原始寄存器数据（32 words 每轴）
    std::array<uint16_t, 32> x1Regs;
    std::array<uint16_t, 32> x2Regs;
    std::array<uint16_t, 32> yRegs;
    std::array<uint16_t, 32> zRegs;
    std::array<uint16_t, 32> rRegs;

    // 龙门反馈原始数据（16 words）
    std::array<uint16_t, 16> gantryRegs;

    // 急停 + 系统状态
    bool emergencyStop;
    bool systemReady;

    /// @brief 从批量读取的原始数据构建快照
    static PlcFeedbackSnapshot fromRegisters(const std::vector<uint16_t>& rawData);

    /// @brief 从 FakeModbusClient 的寄存器映射构建快照（测试用）
    static PlcFeedbackSnapshot fromClient(IModbusClient& client);
};
```

**正确流程**：
```
批量读取寄存器（一次 Modbus 帧获取 192 个寄存器）
    ↓
构建 Snapshot（冻结此时刻的所有数据）
    ↓
解码为领域反馈
    ↓
统一 applyFeedback()（一次性注入所有轴）
```

---

### 8.2 AxisFeedbackDecoder（新增）

**路径**：`infrastructure/plc/feedback/AxisFeedbackDecoder.h` / `.cpp`

**职责**：寄存器 → `AxisFeedback` 对象的解码器。

**设计**：
```cpp
class AxisFeedbackDecoder {
public:
    /// @brief 从快照解码 X 轴反馈（龙门逻辑轴 = 取 X1/X2 均值）
    AxisFeedback decodeX(const PlcFeedbackSnapshot& snapshot);

    /// @brief 从快照解码 X1 轴反馈
    AxisFeedback decodeX1(const PlcFeedbackSnapshot& snapshot);

    /// @brief 从快照解码 X2 轴反馈
    AxisFeedback decodeX2(const PlcFeedbackSnapshot& snapshot);

    AxisFeedback decodeY(const PlcFeedbackSnapshot& snapshot);
    AxisFeedback decodeZ(const PlcFeedbackSnapshot& snapshot);
    AxisFeedback decodeR(const PlcFeedbackSnapshot& snapshot);

    /// @brief 从快照解码龙门反馈
    GantryFeedback decodeGantry(const PlcFeedbackSnapshot& snapshot);

private:
    /// @brief 通用单轴解码（32 个寄存器 → AxisFeedback）
    AxisFeedback decodeAxis(const std::array<uint16_t, 32>& regs);
};
```

**单轴解码映射（示例）**：
```
regs[0..1]   → absPos      (float32, BigEndian)
regs[2]      → state       (uint16 enum)
regs[3]      → relZeroAbsPos (float32 低字，与 regs[4] 组合)
regs[5]      → posLimit    (bool bit 0)
regs[6]      → negLimit    (bool bit 0)
regs[7]      → getjogVelocity  (uint16)
regs[8]      → getMoveVelocity (uint16)
regs[9..10]  → posLimitValue   (float32)
regs[11..12] → negLimitValue   (float32)
// ... 更多位：alarm code, status word, 保留位
```

**为什么必须独立**：解码逻辑会越来越复杂。bit mask、alarm code、status word 等后期会爆炸。必须隔离在独立的解码器中。

---

## 9. 迁移策略

### 9.1 阶段划分

**Phase 1 — 基础设施搭建（不破坏现有代码）**
1. 创建 `infrastructure/plc/` 目录结构
2. 实现 `RegisterAddress.h`、`RegisterBlock.h`、`RegisterCodec`
3. 实现 `IModbusClient.h` 和 `FakeModbusClient.h`
4. 编写 protocol + transport 层的单元测试

**Phase 2 — 协议翻译层**
5. 实现 `RegisterWrite.h`、`RegisterRead.h`、`BatchWritePlan.h`
6. 实现 `PlcRegisterMap` — 将现有 `FakePLC::processCommand` 逻辑翻译为寄存器写入
7. 实现对 `PlcRegisterMap` 的 TDD 测试（使用 FakeModbusClient）

**Phase 3 — 反馈层**
8. 实现 `PlcFeedbackSnapshot` 和 `AxisFeedbackDecoder`
9. 将现有 `FakePLC` 的反馈生成逻辑重构为寄存器反馈 + 解码器
10. 编写反馈层的单元测试

**Phase 4 — Driver 重构**
11. 实现 `ModbusTcpDriver` — 组合 PlcRegisterMap + IModbusClient + AxisFeedbackDecoder
12. 重构 `FakeAxisDriver` — 内部改为组合 `FakeModbusClient`，复用相同的 PlcRegisterMap
13. 更新 `test_system_integration.cpp` — 验证 Fake 和旧行为完全一致

**Phase 5 — 连接管理 + 真实硬件**
14. 实现 `ConnectionManager`
15. 实现 `LibModbusClient`（基于 libmodbus 或自实现）
16. 真实 PLC 硬件联调

### 9.2 向后兼容保证

- `ISystemDriver` 接口**签名完全不变**
- `FakeAxisDriver` 外部行为**完全不变**（内部实现换为 FakeModbusClient 组合）
- `CommunicationResult` 和 `SystemCommand` 不变
- Domain / Application / Presentation 层**零改动**
- 所有现有测试**继续通过**

---

## 10. 测试策略

### 10.1 分层测试

| 测试层 | 测试内容 | 使用的 Fake | 测试文件 |
|--------|---------|-------------|---------|
| protocol | RegisterCodec 编解码正确性 | 无（纯函数） | `test_register_codec.cpp` |
| protocol | PlcRegisterMap 翻译正确性 | FakeModbusClient | `test_plc_register_map.cpp` |
| protocol | BatchWritePlan 合并/分片 | 无（纯逻辑） | `test_batch_write_plan.cpp` |
| transport | FakeModbusClient 读写 | 自身 | `test_fake_modbus_client.cpp` |
| transport | LibModbusClient 真实通讯 | 真实 PLC | `test_libmodbus_client.cpp` |
| feedback | AxisFeedbackDecoder 解码 | FakeModbusClient | `test_axis_feedback_decoder.cpp` |
| feedback | PlcFeedbackSnapshot 一致性 | FakeModbusClient | `test_plc_feedback_snapshot.cpp` |
| integration | FakeAxisDriver 端到端 | FakeModbusClient | `test_system_integration.cpp` |

### 10.2 TDD 核心优势

使用 `FakeModbusClient` 替代 `FakePLC` 后：

1. **测试的是协议行为**，而非函数调用行为 — 更接近真实场景
2. **故障注入**：可以精确模拟 NetworkError / Timeout / Busy / ProtocolError 等所有异常路径
3. **寄存器级断言**：测试可以检查某个寄存器的确切值，而不仅仅是最终 AxisFeedback
4. **真实 PLC 换 Fake 只需替换一个对象**：整个测试夹具只需将 `LibModbusClient` 换成 `FakeModbusClient`

---

## 11. 面向未来的扩展能力

### 11.1 新协议支持

当需要支持 EtherCAT / CANOpen / PROFINET / 三菱 MC 协议 / 西门子 S7 时：

| 需要新增 | 不需要改动 |
|---------|-----------|
| 新的 `IXxxClient` 实现 | Domain 层 |
| 新的 `XxxRegisterMap` | Application 层 |
| 新的 `XxxFeedbackDecoder` | Presentation 层 |
| 新的 `ConnectionManager` 子类 | ISystemDriver 接口 |

### 11.2 寄存器布局变更

PLC 工程师说"所有轴地址整体偏移 200"：
- **只改 `RegisterAddress.h`**
- `FakeModbusClient` 和 `LibModbusClient` 自动生效
- 全部测试仍然通过

### 11.3 多 PLC / 多设备组

一个 SystemContext 可以绑定一个 `ModbusTcpDriver` 实例，每个 Driver 实例拥有独立的：
- `ConnectionManager` — 不同的 IP:Port
- `IModbusClient` — 不同的 socket 连接
- 独立的 `PlcRegisterMap` — 可共享同一个地址映射表

---

## 12. 总结

本架构的核心思想不是"支持 Modbus TCP"，而是：

> **"设备协议变化时，业务层完全无感"。**
