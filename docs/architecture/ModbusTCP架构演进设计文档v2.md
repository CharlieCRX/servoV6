# servoV6 — 协议运行时内核架构设计文档 v2

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-21 | 初版：基于 Modbus TCP 的五层 infrastructure 架构设计 |
| v2.0 | 2026-05-22 | 架构演进：从"Modbus TCP 通讯架构"升级为"协议元数据驱动的运行时内核" |

---

## 架构演进概述

### v1 → v2 的核心转变

v1 的架构重心是 **通讯分层**——Driver → Protocol → Transport → Connection → Feedback 五层模型，围绕"Modbus TCP 如何通讯"展开。

v2 的架构重心是 **协议元数据系统**——`RegisterInfo` 元数据驱动一切。通讯本身降级为基础设施组件，真正的核心变为：

> **"协议元数据库（RegisterRegistry）是整个 Protocol Runtime 的唯一真相源。"**

### 为什么必须升级

已完成的实现表明系统已经从"FakePLC 模拟阶段"进入"工业协议运行时内核阶段"：

| 已完成组件 | 架构意义 |
|-----------|----------|
| `RegisterMetadata` (RegisterInfo, RegisterBehavior, RegisterGroup) | 协议元数据模型 |
| `RegisterCodec` (Endianness-aware 编解码) | 数据编码防腐层 |
| `RegisterAddressY` (constexpr RegisterInfo 定义) | 元数据驱动地址声明 |
| Protocol Constraint TDD | 协议行为语义验证 |
| Command / Feedback 分离 | 命令请求与物理状态解耦 |
| Trigger 模型 (Level / Edge / AutoReset) | 行为语义建模 |
| pulseWidth 建模 | 脉冲宽度元数据 |

这些组件已经将系统推入"协议即元数据"阶段——Polling、BatchRead、Decoder、UI、Logger、Alarm、Snapshot 未来全部依赖 `RegisterInfo`。架构核心必须正式升级为 **协议元数据库**。

---

## 新架构总览

### 核心分层

```
┌──────────────────────────────────────────────────────────────────┐
│                    DOMAIN / APPLICATION                          │
│        Axis · Gantry · UseCase · Orchestrator · SystemCommand    │
│                                                                  │
│        永远不感知 PLC —— 只知道 ISystemDriver                      │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│                  L1: DRIVER 集成层                                │
│                                                                  │
│   ISystemDriver (不变)                                            │
│   ├── FakeAxisDriver (测试替身，内部组合 FakeModbusClient)          │
│   └── ModbusTcpDriver  (真实驱动，系统行为编排器)                   │
│                                                                  │
│   职责：命令流调度 · 反馈流调度 · 错误收敛                           │
└──────────────────────────────────────────────────────────────────┘
        │                                        │
        │  send()                                │  pollFeedback()
        ▼                                        ▼
┌──────────────────────────┐   ┌──────────────────────────────────┐
│  L2a: 命令翻译平面        │   │  L2b: 反馈调度平面                │
│                          │   │                                  │
│  PlcRegisterMap          │   │  PollingScheduler                │
│  Command → RegisterWrite │   │  BatchReadPlan 构建              │
│  → BatchWritePlan        │   │  → 合并连续反馈块                 │
│                          │   │  → 分频轮询策略                   │
└──────────────────────────┘   └──────────────────────────────────┘
             │                              │
             ▼                              ▼
┌──────────────────────────────────────────────────────────────────┐
│             L3: PROTOCOL RUNTIME 核心  ★v2 架构心脏★             │
│                                                                  │
│   ┌──────────────────────────────────────────────┐               │
│   │          RegisterRegistry                    │               │
│   │          (协议元数据库)                        │               │
│   │                                              │               │
│   │  RegisterInfo Metadata Database               │               │
│   │  - findByGroup()                             │               │
│   │  - findByBehavior()                          │               │
│   │  - findByArea()                              │               │
│   │  - findByAddress()                           │               │
│   └──────────────────────────────────────────────┘               │
│             │                                                    │
│             ▼                                                    │
│   ┌──────────────────────────────────────────────┐               │
│   │   ProtocolConstraintValidator                 │               │
│   │   (协议编译器 / 静态分析器)                     │               │
│   │                                              │               │
│   │   - 地址重叠检查                               │               │
│   │   - Coil 类型检查                             │               │
│   │   - Feedback ReadOnly 检查                    │               │
│   │   - Behavior 合法性检查                        │               │
│   │   - PulseWidth 合法性检查                      │               │
│   │   - Group 行为一致性检查                        │               │
│   └──────────────────────────────────────────────┘               │
│             │                                                    │
│             ▼                                                    │
│   ┌──────────────────────────────────────────────┐               │
│   │   RegisterCodec                              │               │
│   │   (数据类型编解码防腐层)                        │               │
│   └──────────────────────────────────────────────┘               │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│               L4: TRANSPORT 传输层                               │
│                                                                  │
│   IModbusClient          — 抽象接口                               │
│   ├── FakeModbusClient   — 内存寄存器模拟（TDD 核心）               │
│   └── LibModbusClient    — 真实 socket 通讯                       │
│                                                                  │
│   只懂: uint16_t[] · Function Code · 寄存器地址                    │
│   不懂: Axis · Gantry · MoveCommand · 业务语义                    │
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

### 数据流

**命令流（下行）—— 元数据驱动翻译**：

```
UI → UseCase → SystemCommand
    → ModbusTcpDriver::send()
        → RegisterRegistry.findByGroup(Command)
        → PlcRegisterMap::translate(cmd) → BatchWritePlan
            → RegisterCodec::encode*(value, endianness)
                → IModbusClient::writeHoldingRegisters(...)
                    → PLC 物理设备
