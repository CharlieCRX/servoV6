# servoV6 — 基于 Modbus TCP 的 infrastructure 架构演进设计文档 v3

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-21 | 初版：基于 Modbus TCP 的五层 infrastructure 架构设计 |
| v2.0 | 2026-05-22 | 补充 CommunicationResult 细分、分组架构、ISystemDriver 重构说明 |
| **v3.0** | **2026-05-22** | **反映当前实现进展：RegisterMetadata 富元数据体系、EndianPolicy 精化、ProtocolProfile、RegisterCodec 三层编解码、RegisterRegistry 集中注册中心。L2 正式定位为 Protocol Runtime Core，拆分为 Metadata / Registry / Validation / Codec / Planning 五个子层。引入协议编译阶段（Protocol Bootstrap & Validation Phase）。** |

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
│   send(cmd) ──→ PlcRegisterMap ──→ RegisterWrite[]               │
│                 ──→ BatchWritePlan ──→ IModbusClient              │
│   pollFeedback() ──→ RegisterBlock 批量读 ──→ PlcFeedbackSnapshot │
│                          ──→ AxisFeedbackDecoder ──→ applyFeedback()│
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│           L2: PROTOCOL RUNTIME CORE   (协议运行时核心)            │
│                                                                  │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ L2a: Metadata Layer — 协议对象元数据                        │  │
│  │   ✅ RegisterMetadata.h    — 核心元数据结构（Area/Type/Access/│
│  │                                Behavior/Group 枚举体系）     │  │
│  │   ✅ EndianPolicy.h        — ByteOrder × WordOrder 组合     │  │
│  │   ✅ ProtocolProfile.h     — PLC 厂商协议特征描述            │  │
│  │   ✅ RegisterAddressY.h    — Y 轴寄存器定义示例              │  │
│  │   ⬜ RegisterAddress X/X1/X2/Z/R  — 其余 5 轴               │  │
│  └────────────────────────────────────────────────────────────┘  │
│                              │                                    │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ L2b: Runtime Registry Layer — 运行时注册中心                 │  │
│  │   ✅ RegisterRegistry.h    — 集中注册 · 多维查询              │  │
│  └────────────────────────────────────────────────────────────┘  │
│                              │                                    │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ L2c: Protocol Validation Layer — 协议编译与验证              │  │
│  │   ⬜ ProtocolConstraintValidator.h/cpp  — Runtime 正式组件   │  │
│  │   ⬜ ProtocolViolation.h              — 违规描述 DTO          │  │
│  │                                                              │  │
│  │   约束集：地址重叠检查｜类型-区域一致性｜权限-分组一致性      │  │
│  │          行为-脉冲宽度一致性｜字长自检｜命名唯一性             │  │
│  └────────────────────────────────────────────────────────────┘  │
│                              │                                    │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ L2d: Codec Layer — 三层编解码引擎                            │  │
│  │   ✅ RegisterCodec.h       — L1 原生值 / L2 端序驱动 /       │  │
│  │                                L3 元数据+Profile 驱动         │  │
│  └────────────────────────────────────────────────────────────┘  │
│                              │                                    │
│  ┌────────────────────────────────────────────────────────────┐  │
│  │ L2e: Planning Layer — 通讯规划与优化                          │  │
│  │   ⬜ RegisterBlock.h       — 连续寄存器块定义                 │  │
│  │   ⬜ RegisterWrite.h       — 单次写操作 DTO                   │  │
│  │   ⬜ RegisterRead.h        — 单次读操作 DTO                   │  │
│  │   ⬜ BatchWritePlan.h      — 批量写优化（合并 + 分片）        │  │
│  │   ⬜ PlcRegisterMap.h      — 业务命令 → RegisterWrite[] 翻译  │  │
│  └────────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│               L3: TRANSPORT 传输层                               │
│                                                                  │
│   ⬜ IModbusClient          — 抽象接口                            │
│   ⬜ FakeModbusClient       — 内存寄存器模拟（TDD 核心）           │
│   ⬜ LibModbusClient        — 真实 socket 通讯                    │
│                                                                  │
│   只懂: uint16_t[] · Function Code · 寄存器地址                    │
│   不懂: Axis · Gantry · MoveCommand · 业务语义                    │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│              L4: CONNECTION 连接管理层                           │
│                                                                  │
│   ⬜ ConnectionManager      — 连接生命周期管理                    │
│   职责：reconnect · heartbeat · timeout · socket state · retry    │
└──────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────┐
│              L5: FEEDBACK 反馈层                                 │
│                                                                  │
│   ⬜ PlcFeedbackSnapshot    — 同一时刻一致性快照                   │
│   ⬜ AxisFeedbackDecoder    — 寄存器 → AxisFeedback 解码          │
└──────────────────────────────────────────────────────────────────┘
```

**图例**：✅ = 已实现  ⬜ = 待实现

### 核心流程图

**命令流（下行）**：
```
UI → UseCase → SystemCommand
    → ModbusTcpDriver::send()
        → PlcRegisterMap::translate(cmd)          // 业务翻译：SystemCommand → RegisterWrite[]
            → BatchWritePlan::plan(writes)         // 物理优化：合并/分片 → Modbus 帧序列
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

