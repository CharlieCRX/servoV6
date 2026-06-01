# servoV6 — 基于 Modbus TCP 的 infrastructure 架构演进设计文档 v4

## 文档版本

| **版本** | **日期**       | **说明**                                                     |
| -------- | -------------- | ------------------------------------------------------------ |
| v1.0     | 2026-05-21     | 初版：基于 Modbus TCP 的五层 infrastructure 架构设计         |
| v2.0     | 2026-05-22     | 补充 CommunicationResult 细分、分组架构、ISystemDriver 重构说明 |
| v3.0     | 2026-05-22     | 富元数据体系、三层编解码、集中注册中心、协议验证阶段。       |
| **v4.0** | **[当前日期]** | **重大架构升级：彻底分离“物理存储”与“逻辑解释”。引入 `RawBitSnapshot` / `RawWordSnapshot` 应对线圈与寄存器的不同物理维度。废弃大一统的 `RegisterValue`，解禁 `std::variant` 并引入 `PlcValue` 作为业务层统一实体。明确 `RegisterPoller` 的地址连续化打包职责。** |

## 1. 设计目标

将当前 `infrastructure` 层从单一 `FakePLC` + `FakeAxisDriver` 的仿真架构，演进为：

> **面向长期演化、支持多 PLC、多协议、零内存浪费、高性能** 的工业级基础设施层。

核心原则：

1. **Domain / Application / Presentation 完全无感** — 协议替换时业务层零改动。
2. **物理层与语义层彻底解耦** — 底层严格按真实网络 payload 存储（Bit/Word 分离），仅在最终投递给业务层时才执行语义映射。
3. **极速高频 Poll 支持** — 通过连续地址自动打包（Packing）和零拷贝解析，支撑 10ms 级别的反馈拉取。
4. **协议防腐层（ACL）** — PLC 寄存器布局、字节序、编码格式变更仅影响 protocol 层。

## 2. 五层分层架构 (Five-Layer Architecture)

自顶向下，我们将基础设施层分为五层：

### L1: Driver API (门面层 / Facade)

- **职责**：面向 Domain 层提供强类型的 C++ API。
- **形态**：`PlcDevice` / `ModbusTcpDriver`
- **演进**：屏蔽所有诸如 "D100", "M2" 等物理地址，对外只暴露 `readFloat("Y_Axis.ABS_POSITION")` 或强类型的 `readFeedback()`。

### L2: Protocol Runtime Core (协议运行时核心) **[v4 核心重构区]**

- **职责**：将逻辑变量映射为底层的物理读写操作，并负责数据的反序列化。
- **关键模块**：
  - `RegisterMetadata` & `RegisterRegistry`：寄存器元数据中心。
  - **[New] Memory Snapshot**：`RawBitSnapshot` 与 `RawWordSnapshot`，分别管理 FC01/02 和 FC03/04 的真实物理内存快照。
  - **[New] PlcValue**：基于 `std::variant` 的业务层统一数据类型。
  - `RegisterCodec`：纯粹的桥梁，负责从 Snapshot 中截取数据并转化为 `PlcValue`。
  - **[New] PlcPoller / AddressPacker**：负责将离散的寄存器元数据合并为连续的批量读写任务。

### L3: Transport (传输层)

- **职责**：执行单一的 Modbus PDU 读写操作。
- **形态**：`IModbusClient` 接口 (包含 `readCoils`, `readHoldingRegisters` 等原生语义)。

### L4: Connection (连接层)

- **职责**：管理 TCP Socket，处理断线重连、心跳保活。

### L5: Feedback / Event (事件与反馈闭环)

- **职责**：以固定频率驱动 L2 的 Poller 生成读取计划，通过 L3 取回数据更新 L2 的 Snapshot，并发布 Domain 事件。

## 3. L2 Protocol Runtime 深度剖析 (v4 重点)

### 3.1 核心痛点与 v4 解决方案

**历史痛点 (P1阶段)**：使用统一的 `std::vector<uint16_t>` 试图兼容所有的线圈(Coil)与寄存器(HoldingReg)。这导致了严重的概念错位（例如将 1 个 bit 的线圈强行膨胀为 1 个 uint16_t），使得底层无法对接真实的 Modbus Byte Buffer。

**v4 解决方案：切割物理层与逻辑层**