```

**反馈流（上行）—— 批量调度优化**：

```
PLC 物理设备
    → PollingScheduler::buildReadPlan(registry)
        → 自动排序 + 合并连续块 + 分频调度
            → IModbusClient::readHoldingRegisters(block)
                → PlcFeedbackSnapshot::fromRegisters(...)
                    → AxisFeedbackDecoder::decode(snapshot)
                        → SystemContext: axis->applyFeedback()
                            → UI 刷新
```

---

## 新目录结构

以 `RegisterRegistry` 为核心的组织方式：

```
infrastructure/
├── ISystemDriver.h                      # 已有，不变更
│
├── driver/
│   ├── FakeAxisDriver.h                 # 保留，内部重构为组合 FakeModbusClient
│   ├── ModbusTcpDriver.h                # [新增] 真实驱动
│   └── ModbusTcpDriver.cpp              # [新增]
│
├── plc/
│   ├── protocol/                        # ★ 协议运行时核心
│   │   ├── RegisterMetadata.h           # [已完成] 元数据模型
│   │   ├── RegisterCodec.h              # [已完成] 编解码防腐层
│   │   ├── RegisterBehavior.h           # [已完成] 行为语义枚举
│   │   ├── RegisterAddressX.h           # [计划] X 轴寄存器地址定义
│   │   ├── RegisterAddressX1.h          # [计划] X1 轴寄存器地址定义
│   │   ├── RegisterAddressX2.h          # [计划] X2 轴寄存器地址定义
│   │   ├── RegisterAddressY.h           # [已完成] Y 轴寄存器地址定义
│   │   ├── RegisterAddressZ.h           # [计划] Z 轴寄存器地址定义
│   │   ├── RegisterAddressR.h           # [计划] R 轴寄存器地址定义
│   │   ├── RegisterAddressGantry.h      # [计划] 龙门寄存器地址定义
│   │   ├── RegisterAddressEmergency.h   # [计划] 急停寄存器地址定义
│   │   ├── RegisterRegistry.h           # [Step 1] 协议元数据库
│   │   ├── RegisterRegistry.cpp         # [Step 1]
│   │   ├── ProtocolConstraintValidator.h # [Step 2] 协议约束验证器
│   │   ├── ProtocolConstraintValidator.cpp # [Step 2]
│   │   ├── BatchWritePlan.h             # [已有] 批量写计划
│   │   ├── BatchReadPlan.h              # [Step 3] 批量读计划
│   │   ├── PlcRegisterMap.h             # [Step 6] 命令翻译核心
│   │   └── PlcRegisterMap.cpp           # [Step 6]
│   │
│   ├── polling/                         # ★ 轮询调度子系统
│   │   ├── PollingScheduler.h           # [Step 4] 批量读取调度
│   │   └── PollingScheduler.cpp         # [Step 4]
│   │
│   ├── transport/
│   │   ├── IModbusClient.h              # Modbus 客户端抽象接口
│   │   ├── FakeModbusClient.h           # [Step 5] 内存寄存器模拟
│   │   ├── FakeModbusClient.cpp         # [Step 5]
│   │   ├── LibModbusClient.h            # 真实 socket 通讯
│   │   └── LibModbusClient.cpp          # 真实 socket 通讯
│   │
│   └── feedback/
│       ├── PlcFeedbackSnapshot.h        # 一致性反馈快照
│       ├── AxisFeedbackDecoder.h        # 寄存器 → 领域反馈解码
│       └── AxisFeedbackDecoder.cpp      # 寄存器 → 领域反馈解码
│
├── logger/
│   ├── Logger.h
│   ├── LogContext.h
│   └── TraceScope.h
│
└── utils/
    └── CommandFormatter.h