**协议启动流程（新增）**：
```
RegisterRegistry registry;
registry.addAll({ ... All register definitions ... });

ProtocolConstraintValidator validator;     // ← Runtime 正式组件
auto violations = validator.validate(registry);  // ← 协议编译阶段

if (!violations.empty())
    throw ProtocolConfigurationException(violations);  // ← 禁止以非法协议启动

// 协议世界合法 → 进入 Runtime
PlcRegisterMap map(registry);
BatchWritePlan planner(profile);
```

---

## 3. 当前目录结构

```
infrastructure/
├── CMakeLists.txt
├── ISystemDriver.h                    # ✅ 已有 — 统一接口
├── FakeAxisDriver.h                   # ✅ 已有 — 测试替身（分组感知）
├── FakePLC.h                          # ✅ 已有 — 物理引擎仿真
│
├── logger/                            # 日志基础设施
│   ├── LogContext.h
│   ├── Logger.h
│   └── TraceScope.h
│
├── plc/
│   └── protocol/                      # L2: Protocol Runtime Core
│       │
│       ├── # ====== L2a: Metadata ======
│       ├── RegisterMetadata.h         # ✅ 核心元数据结构 + 枚举
│       ├── EndianPolicy.h             # ✅ ByteOrder × WordOrder
│       ├── ProtocolProfile.h          # ✅ PLC 协议特征（汇川预设）
│       ├── RegisterAddressY.h         # ✅ Y 轴寄存器定义（示例）
│       ├── RegisterAddressX.h         # ⬜ X 轴（逻辑轴 = X1/X2 均值）
│       ├── RegisterAddressX1.h        # ⬜ X1 物理轴
│       ├── RegisterAddressX2.h        # ⬜ X2 物理轴
│       ├── RegisterAddressZ.h         # ⬜ Z 轴
│       ├── RegisterAddressR.h         # ⬜ R 轴
│       ├── RegisterAddressGantry.h    # ⬜ 龙门控制
│       └── RegisterAddressEmergency.h # ⬜ 急停
│       │
│       ├── # ====== L2b: Registry ======
│       ├── RegisterRegistry.h         # ✅ 集中注册中心 + 多维查询
│       │
│       ├── # ====== L2c: Validation (正式 Runtime 组件) ======
│       ├── validator/
│       │   ├── ProtocolViolation.h              # ⬜ 违规描述 DTO
│       │   ├── ProtocolConstraintValidator.h    # ⬜ 协议约束校验器
│       │   └── ProtocolConstraintValidator.cpp  # ⬜ 校验器实现
│       │
│       ├── # ====== L2d: Codec ======
│       ├── RegisterCodec.h            # ✅ 三层编解码引擎
│       │
│       └── # ====== L2e: Planning ======
│           ├── RegisterBlock.h        # ⬜ 连续寄存器块
│           ├── RegisterWrite.h        # ⬜ 单次写 DTO
│           ├── RegisterRead.h         # ⬜ 单次读 DTO
│           ├── BatchWritePlan.h       # ⬜ 批量写优化（合并 + 分片）
│           └── PlcRegisterMap.h       # ⬜ 业务命令 → RegisterWrite[]
│
├── transport/                     # ⬜ L3 传输层
│   ├── IModbusClient.h
│   ├── FakeModbusClient.h
│   └── LibModbusClient.h
│
├── connection/                    # ⬜ L4 连接管理
│   └── ConnectionManager.h
│
├── feedback/                      # ⬜ L5 反馈
│   ├── PlcFeedbackSnapshot.h
│   └── AxisFeedbackDecoder.h
│
└── utils/
    └── CommandFormatter.h         # ✅ 命令格式化工具
│
tests/
├── infrastructure/
│   ├── test_fake_plc.cpp              # ✅ 保留
│   ├── test_system_integration.cpp    # ✅ 保留
│   └── protocol/
│       ├── test_register_codec.cpp        # ✅ Level 1/2/3 编解码
│       ├── test_register_registry.cpp     # ✅ Registry 多维查询
│       ├── test_protocol_validator.cpp    # ✅ Validator 自身的单元测试
│       ├── test_batch_write_plan.cpp      # ⬜ 合并/分片
│       └── test_plc_register_map.cpp      # ⬜ 业务命令→Write[] 翻译
```

---

## 4. v3 核心演进：Protocol Runtime Core — 从"工具函数库"到"协议世界模型"