| **维度**                      | **责任归属模块**                    | **概念定义**                         | **范例**                               |
| ----------------------------- | ----------------------------------- | ------------------------------------ | -------------------------------------- |
| **物理存储 (Storage/Area)**   | `RawBitSnapshot`, `RawWordSnapshot` | 数据在通讯报文与内存中的真实粒度     | `FC01/Coil` 为 Bit, `FC03/Reg` 为 Word |
| **逻辑解释 (Interpretation)** | `RegisterType`                      | 从物理内存提取出二进制后，如何解释它 | 解析为 `Float32`, `Int16`, `Bool`      |
| **业务载体 (Business Value)** | `PlcValue` (`std::variant`)         | 交给上层控制逻辑的强类型最终结果     | `150.5f`, `true`, `401`                |

### 3.2 L2 组件流转机制

1. **[L3]** 返回真实的报文 Payload（`std::vector<uint8_t>` 用于线圈，`std::vector<uint16_t>` 用于寄存器）。
2. **[L2 快照层]** 将原生 Payload 直接 `std::move` 移入 `RawBitSnapshot` 或 `RawWordSnapshot`，实现零拷贝、零膨胀存储。
3. **[L1 门面层]** 发起读取请求：`readValue("Y_Axis.MOVE_DONE")`。
4. **[L2 编解码层]** `RegisterCodec` 根据元数据，从对应的 Snapshot 中进行位运算或字节序拼装，最终组装出 `PlcValue` 抛给 L1。

## 4. 开发与演进路线图 (Roadmap)

### 4.1 已完成 (Done)

- [x] **L2a Metadata** — `RegisterMetadata.h` / `EndianPolicy.h` / `ProtocolProfile.h`
- [x] **L2b Registry** — `RegisterRegistry.h`，实现按组/按区查询。
- [x] **L2c Validation** — `ProtocolConstraintValidator`，拦截非法的寄存器设计。
- [x] **轴元数据映射** — 梳理并落地 Y 轴所有的 M/D 寄存器映射。

### 4.2 短期目标：落地物理层与重构编解码 (P0) - **当前阶段**

- [ ] **物理快照层** — 实现 `RawBitSnapshot.h`（包含优雅的 Bit 提取算法）和 `RawWordSnapshot.h`。
- [ ] **业务值体系** — 引入 `PlcValue.h` (`using PlcValue = std::variant<...>`)，彻底废弃原有的 `RegisterValue` 作为双重身份的职责。
- [ ] **Codec 重构** — 改写 `RegisterCodec.h`，使其接口签名为：`static PlcValue decode(const RegisterInfo&, const RawBitSnapshot/RawWordSnapshot&)`。
- [ ] **极简门面层** — 实现最初版的 `PlcDevice`，验证从 Snapshot 到 PlcValue 的全链路打通。

### 4.3 中期目标：调度与通讯 (P1)

- [ ] **地址打包规划** — 实现 `AddressPacker.h`，能够遍历 `RegisterRegistry`，合并连续的寄存器地址，生成最少数量的读写 Task。
- [ ] **L3 Transport** — 定义 `IModbusClient.h`，实现 `FakeModbusClient.h`（模拟设备回包）与真实的 `LibModbusClient.h`。
- [ ] **L4 Connection** — `ConnectionManager.h`（心跳、断线重连）。

### 4.4 长期目标：高阶优化 (P2)

- [ ] **异步事件发布** — 将定期拉取的 Snapshot 增量比对，触发 `OnAxisAlarm` 或 `OnPositionChanged`。
- [ ] **多 PLC 集群** — 支持一台工控机通过不同的 `ProtocolProfile` 连接多种不同品牌的 PLC。

## A. 附录：核心变更对照表

| **核心组件**     | **v1 ~ v3 阶段的设计**               | **v4 阶段的设计**                    | **带来的工程价值**                                           |
| ---------------- | ------------------------------------ | ------------------------------------ | ------------------------------------------------------------ |
| **底层数据载体** | `RegisterValue` (`vector<uint16_t>`) | `RawBitSnapshot` / `RawWordSnapshot` | 内存占用骤降，完美贴合工业协议底层的 Bit/Byte 概念           |
| **业务数据载体** | 仍依赖 `RegisterValue` + 强制转型    | `PlcValue` (`std::variant`)          | 强类型，防呆设计，业务逻辑读取后可用 `std::get` 或 `std::visit` 安全处理 |
| **批量读取机制** | 尚未规划                             | `AddressPacker` (规划中)             | 大幅减少网络 IO 请求，支持 10ms 级别的实时反馈闭环           |