```

---

## 核心模块详设

### 1. RegisterRegistry（第一优先级）

**路径**：`infrastructure/plc/protocol/RegisterRegistry.h` / `.cpp`

**职责**：统一管理所有 `RegisterInfo` 的元数据库。这是整个 Protocol Runtime 的核心。

**为什么必须存在**：

系统从单一 Y 轴地址定义扩展为包含 X / X1 / X2 / Y / Z / R / Gantry / Emergency 的完整协议面时，如果没有统一的 Registry，以下问题会迅速失控：

- 地址冲突检测变为手动审核
- Polling、BatchRead、UI 生成、Logger 需要各自扫描所有地址定义文件
- 新增一个寄存器时无法自动传播到所有消费端
- 按 Group / Behavior / Area 的查询需求散落各处

**接口设计**：

```cpp
class RegisterRegistry {
public:
    /// @brief 注册一个 RegisterInfo（通常在初始化阶段批量注册）
    void add(const RegisterInfo& reg);

    /// @brief 注册一个地址命名空间的所有 constexpr 定义
    template<typename Container>
    void addAll(const Container& regs);

    /// @brief 按分组查询
    std::vector<RegisterInfo> findByGroup(RegisterGroup group) const;

    /// @brief 按行为语义查询
    std::vector<RegisterInfo> findByBehavior(RegisterBehavior behavior) const;

    /// @brief 按寄存器区域查询
    std::vector<RegisterInfo> findByArea(RegisterArea area) const;

    /// @brief 按精确地址查询（唯一索引）
    const RegisterInfo* findByAddress(RegisterArea area, uint16_t address) const;

    /// @brief 获取所有已注册的寄存器信息
    const std::vector<RegisterInfo>& all() const;

    /// @brief 验证所有已注册的寄存器（委托给 ProtocolConstraintValidator）
    std::vector<std::string> validate() const;

private:
    std::vector<RegisterInfo> m_registers;
    // 内部使用 area+address 作为唯一键建立索引
};
```

**使用示例**：

```cpp
// 初始化阶段：注册所有轴的所有寄存器
RegisterRegistry registry;
registry.addAll(plc::reg::y_axis::command::all());
registry.addAll(plc::reg::y_axis::feedback::all());
registry.addAll(plc::reg::x_axis::command::all());
registry.addAll(plc::reg::x_axis::feedback::all());
// ... 其他轴

// 运行时查询
auto feedbackRegs = registry.findByGroup(RegisterGroup::Feedback);
auto alarms = registry.findByGroup(RegisterGroup::Alarm);
auto edgeTriggers = registry.findByBehavior(RegisterBehavior::ManualResetEdgeTrigger);
```

---

### 2. ProtocolConstraintValidator（协议编译器）

**路径**：`infrastructure/plc/protocol/ProtocolConstraintValidator.h` / `.cpp`

**职责**：对已注册的所有 `RegisterInfo` 执行工业协议静态分析。它不再是测试辅助，而是协议编译器。

**验证规则**：

| 规则 | 说明 | 严重性 |
|------|------|--------|
| 地址重叠检查 | 同 Area 下两个 RegisterInfo 不能地址重叠 | Error |
| Coil 类型检查 | Coil 区域只能使用 Bool 类型 | Error |
| Feedback ReadOnly | Feedback 组的寄存器必须是 ReadOnly | Error |
| Command WriteAccess | Command 组的寄存器必须是 ReadWrite 或 WriteOnly | Warning |
| Behavior 合法性 | Coil 区域不能使用 Continuous 行为 | Error |
| PulseWidth 合法性 | 仅 ManualResetEdgeTrigger 可定义 pulseWidth > 0 | Error |
| PulseWidth 必需性 | ManualResetEdgeTrigger 必须定义 pulseWidth > 0 | Error |
| Group 一致性 | 同一 Group 内的 Area 应保持一致 | Warning |
| 地址对齐 | Float32 地址不应跨越 HoldingReg 边界 | Warning |

**接口设计**：

```cpp
class ProtocolConstraintValidator {
public:
    struct Violation {
        enum class Severity { Error, Warning };
        Severity severity;
        std::string rule;
        std::string message;
        const RegisterInfo* involved;
    };