> **v1 到 v3 的最大变化：不再把 protocol 层视为地址常量 + 编解码的工具函数集合，而是将其正式定位为"工业协议运行时核心（Protocol Runtime Core）"。**

### 4.1 L2 子层拆分的必要性

**v1/v2 的设计**：L2 Protocol Layer 是一个扁平的模块集合。

**v3 的现实**：当前已实现的代码形成了清晰的分层依赖关系：

```
Metadata  (RegisterInfo, EndianPolicy, ProtocolProfile)
    ↓
Registry  (RegisterRegistry)  — 依赖 Metadata 的 RegisterInfo
    ↓  ← 中间缺少"协议编译"步骤，这是当前最大缺口
Validation (ProtocolConstraintValidator)  — Runtime 正式组件
    ↓
Codec     (RegisterCodec)  — 依赖 EndianPolicy
    ↓
Planning  (RegisterBlock, BatchWritePlan, PlcRegisterMap)  — 依赖以上全部
```

**为什么不能跳过 Validation**：

当前 `test_protocol_validator.cpp` 只是测试层验证。但 `RegisterInfo` 不是"运行时输入"——它是**协议声明语言（DSL）**。任何由声明构建的"协议世界（Protocol World）"在进入 Runtime 之前必须经过编译验证：

- 地址重叠（Float32 占用 2 个寄存器，相邻 Int16 不应落在这 2 个寄存器范围内）
- 区域-类型一致性（Coil 不能是 Float32）
- 权限-分组一致性（Feedback 组必须 ReadOnly）
- 行为-脉冲一致性（ManualResetEdgeTrigger 必须 pulseWidthMs > 0）
- 字长自检（wordCount() 必须与 type 匹配）

这就是 **Protocol Bootstrap & Validation Phase（协议启动验证阶段）**。

### 4.2 正确的 Runtime 初始化流程

```cpp
// ====== Protocol Bootstrap Phase ======

// Step 1: 声明协议世界（Metadata）
RegisterRegistry registry;
registry.addAll({
    plc::reg::x_axis::command::ENABLE_REQUEST,
    plc::reg::x_axis::command::ABS_TARGET,
    plc::reg::x_axis::feedback::STATE,
    // ... 所有寄存器定义
});

// Step 2: 协议编译（Validation）
ProtocolConstraintValidator validator;
auto violations = validator.validate(registry);

// Step 3: 禁止以非法协议启动
if (!violations.empty()) {
    LOG_ERROR("Protocol configuration is invalid:");
    for (auto& v : violations) LOG_ERROR("  - {}", v.description);
    throw ProtocolConfigurationException(violations);
}

// Step 4: 协议世界合法 → 进入 Runtime
PlcRegisterMap map(registry, profile);    // 业务翻译器
BatchWritePlan planner(profile);          // 物理优化器

// Step 5: 此时 ModbusTcpDriver 才可以安全使用 map + planner
```

**这个流程至关重要**：它不是"可选的测试"，而是"Runtime 的前置条件"。就像编译器不会让语法错误的 C++ 代码运行一样，Protocol Runtime 也不能让地址重叠的协议配置启动。

### 4.3 L2 各子层职责精确定义

| 子层 | 职责 | 核心类型 | 状态 |
|------|------|---------|------|
| **L2a Metadata** | 协议对象元数据定义 | `RegisterInfo`, `EndianPolicy`, `ProtocolProfile` | ✅ |
| **L2b Registry** | 运行时集中注册与查询 | `RegisterRegistry` | ✅ |
| **L2c Validation** | 协议世界合法性编译验证 | `ProtocolConstraintValidator`, `ProtocolViolation` | ⬜ |
| **L2d Codec** | 值 ↔ 寄存器的编解码 | `RegisterCodec` | ✅ |
| **L2e Planning** | 通讯规划与物理优化 | `RegisterBlock`, `RegisterWrite`, `BatchWritePlan`, `PlcRegisterMap` | ⬜ |

### 4.4 PlcRegisterMap 与 BatchWritePlan 的职责边界（v3 修正）

**v1 文档中的顺序问题**：将 `PlcRegisterMap` 放在 `BatchWritePlan` 之前描述，且暗示 `PlcRegisterMap` 直接产出 Modbus 写计划。

**v3 修正后的正确关系**：

```
PlcRegisterMap                    BatchWritePlan
┌─────────────────────┐           ┌──────────────────────┐
│  业务语义翻译层       │           │  物理通讯优化层        │
│                     │           │                      │
│  MoveCommand        │           │  RegisterWrite[]     │
│  {target=100.5}     │           │   → 合并相邻写入      │
│       ↓             │           │   → 分片（>120 reg)  │
│  RegisterWrite[]   ──┼──输出──→ │   → FC16 连续优化    │
│  [                   │           │   → 最优帧序列       │
│    {addr:24, vals:..}│           └──────────────────────┘
│    {addr:42, vals:[1]}│
│  ]                   │
│                     │
│  只关心：             │
│  "使能轴X" → 哪些    │
│  寄存器要被写入       │
└─────────────────────┘
```

**核心区分**：

- **PlcRegisterMap = 逻辑翻译**：`SystemCommand` → `RegisterWrite[]`。它不知道 Modbus 帧长限制，不知道相邻寄存器可以合并。
- **BatchWritePlan = 物理优化**：`RegisterWrite[]` → 最优 Modbus 物理帧序列。它不知道 MoveCommand，只知道连续的寄存器地址和值。

驱动层调用顺序：

```cpp
// ModbusTcpDriver::send()
auto writes = m_registerMap.translate(cmd);      // 逻辑翻译
auto frames = m_batchPlan.plan(writes);           // 物理优化
return m_client->executeFrames(frames);           // 网络发送
```

### 4.5 正确的 L2e 开发顺序

```
Step 1: RegisterBlock.h      — 连续寄存器块（基元）
Step 2: RegisterWrite.h      — 单次写操作 DTO
Step 3: RegisterRead.h       — 单次读操作 DTO
Step 4: BatchWritePlan.h     — 基于 RegisterWrite[] 的物理优化
Step 5: PlcRegisterMap.h     — 基于 RegisterRegistry + RegisterWrite 的业务翻译
```

**不能颠倒**：`PlcRegisterMap` 的返回值类型必须是 `RegisterWrite[]`，而 `RegisterWrite` 必须先于 `BatchWritePlan` 定义。

---

## 5. RegisterMetadata 富元数据体系（v3 已实现）

### 5.1 v1 设计 → v3 实现对比

| 维度 | v1 设计 | v3 实现 |
|------|---------|---------|
| 寄存器定义 | `constexpr uint16_t` 地址常量 | `constexpr RegisterInfo` 自描述结构体 |
| 大小端 | `enum class Endianness {4 种}` | `EndianPolicy { ByteOrder + WordOrder }` 精确组合 |
| 协议特征 | 散落在 Codec 参数中 | `ProtocolProfile` 集中描述 + `INOVANCE_PROFILE` 预设 |
| 寄存器行为 | 隐式约定 | `RegisterBehavior` 枚举（5 种行为语义） |
| 分组策略 | 注释约定 | `RegisterGroup` 枚举（4 组）驱动批量读写策略 |
| 地址定义 | 单个头文件 | 按轴/功能分离 + `RegisterRegistry` 集中管理 |
| 字长计算 | 手动编码 | `wordCount()` 编译期自动推导 |
| 端序覆盖 | 不支持 | `endianOverride` 可选字段，寄存器级精细控制 |
| 协议验证 | 无 | `ProtocolConstraintValidator` Runtime 正式组件（L2c） |

### 5.2 核心类型体系

#### 5.2.1 RegisterArea — 寄存器区域

```cpp
enum class RegisterArea { Coil, HoldingReg };
```

将 IEC 61131-3 的 `%MX` 和 `%MW` 映射为类型安全的枚举。

#### 5.2.2 RegisterType — 寄存器数据类型

```cpp
enum class RegisterType { Bool, Int16, Float32, String };

constexpr uint16_t getWordCount(RegisterType type) {
    switch (type) {
        case RegisterType::Bool:    return 1;
        case RegisterType::Int16:   return 1;
        case RegisterType::Float32: return 2;
        default:                    return 1;
    }
}
```

#### 5.2.3 RegisterAccess — 访问权限

```cpp
enum class RegisterAccess { ReadOnly, WriteOnly, ReadWrite };
```

**L2c 强制约束**：Feedback 组必须 ReadOnly，Command 组必须 ReadWrite。

#### 5.2.4 RegisterBehavior — 行为语义

```cpp
enum class RegisterBehavior {
    Level,                    // 电平触发（持续保持）
    ManualResetEdgeTrigger,   // 手动复位边沿触发（软件需控制 ON → delay → OFF）
    AutoResetEdgeTrigger,     // 自动复位边沿触发（PLC 端自动复位）
    Continuous,               // 连续状态反馈
    Latch                     // 锁存状态（需明确 Reset）
};
```

**L2c 强制约束**：`ManualResetEdgeTrigger` 必须 `pulseWidthMs > 0`。

#### 5.2.5 RegisterGroup — 分组语义

```cpp
enum class RegisterGroup {
    Command,      // 下发指令（高频写）
    Feedback,     // 实时状态反馈（高频 Poll）
    Parameter,    // 设备参数（低频/一次性读取）
    Alarm         // 报警区（事件驱动读取）
};
```