    /// @brief 验证整个 Registry
    std::vector<Violation> validate(const RegisterRegistry& registry);

    /// @brief 验证单个 RegisterInfo（增量注册时）
    std::vector<Violation> validateOne(
        const RegisterInfo& reg,
        const RegisterRegistry& existing);

private:
    void checkOverlap(const RegisterRegistry& registry,
                      std::vector<Violation>& out);
    void checkCoilType(const RegisterRegistry& registry,
                       std::vector<Violation>& out);
    void checkFeedbackReadOnly(const RegisterRegistry& registry,
                               std::vector<Violation>& out);
    void checkBehaviorLegality(const RegisterRegistry& registry,
                               std::vector<Violation>& out);
    void checkPulseWidth(const RegisterRegistry& registry,
                         std::vector<Violation>& out);
};
```

**设计原则**：

- 在系统初始化阶段执行，发现违规应立即报错（Error 级别阻止启动）
- 与现有的 TDD 约束测试互补——TDD 测试针对单个地址定义文件的声明期约束，Validator 针对运行时 Registry 的整体约束

---

### 3. BatchReadPlan（批量读计划）

**路径**：`infrastructure/plc/protocol/BatchReadPlan.h`

**职责**：将分散的反馈寄存器合并为最少次数的连续块读取。

**为什么比 BatchWritePlan 更重要**：工业系统 90% 的通讯是读操作。一次 `readHoldingRegisters(101, 30)` 远比 `read(101)` + `read(111)` + `read(124)` 高效。

**核心算法**：

```
输入: [RegisterInfo(address=101), RegisterInfo(address=111), RegisterInfo(address=124)]
输出: [RegisterBlock(start=101, count=30)]  // 一次读取覆盖全部
```

**接口设计**：

```cpp
class BatchReadPlan {
public:
    /// @brief 从 RegisterInfo 列表构建最优读取计划
    static std::vector<RegisterBlock> build(
        const std::vector<RegisterInfo>& regs);

    /// @brief 合并连续或接近的寄存器（gap <= threshold 时合并为一次读取）
    static std::vector<RegisterBlock> mergeContinuous(
        const std::vector<RegisterInfo>& regs,
        uint16_t maxGap = 8);  // gap <= 8 words 时合并

    /// @brief 分片：确保单帧不超过 PLC 最大读取长度
    static std::vector<RegisterBlock> split(
        const RegisterBlock& block,
        uint16_t maxWordsPerFrame = 123);
};
```

**与 PollingScheduler 的关系**：

- `PollingScheduler` 根据 Group 频率决定"什么时候读哪些组"
- `BatchReadPlan` 决定"这些组怎么合并为物理 Modbus 帧"
- 两者解耦：调度策略变化不影响合并算法

---

### 4. PollingScheduler（批量读取调度器）

**路径**：`infrastructure/plc/polling/PollingScheduler.h` / `.cpp`

**职责**：以最优方式调度 PLC 反馈读取。这是工业性能的核心。

**设计要点**：

```
1. 收集所有 Feedback 组寄存器 → registry.findByGroup(Feedback)
2. 自动排序地址 → 101, 111, 124, ...
3. 自动合并连续块 → 101~130 一次读取
4. 自动分频轮询 → Feedback 20ms / Alarm 200ms / Parameter 2s
```

**分频策略**：

| RegisterGroup | 推荐周期 | 理由 |
|---------------|----------|------|
| Feedback      | 20ms     | 实时位置/状态，UI 刷新依赖 |
| Alarm         | 200ms    | 报警响应不需要毫秒级 |
| Parameter     | 2s       | 设备参数很少变化，低频即可 |

**接口设计**：

```cpp
class PollingScheduler {
public:
    /// @brief 配置各分组的轮询周期（毫秒）
    struct GroupSchedule {
        RegisterGroup group;
        int intervalMs;       // 轮询间隔
    };

    /// @brief 设置调度策略
    void configure(const std::vector<GroupSchedule>& schedules);