分组决定批量读取策略：Feedback 组每 tick 全读，Command 组按需写，Parameter 组启动时读一次，Alarm 组事件驱动。

### 5.3 RegisterInfo — 核心元数据结构

```cpp
struct RegisterInfo {
    RegisterArea area;
    uint16_t address;
    RegisterType type;
    RegisterAccess access;
    RegisterBehavior behavior;
    RegisterGroup group;
    const char* unit;           // 物理单位 ("mm", "mm/s", "")
    const char* description;    // 人类可读描述 (UI/Logger极其需要)
    uint32_t pulseWidthMs;      // 脉冲宽度 (仅对 ManualResetEdgeTrigger 有效)
    std::optional<EndianPolicy> endianOverride = std::nullopt;
    constexpr uint16_t wordCount() const { return getWordCount(type); }
};
```

**关键设计决策**：

1. **`const char*` 而非 `std::string`**：`constexpr` 全局常量，避免动态分配。
2. **`endianOverride`**：默认 `std::nullopt` → 继承 `ProtocolProfile.defaultEndian`。
3. **`wordCount()` 编译期计算**：零运行时开销。

### 5.4 EndianPolicy — 精确的大小端策略

```cpp
enum class ByteOrder {
    BigEndian,    // 字内高字节在前 (AB)
    LittleEndian  // 字内低字节在前 (BA)
};

enum class WordOrder {
    HighWordFirst, // 多寄存器中，高位字在前
    LowWordFirst   // 多寄存器中，低位字在前
};

struct EndianPolicy {
    ByteOrder byteOrder;
    WordOrder wordOrder;
};
```

**v1 的 4 种 Endianness 为什么不精确**：`BigEndianSwap`（CDAB）= `BigEndian` ByteOrder + `LowWordFirst` WordOrder。拆解后每个组合精确对应一个厂商：

| ByteOrder | WordOrder | 等价 v1 | 代表厂商 |
|-----------|-----------|---------|---------|
| BigEndian | HighWordFirst | BigEndian | 标准 Modbus |
| BigEndian | LowWordFirst | BigEndianSwap | **汇川 H5U** |
| LittleEndian | LowWordFirst | LittleEndian | 台达 |
| LittleEndian | HighWordFirst | LittleEndianSwap | 松下 |

### 5.5 RegisterCodec — 三层编解码引擎

```cpp
// Level 1: 基础单寄存器编解码（不受大小端影响）
static std::vector<uint16_t> encodeBool(bool value);
static bool decodeBool(const std::vector<uint16_t>& regs);

// Level 2: 核心数学拼装引擎（纯 EndianPolicy 驱动，TDD 友好）
static std::vector<uint16_t> encodeFloat(float value, EndianPolicy policy);
static float decodeFloat(const std::vector<uint16_t>& registers, EndianPolicy policy);

// Level 3: 业务代理层（元数据 + Profile 驱动）
static EndianPolicy resolvePolicy(const RegisterInfo& reg, const ProtocolProfile& profile);
static std::vector<uint16_t> encode(float value, const RegisterInfo& reg, const ProtocolProfile& profile);
static float decodeFloat(const std::vector<uint16_t>& regs, const RegisterInfo& reg, const ProtocolProfile& profile);
```

| 层级 | 输入 | 适用场景 |
|------|------|---------|
| Level 1 | 原生值 | Bool/Uint16 直通 |
| Level 2 | 原生值 + EndianPolicy | TDD 单元测试（纯函数） |
| Level 3 | 原生值 + RegisterInfo + ProtocolProfile | Driver 业务调用 |

### 5.6 ProtocolProfile — PLC 协议特征描述

```cpp
struct ProtocolProfile {
    std::string_view name;
    EndianPolicy defaultEndian;
    uint16_t maxReadRegisters;
    uint16_t maxWriteRegisters;
    bool coilUsesFF00;
    bool supportsMixedEndian;
};

constexpr ProtocolProfile INOVANCE_PROFILE {
    "Inovance_H5U&Easy",
    { ByteOrder::BigEndian, WordOrder::LowWordFirst },
    120, 120, true, false
};
```

### 5.7 RegisterRegistry — 集中注册中心

```cpp
class RegisterRegistry {
public:
    void add(const RegisterInfo& reg);
    void addAll(std::span<const RegisterInfo> regs);
    void addAll(std::initializer_list<RegisterInfo> regs);

    const std::vector<RegisterInfo>& all() const;
    std::vector<RegisterInfo> findByGroup(RegisterGroup group) const;
    std::vector<RegisterInfo> findByArea(RegisterArea area) const;
    const RegisterInfo* findByAddress(RegisterArea area, uint16_t address) const;
};
```

**核心价值**：

- **集中管理**：所有寄存器定义汇总到一处，避免分散在多个 axis 文件中导致遗漏或冲突。
- **多维查询**：按 Group 查（构建批量读计划）、按 Address 查（命令翻译时快速定位）。
- **`addAll({...})` 语法糖**：支持花括号初始化列表，注册时简洁优雅。

### 5.8 RegisterAddressY — 寄存器定义示例

```cpp
namespace plc::reg::y_axis::command {
    constexpr RegisterInfo ENABLE_REQUEST = {
        RegisterArea::Coil, 1, RegisterType::Bool, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "", "使能轴Y电机请求", 0
    };

    constexpr RegisterInfo ABS_MOVE_TRIGGER = {
        RegisterArea::Coil, 42, RegisterType::Bool, RegisterAccess::ReadWrite,
        RegisterBehavior::ManualResetEdgeTrigger, RegisterGroup::Command,
        "", "绝对定位触发脉冲", 50   // ← 50ms 脉冲宽度，L2c 自动验证
    };

    constexpr RegisterInfo ABS_TARGET = {
        RegisterArea::HoldingReg, 24, RegisterType::Float32, RegisterAccess::ReadWrite,
        RegisterBehavior::Level, RegisterGroup::Command,
        "mm", "绝对定位目标距离", 0
    };
}

namespace plc::reg::y_axis::feedback {
    constexpr RegisterInfo STATE = {
        RegisterArea::HoldingReg, 101, RegisterType::Int16, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "", "轴Y当前运行状态", 0
    };

    constexpr RegisterInfo ABS_POSITION = {
        RegisterArea::HoldingReg, 124, RegisterType::Float32, RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous, RegisterGroup::Feedback,
        "mm", "轴Y实时绝对位置", 0
    };

    constexpr RegisterInfo ALARM_CODE = {
        RegisterArea::HoldingReg, 111, RegisterType::Int16, RegisterAccess::ReadOnly,
        RegisterBehavior::Latch, RegisterGroup::Alarm,
        "", "轴Y故障报警代码", 0
    };
}
```

---

## 6. ProtocolConstraintValidator — Runtime 正式组件（v3 新增子层）

> **v3 最大缺口：当前 Validator 仅存在于测试层。需要正式进入 L2c 作为 Runtime 的正式模块。**

### 6.1 为什么 Validator 不能只是测试辅助

`RegisterInfo` 本质上不是"运行时输入"，而是**协议声明语言（Protocol DSL）**。当多个工程师在不同文件中定义寄存器时，可能出现：

- 地址重叠：`Float32` 占 2 个寄存器，相邻 `Int16` 落入同一地址
- 类型-区域冲突：`Coil` 上定义 `Float32`
- 权限-分组冲突：`Feedback` 组标记为 `WriteOnly`
- 行为-脉冲缺失：`ManualResetEdgeTrigger` 无 `pulseWidthMs`
- 字长不一致：`wordCount()` 与 `type` 不匹配
- 命名冲突：同一地址被多个 `RegisterInfo` 声明

这些是**协议配置错误**，不是测试 bug。必须在 Runtime 初始化阶段（Protocol Bootstrap）被拦截，而不是等到测试运行时才发现。

### 6.2 目标文件结构

```
infrastructure/plc/protocol/validator/
├── ProtocolViolation.h                 # ⬜ 违规描述 DTO
├── ProtocolConstraintValidator.h       # ⬜ 协议约束校验器
└── ProtocolConstraintValidator.cpp     # ⬜ 校验器实现
```

### 6.3 ProtocolViolation — 违规描述 DTO

```cpp
struct ProtocolViolation {
    enum class Severity { Error, Warning };
    Severity severity;
    std::string description;           // 人类可读描述
    const RegisterInfo* regA;          // 涉及的寄存器 A
    const RegisterInfo* regB;          // 涉及的寄存器 B（冲突场景）
};
```

### 6.4 ProtocolConstraintValidator — 校验器接口

```cpp
class ProtocolConstraintValidator {
public:
    /// @brief 验证整个寄存器注册表，返回所有违规
    /// @return 违规列表（空 = 协议合法）
    std::vector<ProtocolViolation> validate(const RegisterRegistry& registry);

private:
    void checkAddressOverlap(const RegisterRegistry& registry,
                             std::vector<ProtocolViolation>& out);
    void checkTypeAreaConsistency(const RegisterRegistry& registry,
                                  std::vector<ProtocolViolation>& out);
    void checkAccessGroupConsistency(const RegisterRegistry& registry,
                                     std::vector<ProtocolViolation>& out);
    void checkBehaviorPulseConsistency(const RegisterRegistry& registry,
                                       std::vector<ProtocolViolation>& out);
    void checkWordCountConsistency(const RegisterRegistry& registry,
                                   std::vector<ProtocolViolation>& out);
};
```