    /// @brief 核心接口：根据 Registry 和当前 tick 构建本周期需读取的块列表
    /// @param registry 协议元数据库
    /// @param elapsedMs 距离上次调用的累计毫秒
    /// @return 本周期需要执行读取的 RegisterBlock 列表
    std::vector<RegisterBlock> buildReadPlan(
        const RegisterRegistry& registry,
        int elapsedMs);

private:
    /// @brief 合并连续寄存器块
    std::vector<RegisterBlock> mergeContinuous(
        const std::vector<RegisterInfo>& regs);

    // 每个 Group 的上次读取时间戳
    std::unordered_map<RegisterGroup, int> m_lastPollMs;
    std::vector<GroupSchedule> m_schedules;
};
```

**使用示例**：

```cpp
// 驱动层使用
void ModbusTcpDriver::pollFeedback(SystemContext& ctx) {
    auto now = steady_clock::now();
    int elapsed = duration_cast<milliseconds>(now - m_lastPoll).count();
    m_lastPoll = now;

    // 调度器决定本周期读哪些块
    auto blocks = m_pollingScheduler.buildReadPlan(m_registry, elapsed);

    for (auto& block : blocks) {
        auto rawData = m_client->readHoldingRegisters(block.start, block.count);
        if (!rawData) continue;  // 通讯失败，保留上次值

        auto snapshot = PlcFeedbackSnapshot::fromRegisters(*rawData);
        // ... 注入反馈
    }
}
```

---

### 5. FakeModbusClient（TDD 传输层核心）

**路径**：`infrastructure/plc/transport/FakeModbusClient.h` / `.cpp`

**职责**：基于内存 `unordered_map<uint16_t, uint16_t>` 的 Modbus 客户端模拟实现。与真实 `LibModbusClient` 完全同接口。

**设计**：

```cpp
class FakeModbusClient : public IModbusClient {
public:
    FakeModbusClient();

    // IModbusClient 接口实现
    std::optional<std::vector<uint16_t>>
        readHoldingRegisters(uint16_t address, uint16_t count) override;

    ModbusResult
        writeHoldingRegisters(uint16_t address,
                              const std::vector<uint16_t>& values) override;

    ModbusResult executeBatchWrite(const BatchWritePlan& plan) override;

    std::string lastDiagnostic() const override;

    // ========== 测试辅助接口 ==========

    /// @brief 直接写入单个寄存器（测试初始化）
    void setRegister(uint16_t address, uint16_t value);

    /// @brief 直接读取单个寄存器（测试断言）
    uint16_t getRegister(uint16_t address) const;

    /// @brief 故障注入：使下一次读操作返回错误
    void injectNextReadError(ModbusResult error);

    /// @brief 故障注入：使下一次写操作返回错误
    void injectNextWriteError(ModbusResult error);

    /// @brief 清空所有寄存器和注入状态
    void reset();

private:
    std::unordered_map<uint16_t, uint16_t> m_registers;
    std::optional<ModbusResult> m_nextReadError;
    std::optional<ModbusResult> m_nextWriteError;
};
```

**为什么是 TDD 核心**：

- 与真实 `LibModbusClient` 完全同接口 → 测试覆盖的是"协议行为"而非"socket 行为"
- 故障注入能力 → 覆盖所有异常路径（NetworkError / Timeout / ProtocolError）
- 寄存器级断言 → 测试可以检查某个寄存器的确切值

**与 FakeAxisDriver 的关系**：

```
当前:  FakeAxisDriver → FakePLC (直接业务逻辑仿真)
演进:  FakeAxisDriver → FakeModbusClient (与 ModbusTcpDriver 完全同构)
```

重构后 FakeAxisDriver 和 ModbusTcpDriver 共享完全相同的 PlcRegisterMap + IModbusClient 路径，使得 Fake 和 Real 行为完全一致。

---

### 6. PlcRegisterMap（命令翻译核心）

**路径**：`infrastructure/plc/protocol/PlcRegisterMap.h` / `.cpp`

**职责**：领域命令 → PLC 寄存器行为的翻译器。与 v1 设计一致，但现在它基于 `RegisterRegistry` 获取地址信息，而非硬编码。

**接口设计**：

```cpp
class PlcRegisterMap {
public:
    /// @brief 构造函数，注入 Registry 获取地址元数据
    explicit PlcRegisterMap(const RegisterRegistry& registry);