### 6.5 校验规则全集

| 规则 ID | 检查项 | 严重性 | 说明 |
|---------|--------|--------|------|
| R01 | 地址重叠 | Error | `Float32` 占 2 reg，后续寄存器 addr 不能落在 [addr, addr+1] 内 |
| R02 | Coil → Float32 | Error | `Coil` 区域只能 `Bool` |
| R03 | Coil → Int16 | Error | `Coil` 区域不能是 `Int16` |
| R04 | Feedback → WriteOnly | Error | `Feedback` 组必须 `ReadOnly` |
| R05 | Command → ReadOnly | Error | `Command` 组必须 `ReadWrite` |
| R06 | EdgeTrigger → pulseWidth=0 | Error | `ManualResetEdgeTrigger` 必须 `pulseWidthMs > 0` |
| R07 | Level → pulseWidth>0 | Warning | `Level` 行为通常不需要 `pulseWidthMs` |
| R08 | wordCount 不一致 | Error | `wordCount()` 必须与 `type` 匹配 |
| R09 | 命名冲突 | Error | 同一 `(area, address)` 不能出现两次 |

### 6.6 正确的使用位置 — Protocol Bootstrap Phase

```cpp
// ====== main.cpp 或 Application::initialize() ======

void Application::initializeProtocol() {
    RegisterRegistry registry;

    // L2b: 注册所有寄存器
    registry.addAll(plc::reg::x1_axis::command::all());
    registry.addAll(plc::reg::x1_axis::feedback::all());
    registry.addAll(plc::reg::x2_axis::command::all());
    registry.addAll(plc::reg::x2_axis::feedback::all());
    // ... 其余轴 + 龙门 + 急停

    // L2c: 协议编译验证
    ProtocolConstraintValidator validator;
    auto violations = validator.validate(registry);

    if (!violations.empty()) {
        LOG_CRITICAL("⚠ Protocol configuration is invalid — system will not start:");
        for (auto& v : violations) {
            LOG_CRITICAL("  [{}{}] {}",
                v.severity == ProtocolViolation::Severity::Error ? "E" : "W",
                v.regA ? fmt::format(" @{}:{}", v.regA->address, v.regA->description) : "",
                v.description);
        }
        throw std::runtime_error("ProtocolConfigurationException");
    }

    LOG_INFO("✅ Protocol configuration validated successfully");

    // L2e: 继续初始化 PlcRegisterMap / BatchWritePlan / ModbusTcpDriver
}
```

---

## 7. 已有基础设施层的现状总结

### 7.1 ISystemDriver — 统一抽象接口 ✅

```cpp
class ISystemDriver {
public:
    virtual ~ISystemDriver() = default;
    virtual CommunicationResult send(const SystemCommand& cmd) = 0;
    virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

**不变的合约边界**：Domain 层只知道 `ISystemDriver`。无论底层是 FakePLC 还是 ModbusTcpDriver，业务逻辑完全不变。

### 7.2 FakeAxisDriver — 测试替身 ✅

FakeAxisDriver 已实现 `ISystemDriver` 接口，并通过 `SystemContext` 注入依赖。当前实现包含：

- **分组感知**：按 AxisId 将命令路由到对应的 FakePLC 实例
- **反馈轮询**：每 tick 从 FakePLC 读取位置/状态，写入对应 Axis 实体
- **基于 RegisterMetadata 的命令生成**：使用 `RegisterInfo` 的元数据生成符合协议格式的内部命令，而非硬编码字面量

### 7.3 协议编译基础设施（Protocol Bootstrap Infrastructure）✅

当前已具备的 Runtime 前置条件：

| 组件 | 文件 | 作用 |
|------|------|------|
| Metadata | `RegisterMetadata.h` | 所有枚举 + `RegisterInfo` 结构体 |
| Endian | `EndianPolicy.h` | `ByteOrder × WordOrder` 精确组合 |
| Profile | `ProtocolProfile.h` | 汇川 H5U 协议特征预设 |
| Codec | `RegisterCodec.h` | 三层编解码引擎 |
| Registry | `RegisterRegistry.h` | 集中注册 + 多维查询 |
| Address | `RegisterAddressY.h` | Y 轴完整寄存器定义 |

**当前可用而无需额外开发的协议原语**：
- `RegisterCodec::resolvePolicy()` — 自动解析每个寄存器的最终端序策略
- `RegisterCodec::encode(float, RegisterInfo, Profile)` — Driver 层编码业务值
- `RegisterCodec::decodeFloat(regs, RegisterInfo, Profile)` — Driver 层解码寄存器值
- `RegisterRegistry::findByGroup(Feedback)` — 快速获取所有 Feedback 寄存器构建批量读计划

---

## 8. 关键设计决策与权衡

### 8.1 为什么 ISystemDriver 不直接依赖 RegisterInfo

`ISystemDriver` 暴露给 Domain 层的接口是：
- `send(SystemCommand)` — 只知道业务命令
- `pollFeedback(SystemContext)` — 只知道业务实体

寄存器细节完全封装在 `ModbusTcpDriver` 内部。这确保了：
- 协议替换时 Domain 层零改动
- 单元测试可以注入 `FakeAxisDriver` 而不需要任何 Modbus 知识

### 8.2 为什么 RegisterInfo 使用 const char* 而非 std::string

所有 `RegisterInfo` 实例都是 `constexpr` 全局常量。`const char*` 指向编译期常量字符串，不产生任何运行时分配。如果用 `std::string`，每个 `RegisterInfo` 在构造时都会触发堆分配。

### 8.3 为什么 EndianPolicy 拆分为 ByteOrder + WordOrder

原始的 `enum class Endianness { BigEndian, LittleEndian, BigEndianSwap, LittleEndianSwap }` 将两个正交维度混为一谈。拆解后：
- 每个维度独立控制
- 任意组合都可表达（包括未来的新厂商）
- 数学上更精确（CDAB = BigEndian byte + LowWordFirst word）

### 8.4 为什么 Validator 必须是 Runtime 组件而非测试工具

见第 6 章详细阐述。核心论点：协议声明是一种 DSL，其合法性应该在"编译期"（Runtime 启动时）验证，而非"测试期"。

### 8.5 PlcRegisterMap 与 BatchWritePlan 的职责分离

这是 v3 对 v1 的重要修正。两者职责不同：
- **PlcRegisterMap** 回答"什么寄存器" — 业务翻译
- **BatchWritePlan** 回答"怎么发" — 物理优化

分离后各自可独立测试、独立替换。

---

## 9. 从当前状态到 v3 目标的实施路径

### 9.1 已完成 ✅

- [x] L2a Metadata 层完整实现
- [x] L2b Registry 层完整实现
- [x] L2d Codec 层三层编解码引擎
- [x] ISystemDriver 统一接口
- [x] FakeAxisDriver + FakePLC 测试基础设施
- [x] Y 轴寄存器定义示例

### 9.2 短期目标（优先级 P0）

- [ ] **L2c Validation** — `ProtocolViolation.h` + `ProtocolConstraintValidator.h/cpp` — 当前最大缺口
- [ ] **补全所有轴的寄存器定义** — X/X1/X2/Z/R/Gantry/Emergency
- [ ] **L2e Planning 基元** — `RegisterBlock.h` + `RegisterWrite.h` + `RegisterRead.h`

### 9.3 中期目标（P1）

- [ ] **L2e Planning 核心** — `BatchWritePlan.h` + `PlcRegisterMap.h`
- [ ] **L3 Transport** — `IModbusClient.h` + `FakeModbusClient.h` + `LibModbusClient.h`
- [ ] **L4 Connection** — `ConnectionManager.h`
- [ ] **L5 Feedback** — `PlcFeedbackSnapshot.h` + `AxisFeedbackDecoder.h`

### 9.4 长期目标（P2）

- [ ] **L1 Driver 集成** — `ModbusTcpDriver` 替代 `FakeAxisDriver`
- [ ] **对真实 PLC 联调** — 单轴 → 多轴 → 龙门
- [ ] **性能优化** — 批量读写、帧合并、连接池
- [ ] **多协议支持** — 抽象 `IProtocolDriver` 接口

---

## A. 附录：与 v1/v2 的变更对照

| 章节 | v1 | v2 | v3 |
|------|----|----|-----|
| L2 定位 | Protocol Layer（扁平） | Protocol Layer | **Protocol Runtime Core（五子层）** |
| 寄存器定义 | 地址常量 | 地址常量 | **RegisterInfo 富元数据** |
| 大小端 | Endianness 枚举 | 同 v1 | **EndianPolicy 正交拆分** |
| 协议特征 | 散落 Codec | 散落 Codec | **ProtocolProfile 集中** |
| 编解码 | 单层 | 单层 | **三层引擎 (L1/L2/L3)** |
| 注册中心 | 无 | 无 | **RegisterRegistry** |
| 协议验证 | 无 | 测试层辅助 | **Runtime 正式组件 (L2c)** |
| 开发顺序 | PlcRegisterMap → Batch | 同 v1 | **BatchWritePlan → PlcRegisterMap (修正)** |
| 协议启动流程 | 无 | 无 | **Protocol Bootstrap Phase** |

---

> **v3 的核心精神**：协议层不应是"工具函数库"，而应是工程化的"协议世界模型"—有声明（Metadata）、有编译（Validation）、有运行时（Registry/Codec/Planning）。如同编译器保证程序语法正确，ProtocolConstraintValidator 保证协议世界合法。