    /// @brief 将 SystemCommand 翻译为 BatchWritePlan
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
    BatchWritePlan translateZeroAbsolute(AxisId, const ZeroAbsoluteCommand&);
    BatchWritePlan translateSetRelativeZero(AxisId, const SetRelativeZeroCommand&);
    BatchWritePlan translateClearRelativeZero(AxisId, const ClearRelativeZeroCommand&);

    const RegisterRegistry& m_registry;
};
```

**关键设计变化**：v2 中 `PlcRegisterMap` 从 `RegisterRegistry` 获取地址元数据，确保它与 UI、Logger、Alarm 等所有消费者共享同一个真相源。

---

### 7. ModbusTcpDriver（系统行为编排器）

**路径**：`infrastructure/driver/ModbusTcpDriver.h` / `.cpp`

与 v1 设计一致，但新增了 `PollingScheduler` 和 `RegisterRegistry` 的注入。

```cpp
class ModbusTcpDriver : public ISystemDriver {
public:
    ModbusTcpDriver(
        std::unique_ptr<IModbusClient> client,
        std::shared_ptr<RegisterRegistry> registry,
        std::shared_ptr<PollingScheduler> scheduler);

    CommunicationResult send(const SystemCommand& cmd) override;
    void pollFeedback(SystemContext& ctx) override;

private:
    std::unique_ptr<IModbusClient> m_client;
    std::shared_ptr<RegisterRegistry> m_registry;
    std::shared_ptr<PollingScheduler> m_scheduler;
    PlcRegisterMap m_registerMap{*m_registry};
    AxisFeedbackDecoder m_feedbackDecoder;
};
```

---

## 开发路线图

### Step 1 — RegisterRegistry

- 实现 `RegisterRegistry` 类
- 支持多地址命名空间批量注册
- 提供 `findByGroup` / `findByBehavior` / `findByArea` / `findByAddress` 查询
- 编写 TDD：注册、查询、重名检测、空查询

### Step 2 — ProtocolConstraintValidator

- 实现完整的约束验证规则集
- 与现有 TDD 约束测试互补
- 编写 TDD：地址重叠、Coil 类型、Feedback ReadOnly、PulseWidth 等

### Step 3 — BatchReadPlan

- 实现连续地址合并算法
- 实现单帧分片
- 编写 TDD：分散地址合并、gap 阈值、超长帧分片

### Step 4 — PollingScheduler

- 实现分频轮询调度
- 实现基于 Registry 的动态读计划构建
- 编写 TDD：Feedback 20ms / Alarm 200ms 分频、连续块合并

### Step 5 — FakeModbusClient

- 实现基于 `unordered_map` 的内存 Modbus 客户端
- 实现故障注入
- 编写 TDD：读写正确性、故障注入、批量写

### Step 6 — PlcRegisterMap

- 实现基于 Registry 的命令翻译
- 将现有 `FakePLC::processCommand` 逻辑迁移为寄存器写入
- 编写 TDD：各命令翻译正确性、端到端 FakeModbusClient 验证

### Step 7 — ModbusTcpDriver

- 实现真实 Modbus TCP 驱动
- 组合 PlcRegisterMap + PollingScheduler + IModbusClient
- 重构 FakeAxisDriver 为组合 FakeModbusClient 的同构版本

---

## 架构设计原则总结

### 元数据驱动一切

```
RegisterInfo (元数据)
    ↓ 消费
    ├── RegisterRegistry (元数据库)
    ├── PollingScheduler (调度策略)
    ├── BatchReadPlan (合并优化)
    ├── ProtocolConstraintValidator (静态分析)
    ├── PlcRegisterMap (命令翻译)
    ├── AxisFeedbackDecoder (反馈解码)
    ├── UI (单位/描述显示)
    └── Logger (可读日志)
```

### 单一真相源

`RegisterRegistry` 是唯一的元数据聚合点。所有消费端通过 Registry 查询，而非硬编码遍历。

### 分频轮询

工业性能的核心不是"发多快"，而是"以最少的 IO 操作覆盖所有需求"。

### 协议编译器

静态分析在系统初始化阶段就捕获配置错误，而非运行时才发现。

---

## 与 v1 的向后兼容

- `ISystemDriver` 接口**签名完全不变**
- `FakeAxisDriver` 外部行为**完全不变**（内部实现换为 FakeModbusClient 组合）
- `CommunicationResult` 和 `SystemCommand` 不变
- `RegisterMetadata` / `RegisterCodec` / `RegisterAddressY` 不变
- Domain / Application / Presentation 层**零改动**
- 所有现有测试**继续通过**
