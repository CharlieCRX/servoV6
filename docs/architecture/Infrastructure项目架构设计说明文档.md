# Infrastructure 项目架构设计说明文档

> 基于 ProtocolRuntime v4 规划 + TDD 测试案例的完整架构剖析  
> 生成日期：2026-05-26  
> 项目：servoV6 工控上位机

---

## 目录

1. [设计目标：infrastructure 到底想做什么](#1-设计目标infrastructure-到底想做什么)
2. [架构分层总览](#2-架构分层总览)
3. [核心文件逐文件详解](#3-核心文件逐文件详解)
4. [数据流转全链路模拟](#4-数据流转全链路模拟)
5. [函数处理链接过程](#5-函数处理链接过程)
6. [TDD 测试验证体系](#6-tdd-测试验证体系)
7. [附录：类关系图与依赖拓扑](#7-附录类关系图与依赖拓扑)

---

## 1. 设计目标：infrastructure 到底想做什么

### 1.1 一句话定义

**infrastructure 项目实现了「ProtocolRuntime —— 从声明到运行」的完整管线：将开发者声明的 Modbus 寄存器映射表，在运行时自动、可靠地转换为业务层可直接使用的强类型数据。**

### 1.2 核心问题域

在工控上位机中，与 PLC 通讯存在以下痛点：

| 痛点 | 说明 |
|------|------|
| **物理/语义鸿沟** | PLC 只有「位 (Coil)」和「字 (HoldingReg)」两种物理形态，但业务层需要 `bool`、`int16_t`、`float` 等强类型语义 |
| **端序混乱** | 同一型号 PLC（如汇川 H5U）的 Float32 寄存器可能使用 CDAB 序（BigEndian + LowWordFirst），而外接传感器可能要求 ABCD 序，端序决议逻辑散落各处 |
| **离散地址碎片** | 寄存器地址可能是 {100, 101, 102, 200, 201}，如果每个地址单独发送一条 Modbus 指令，100 个寄存器 = 100 次网络往返 → 性能灾难 |
| **数据不一致** | FC01（读 Coil）和 FC03（读 HoldingReg）是两次独立网络调用，如果 FC01 成功但 FC03 失败，`bits` 是最新的但 `words` 是旧值 → 数据错配 |
| **可测试性** | Modbus 通讯涉及 TCP socket 和真实硬件，无法在 CI 环境中测试编解码逻辑的正确性 |

infrastructure 通过分层抽象彻底解决了上述问题。

### 1.3 设计原则

1. **声明式寄存器映射**：开发者只需描述"我要读 D124-125 作为 Float32，CDAB 序"，无需关心地址打包、协议帧构造
2. **纯数据变换**：PlcPoller 和 RegisterCodec 不持有 socket 句柄，不执行 I/O，只做地址→请求、响应→快照的纯数据变换，100% 可单元测试
3. **可信度标记**：PlcSnapshot 通过 `complete` 标志位携带"本轮采集是否完全成功"的元信息，业务层据此决定是否跳过不可信数据
4. **端序策略三态决议**：寄存器级 override > Profile 全局默认 > 运行时拒绝，消除端序硬编码

---

## 2. 架构分层总览

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Business Layer (领域层)                          │
│   Axis::applyFeedback(PlcValue)                                     │
│   EmergencyStopController::applyFeedback(PlcValue)                  │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ PlcValue (std::variant)
┌──────────────────────────────┴──────────────────────────────────────┐
│  L3: RegisterCodec  (编解码引擎 — 静态工具类)                         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Level 1: 单字编解码   decodeBool / decodeUint16               │   │
│  │ Level 2: 端序核心引擎  decodeInt32 / decodeFloat (EndianPolicy) │   │
│  │ Level 3: 业务门面层    decode(snapshot) / encode(PlcValue)    │   │
│  └──────────────────────────────────────────────────────────────┘   │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ PlcSnapshot (bits + words + complete + ts)
┌──────────────────────────────┴──────────────────────────────────────┐
│  L2: Snapshot 层 (一次采集的不变数据包)                                │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │ RawBitSnapshot   │  │ RawWordSnapshot  │  │ PlcSnapshot      │   │
│  │ getBit(addr)     │  │ getWords(addr,n) │  │ bits + words     │   │
│  │ → optional<bool> │  │ → span<uint16_t> │  │ complete + ts    │   │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘   │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ assemble()
┌──────────────────────────────┴──────────────────────────────────────┐
│  L1: PlcPoller (PLC 现场照片采集器)                                    │
│  ┌──────────────────┐  ┌──────────────────────────────────────┐     │
│  │ AddressPacker    │  │ PlcPoller                            │     │
│  │ pack(addrs, gap) │  │ prepare() → PollRequest              │     │
│  │ → vector<Range>  │  │ assemble(responses, ts) → PlcSnapshot│     │
│  └──────────────────┘  │ trust() / untrusted()                │     │
│                        └──────────────────────────────────────┘     │
└──────────────────────────────┬──────────────────────────────────────┘
                               │ PollRequest
┌──────────────────────────────┴──────────────────────────────────────┐
│  L0: 声明层 (编译期配置)                                               │
│  ┌──────────────────┐  ┌──────────────────┐  ┌──────────────────┐   │
│  │ RegisterInfo     │  │ RegisterRegistry │  │ ProtocolProfile  │   │
│  │ area/type/access │  │ add() / addAll() │  │ name / default   │   │
│  │ behavior/group   │  │ findByGroup()    │  │ Endian / maxRead │   │
│  │ endianOverride   │  │ findByAddress()  │  │ coilUsesFF00     │   │
│  └──────────────────┘  └──────────────────┘  └──────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 层的隔离原则

- **L0 → L1**：L1 从 RegisterRegistry **读取** 地址列表，但不修改、不依赖具体寄存器语义
- **L1 → L2**：L1 用 `assemble()` **产出** PlcSnapshot，Snapshot 是不可变的纯数据
- **L2 → L3**：L3 从 PlcSnapshot **提取** bits/words 并解码，Codec 无状态
- **L3 → Business**：L3 产出 `PlcValue`，业务层通过 `isBool()/isFloat()` 安全消费

---

## 3. 核心文件逐文件详解

### 3.1 文件清单

```
infrastructure/
├── ISystemDriver.h                         # 通讯接口抽象
├── FakeAxisDriver.h                        # 轴驱动测试替身
├── FakePLC.h                               # PLC 测试替身
├── plc/
│   └── protocol/
│       ├── EndianPolicy.h                  # 端序策略（ByteOrder + WordOrder）
│       ├── ProtocolProfile.h               # 设备 Profile 配置
│       ├── RegisterMetadata.h              # 寄存器元数据（字段/枚举）
│       ├── RegisterRegistry.h              # 寄存器注册表（声明容器）
│       ├── MemorySnapshot.h                # 原始内存快照（RawBit/RawWord）
│       ├── PlcSnapshot.h                   # 一次完整 PLC 现场照片
│       ├── PlcValue.h                      # 多态数据载体（std::variant）
│       ├── RegisterCodec.h                 # 编解码引擎（纯静态工具类）
│       ├── PlcPoller.h                     # PLC 现场照片采集器
│       └── PlcPoller.cpp                   # PlcPoller 实现
├── logger/
│   ├── Logger.h                            # 日志接口
│   ├── LogContext.h                        # 日志上下文
│   └── TraceScope.h                        # 追踪作用域
└── utils/
    └── CommandFormatter.h                  # 命令格式化工具
```

### 3.2 逐文件分析

---

#### 3.2.1 `EndianPolicy.h` — 端序策略定义

**作用**：定义字节序 (ByteOrder) 和字序 (WordOrder) 的组合策略。

```cpp
enum class ByteOrder { BigEndian, LittleEndian };
enum class WordOrder { HighWordFirst, LowWordFirst };
struct EndianPolicy { ByteOrder byteOrder; WordOrder wordOrder; };
```

**为什么需要它**：

以 Float32 编码为例，32 位 IEEE 754 值 `3F 80 00 00` (= 1.0f) 在 Modbus 中存在 4 种可能的寄存器布局：

| 策略 | 寄存器[0] | 寄存器[1] | 名称 |
|------|-----------|-----------|------|
| BigEndian + HighWordFirst | 0x3F80 | 0x0000 | 标准 ABCD |
| BigEndian + LowWordFirst  | 0x0000 | 0x3F80 | 汇川 CDAB |
| LittleEndian + HighWordFirst | 0x803F | 0x0000 | 少见 DCBA |
| LittleEndian + LowWordFirst  | 0x0000 | 0x803F | 少见 BADC |

**调用关系**：
- `RegisterInfo::endianOverride` 可以覆盖单个寄存器的端序
- `ProtocolProfile::defaultEndian` 提供设备默认端序
- `RegisterCodec::resolvePolicy()` 按优先级决议：override > profile

---

#### 3.2.2 `ProtocolProfile.h` — 设备配置

**作用**：描述一个 PLC 型号的全局通讯参数。

```cpp
struct ProtocolProfile {
  std::string_view name;          // 如 "Inovance_H5U&Easy"
  EndianPolicy defaultEndian;     // 默认端序
  uint16_t maxReadRegisters;      // 单次最多读字数量
  uint16_t maxWriteRegisters;     // 单次最多写字数量
  bool coilUsesFF00;              // 线圈写是否用 0xFF00/0x0000
  bool supportsMixedEndian;       // 是否支持混合端序
};
```

**预定义 Profile**：

```cpp
constexpr ProtocolProfile INOVANCE_PROFILE {
  "Inovance_H5U&Easy",
  { ByteOrder::BigEndian, WordOrder::LowWordFirst },  // CDAB!
  120, 120, true, false
};
```

**为什么需要 `coilUsesFF00`**：

Modbus FC05 (写单线圈) 规范规定 ON = 0xFF00、OFF = 0x0000，但部分 PLC 允许 ON = 0x0001。通过此标志控制编码行为。

---

#### 3.2.3 `RegisterMetadata.h` — 寄存器元数据

**作用**：单个寄存器的完整声明信息，是整个系统的「最小配置单元」。

```cpp
struct RegisterInfo {
  RegisterArea area;              // Coil / HoldingReg
  uint16_t address;               // 物理地址
  RegisterType type;              // Bool / Int16 / Float32 / String
  RegisterAccess access;          // ReadOnly / WriteOnly / ReadWrite
  RegisterBehavior behavior;      // Level / EdgeTrigger / Latch / Continuous
  RegisterGroup group;            // Command / Feedback / Parameter / Alarm
  std::string unit;               // 物理单位（mm, V, rpm）
  std::string description;        // 人类可读描述
  uint16_t pulseWidth;            // 边沿触发脉冲宽度（ms），0 = 非边沿
  std::optional<EndianPolicy> endianOverride;  // 寄存器级端序覆盖
};
```

**核心枚举说明**：

| 枚举 | 值 | 说明 |
|------|-----|------|
| `RegisterArea` | `Coil`, `HoldingReg` | Modbus 地址空间类型 |
| `RegisterType` | `Bool`, `Int16`, `Float32`, `String` | 解码后的目标类型 |
| `RegisterAccess` | `ReadOnly`, `WriteOnly`, `ReadWrite` | 读写权限 |
| `RegisterBehavior` | `Level`, `ManualResetEdgeTrigger`, `AutoResetEdgeTrigger`, `Continuous`, `Latch` | 触发/更新语义 |
| `RegisterGroup` | `Command`, `Feedback`, `Parameter`, `Alarm` | 分组决定 Poll 策略 |

**行为语义详解**：

- **Level**：持续保持的电平信号（如速度设定值 1000 rpm）
- **ManualResetEdgeTrigger**：软件需控制 ON→延时→OFF（如触发急停）
- **AutoResetEdgeTrigger**：PLC 自动复位（软件只需发 ON）
- **Continuous**：持续更新的反馈（如实时位置、速度）
- **Latch**：锁存状态（显式 Reset 才能清除，如报警）

---

#### 3.2.4 `RegisterRegistry.h` — 寄存器注册表

**作用**：`RegisterInfo` 的集合容器，提供多种查询接口。**这是「声明」的终点，也是「运行」的起点。**

```cpp
class RegisterRegistry {
public:
  void add(const RegisterInfo& reg);
  void addAll(std::span<const RegisterInfo> regs);
  void addAll(std::initializer_list<RegisterInfo> regs); // 支持 {A, B, C}

  const std::vector<RegisterInfo>& all() const;
  std::vector<RegisterInfo> findByGroup(RegisterGroup group) const;
  std::vector<RegisterInfo> findByArea(RegisterArea area) const;
  const RegisterInfo* findByAddress(RegisterArea area, uint16_t address) const;
private:
  std::vector<RegisterInfo> m_registers;
};
```

**使用模式（声明式）**：

```cpp
RegisterRegistry registry;
registry.addAll({
  {RegisterArea::Coil, 101, RegisterType::Bool, ..., "Move Done"},
  {RegisterArea::HoldingReg, 124, RegisterType::Float32, ..., "Position", 0,
   EndianPolicy{ByteOrder::BigEndian, WordOrder::LowWordFirst}},
  {RegisterArea::HoldingReg, 130, RegisterType::Int16, ..., "Speed"},
});
```

这就是「从声明开始」——开发者声明了 3 个寄存器，之后全部由 pipeline 自动处理。

---

#### 3.2.5 `MemorySnapshot.h` — 原始内存快照

**作用**：将 Modbus 原始响应字节封装为带地址范围和边界检查的可查询对象。

**`RawBitSnapshot`**：封装 Coil (FC01) 响应

```cpp
class RawBitSnapshot {
  uint16_t m_startAddress;
  uint16_t m_bitCount;
  std::vector<uint8_t> m_payload;

  std::optional<bool> getBit(uint16_t address) const {
    // 1. 边界检查
    if (address < m_startAddress || address >= m_startAddress + m_bitCount)
      return std::nullopt;
    // 2. 位索引计算
    uint16_t offset = address - m_startAddress;
    return (m_payload[offset / 8] & (1 << (offset % 8))) != 0;
  }
};
```

**`RawWordSnapshot`**：封装 HoldingReg (FC03) 响应

```cpp
class RawWordSnapshot {
  uint16_t m_startAddress;
  std::vector<uint16_t> m_payload;

  std::optional<std::span<const uint16_t>> getWords(uint16_t address, uint16_t count) const {
    if (address < m_startAddress || address + count > m_startAddress + m_payload.size())
      return std::nullopt;
    return std::span<const uint16_t>(m_payload.data() + (address - m_startAddress), count);
  }
};
```

**设计亮点**：
- 返回 `std::optional` 替代异常：边界越界 → `nullopt`，无异常开销
- `std::span` 零拷贝视图：Float32 解码需要连续 2 个字，直接 span 避免拷贝
- 默认构造 = 空快照（无数据），天然支持"部分读取"场景

---

#### 3.2.6 `PlcSnapshot.h` — 一次完整的 PLC 现场照片

**作用**：将 `RawBitSnapshot` 和 `RawWordSnapshot` 绑定为同一时刻的采集产物，并通过 `complete` 标志表达数据可信度。

```cpp
struct PlcSnapshot {
  RawBitSnapshot  bits;     // Coil 快照
  RawWordSnapshot words;    // HoldingReg 快照
  bool    complete;         // 本轮所有 FC 调用全部成功
  uint64_t timestamp;       // 采集时间戳 (ms)

  bool isTrusted() const { return complete; }
};
```

**核心概念：可信度传递**

```
FC01 成功 + FC03 成功 → complete=true  → isTrusted()=true  → 业务层消费数据
FC01 成功 + FC03 失败 → complete=false → isTrusted()=false → 业务层跳过，保留上次已知值
```

这是防止"bits 最新但 words 是旧值"导致数据不一致的关键机制。

---

#### 3.2.7 `PlcValue.h` — 多态数据载体

**作用**：`std::variant<bool, int16_t, float, std::string>` 的类型别名 + 安全的类型判别/提取辅助函数。

```cpp
using PlcValue = std::variant<bool, int16_t, float, std::string>;

// 类型提取
template<typename T> T getValue(const PlcValue& value) { return std::get<T>(value); }

// 类型判别
inline bool isBool(const PlcValue& v)   { return std::holds_alternative<bool>(v); }
inline bool isInt16(const PlcValue& v)  { return std::holds_alternative<int16_t>(v); }
inline bool isFloat(const PlcValue& v)  { return std::holds_alternative<float>(v); }
inline bool isString(const PlcValue& v) { return std::holds_alternative<std::string>(v); }
```

**为什么用 `std::variant` 而非基类多态**：
1. **值语义**：可以拷贝、move，不需要 `shared_ptr`
2. **编译期穷举**：switch 不覆盖所有类型时编译器警告
3. **零开销**：无虚函数表、无堆分配
4. **可测试**：`PlcValue v = 150.0f` 直接构造

**典型消费模式**：

```cpp
PlcValue result = RegisterCodec::decode(REG_ABS_POSITION, snapshot, profile);
if (isFloat(result)) {
  float position = getValue<float>(result);
  axis->updatePosition(position);
}
```

---

#### 3.2.8 `RegisterCodec.h` — 编解码引擎

**作用**：整个系统的核心转换站，三层门面设计。

```
Level 1: 单字编解码（无端序）
  encodeBool → vector<uint16_t>
  decodeBool → bool
  encodeUint16 / decodeUint16

Level 2: 端序核心引擎（由 EndianPolicy 驱动）
  encodeInt32 / decodeInt32
  encodeFloat / decodeFloat

Level 3: 业务门面层（动态策略决议 + PlcValue 管线）
  resolvePolicy(RegisterInfo, Profile) → EndianPolicy
  decode(RegisterInfo, PlcSnapshot, Profile) → PlcValue    ← 最常用!
  decode(RegisterInfo, RawBit*, RawWord*, Profile) → PlcValue
  encode(PlcValue, RegisterInfo, Profile) → vector<uint16_t>
```

**端序决议逻辑**：

```
resolvePolicy(reg, profile):
  if reg.endianOverride.has_value()
    → return reg.endianOverride.value()  // 寄存器级覆盖优先
  else
    → return profile.defaultEndian       // 设备默认
```

**decode 主流程（以 Float32 为例）**：

```
1. 参数校验：
   if reg.area == HoldingReg && words == nullptr → throw
2. 端序决议：
   policy = resolvePolicy(reg, profile)
3. 从快照读取原始字：
   span = words.getWords(reg.address, 2);  // Float32 占 2 个字
4. 类型分发：
   switch (reg.type) {
     case Float32:
       vector<uint16_t> tmp(span.begin(), span.end());
       float value = decodeFloat(tmp, policy);  // L2 引擎
       return PlcValue{value};
   }
```

**encode 主流程（门面层重载）**：

```
1. 端序决议：policy = resolvePolicy(reg, profile);
2. 类型分发：
   if isBool(value):
     // 区分 Coil vs HoldingReg 编码
     if reg.area == Coil && profile.coilUsesFF00:
       return { b ? 0xFF00 : 0x0000 }
     else:
       return encodeBool(b)
   if isInt16(value):
     return encodeUint16(...)
   if isFloat(value):
     return encodeFloat(...)
   throw "unsupported"
```

---

#### 3.2.9 `PlcPoller.h` / `PlcPoller.cpp` — PLC 现场照片采集器

**作用**：将 RegisterRegistry 的离散地址列表转换为 Modbus 请求包，并将网络响应拼装为 PlcSnapshot。

**类职责分解**：

```
AddressPacker (静态工具)
  pack(addresses, maxGap) → vector<AddressRange>
  例：{100,101,102,200} → [Range(100,3), Range(200,1)]

PlcPoller (构造即分析)
  构造时：
    1. 从 registry 提取所有 Coil 地址 → 排序 → 打包 m_coilRanges
    2. 从 registry 提取所有 HoldingReg 地址（Float32 展开占 2 字）→ 排序 → 去重 → 打包 m_wordRanges
  prepare():
    → PollRequest { coilRequests[], wordRequests[] }
  assemble(coilResponses, wordResponses, timestamp):
    → PlcSnapshot (合并所有响应区间的数据)
  trust():
    → 快速构造受信快照（真实 PLC 成功场景）
  untrusted():
    → 构造不受信快照（网络失败兜底）
```

**地址打包算法**：

```
输入: [100, 101, 102, 200, 201]
maxGap = 0 (不允许空隔)

迭代:
  100: 开始新区间 Range(100, 1)
  101: gap = 0 → 合并，Range(100, 2)
  102: gap = 0 → 合并，Range(100, 3)
  200: gap = 97 > 0 → 提交 Range(100, 3)，新开 Range(200, 1)
  201: gap = 0 → 合并，Range(200, 2)

输出: [Range(100, 3), Range(200, 2)]
→ 2 条 Modbus 指令代替 5 条，节省 60% 网络往返
```

**assemble 合并逻辑**：

```
输入：
  m_coilRanges = [Range(100, 3), Range(200, 2)]
  coilResponses[0] = [0x07]     // 100-102: bits [1,1,1]
  coilResponses[1] = [0x02]     // 200-201: bits [0,1]
  firstStartAddr = 100
  lastEndAddr = 202
  totalBits = 102

输出 mergedPayload:
  合并两个区间的位数据到一个连续的 byte vector
  每个字节的每个位按全局 bit 偏移量写入
```

---

#### 3.2.10 `ISystemDriver.h` — 通讯接口抽象

**作用**：定义 Command / Feedback 双通路的抽象接口，隔离业务层与具体的 Modbus TCP 实现。

```cpp
class ISystemDriver {
public:
  // 命令通路：发送命令给 PLC，返回通讯结果（只表达"帧是否送达"）
  virtual CommunicationResult send(const SystemCommand& cmd) = 0;

  // 反馈通路：拉取 PLC 状态并分发给领域实体
  virtual void pollFeedback(SystemContext& ctx) = 0;
};
```

**`CommunicationResult` 错误分层**：

| 层级 | 状态 | 可重试 | 说明 |
|------|------|--------|------|
| L0 会话 | `Disconnected` | 否 | 未连接 |
| L1 网络 | `NetworkError` | 否 | TCP 断连/拒绝 |
| L2 传输 | `Timeout` | **是** | 超时 |
| L3 Modbus | `ProtocolError` | 否 | 异常响应 |
| L3 Modbus | `Busy` | **是** | PLC 忙 |
| L4 数据 | `InvalidResponse` | 否 | 数据格式非法 |

**设计精髓**：`send()` 只表达通讯结果，不表达 PLC 执行结果。执行结果（如是否到达目标位置）由 `pollFeedback()` 通过持续读取寄存器来判断。这彻底解耦了"发送"和"确认"。

---

#### 3.2.11 其他辅助文件

| 文件 | 作用 |
|------|------|
| `FakeAxisDriver.h` | 轴驱动的内存实现，用于 TDD 测试 |
| `FakePLC.h` | PLC 的测试替身，可注入 mock 状态 |
| `CommandFormatter.h` | 将领域命令格式化为 PlcValue，桥接 domain → 编码管线 |
| `Logger.h` / `LogContext.h` / `TraceScope.h` | 结构化日志系统，支持 RAII 追踪作用域 |
| `ProtocolConstraintValidator.h` | 编译期 / 启动期验证：地址重叠、Coil 只能 Bool、Feedback 必须是 ReadOnly |

---

## 4. 数据流转全链路模拟

### 4.1 完整流程（声明 → 反馈 → 业务消费）

```
╔═══════════════════════════════════════════════════════════════════╗
║                    Phase 1: 编译期 — 声明                          ║
╚═══════════════════════════════════════════════════════════════════╝

RegisterRegistry registry;
registry.addAll({
  {Coil, 101, Bool, ReadOnly, Continuous, Feedback, "", "MoveDone", 0, nullopt},
  {Coil, 102, Bool, ReadOnly, Continuous, Feedback, "", "HasAlarm", 0, nullopt},
  {HoldingReg, 100, Int16, ReadOnly, Continuous, Feedback, "", "StateCode", 0, nullopt},
  {HoldingReg, 124, Float32, ReadOnly, Continuous, Feedback, "mm", "AbsPosition", 0, nullopt},
  {HoldingReg, 50, Float32, ReadWrite, Level, Command, "mm", "TargetPos", 0, nullopt},
});

ProtocolProfile profile = INOVANCE_PROFILE; // CDAB 序

╔═══════════════════════════════════════════════════════════════════╗
║               Phase 2: 初始化 — 地址打包                           ║
╚═══════════════════════════════════════════════════════════════════╝

PlcPoller poller(registry);
// 构造时自动：
//   Coil 地址 {101, 102} → [Range(101, 2)]
//   HoldingReg 地址 {100, 50, 51, 124, 125} → 排序去重 → [Range(50, 2), Range(100, 1), Range(124, 2)]

PollRequest req = poller.prepare();
// req.coilRequests  = [{start:101, count:2}]
// req.wordRequests  = [{start:50, count:2}, {start:100, count:1}, {start:124, count:2}]
// → 4 条 Modbus 指令替代 7 次单独读取

╔═══════════════════════════════════════════════════════════════════╗
║            Phase 3: 运行时 — 网络 I/O (Driver 负责)                ║
╚═══════════════════════════════════════════════════════════════════╝

// Driver 执行：
//   FC01(101, 2) → response: [0x03]  // 101=true, 102=true
//   FC03(50, 2)  → response: [0x0000, 0x4316]  // TargetPos = 150.0mm (CDAB)
//   FC03(100, 1) → response: [0x0003]           // StateCode = 3
//   FC03(124, 2)  → response: [0x0000, 0x4316]  // AbsPosition = 150.0mm

std::vector<std::vector<uint8_t>>  coilResponses = {{0x03}};
std::vector<std::vector<uint16_t>> wordResponses = {
  {0x0000, 0x4316},
  {0x0003},
  {0x0000, 0x4316}
};

╔═══════════════════════════════════════════════════════════════════╗
║            Phase 4: 快照拼装                                       ║
╚═══════════════════════════════════════════════════════════════════╝

uint64_t now = getCurrentMilliseconds();
PlcSnapshot snapshot = poller.assemble(coilResponses, wordResponses, now);

// snapshot.bits:
//   startAddress=101, bitCount=2, payload=[0x03]
//   → getBit(101) = true, getBit(102) = true
//
// snapshot.words:
//   startAddress=50, payload 包含 50→127 所有字
//     offset 0  (addr 50):  0x0000  ← TargetPos 低字
//     offset 1  (addr 51):  0x4316  ← TargetPos 高字
//     offset 50 (addr 100): 0x0003  ← StateCode
//     offset 74 (addr 124): 0x0000  ← AbsPosition 低字
//     offset 75 (addr 125): 0x4316  ← AbsPosition 高字
//
// snapshot.complete = true  (4 条指令全部成功)

╔═══════════════════════════════════════════════════════════════════╗
║            Phase 5: 解码 — RegisterCodec                         ║
╚═══════════════════════════════════════════════════════════════════╝

PlcValue moveDone = RegisterCodec::decode(REG_MOVE_DONE, snapshot, profile);
// → bits.getBit(101) → true → PlcValue{true}

PlcValue stateCode = RegisterCodec::decode(REG_STATE, snapshot, profile);
// → words.getWords(100, 1) → span=[0x0003] → PlcValue{(int16_t)3}

PlcValue absPos = RegisterCodec::decode(REG_ABS_POSITION, snapshot, profile);
// → resolvePolicy: → profile.defaultEndian = {BigEndian, LowWordFirst}
// → words.getWords(124, 2) → span=[0x0000, 0x4316]
// → decodeFloat([0x0000, 0x4316], {BE, LWF}):
//     WordOrder=LowWordFirst → highWord=0x4316, lowWord=0x0000
//     ByteOrder=BigEndian → 0x43160000 = 150.0f ✓
// → PlcValue{150.0f}

╔═══════════════════════════════════════════════════════════════════╗
║            Phase 6: 业务消费                                       ║
╚═══════════════════════════════════════════════════════════════════╝

if (snapshot.isTrusted()) {
  if (isFloat(absPos)) {
    axis->updatePosition(getValue<float>(absPos));  // → 150.0mm
  }
  if (isBool(moveDone)) {
    if (getValue<bool>(moveDone)) {
      axis->onMoveComplete();
    }
  }
}
```

### 4.2 发送管线（下行）

```
用户点击 JOG+ 按钮
  → JogOrchestrator: 生成 SystemCommand { axisId=X, type=JOG, ... }
  → ISystemDriver::send(cmd)
    → CommandFormatter: 将 cmd 翻译为 {Register{Coil, 42, Bool}, PlcValue{true}}
    → RegisterCodec::encode(PlcValue{true}, REG_JOG_TRIGGER, profile)
      → isBool + area=Coil + coilUsesFF00=true → return {0xFF00}
    → ModbusClient::writeCoil(42, 0xFF00)
    → CommunicationResult { Sent }
```

### 4.3 不可信快照场景（FC01 成功但 FC03 失败）

```
假设 FC03(124,2) 超时，只有 2 个 word 响应回来

PlcPoller::assemble():
  wordResponses.size()=2 != m_wordRanges.size()=3
  → complete = false
  → words 保持空 RawWordSnapshot

业务层:
  snapshot.isTrusted() → false
  → 跳过本轮所有位置/速度更新
  → 保留上一帧的已知值
```

---

## 5. 函数处理链接过程

### 5.1 完整调用链路图

```
main

---

## 5. 函数处理链接过程

### 5.1 调用链路简图（缩进表示调用层级）

    main()
      +-- tickLoop()
            +-- ISystemDriver::pollFeedback(ctx)
            |     +-- PlcPoller::prepare()
            |     |     +-- RegisterRegistry::findByArea(Coil)       -> coilAddresses
            |     |     +-- AddressPacker::pack(coilAddresses, 0)    -> coilRanges
            |     |     +-- RegisterRegistry::findByArea(HoldingReg) -> wordAddresses
            |     |     +-- AddressPacker::pack(wordAddresses, 0)    -> wordRanges
            |     +-- [Modbus IO: 逐 range 发送 FC01 / FC03，收集响应]
            |     +-- PlcPoller::assemble(coilResponses, wordResponses, ts)
            |     |     +-- 合并 Coil 区间 -> RawBitSnapshot
            |     |     +-- 合并 Word 区间 -> RawWordSnapshot
            |     |     +-- 检查响应数量匹配性 -> complete flag
            |     |     --> PlcSnapshot { bits, words, complete, ts }
            |     +-- for each axis:
            |           +-- RegisterCodec::decode(REG_POSITION, snapshot, profile)
            |           |     +-- resolvePolicy(reg, profile) -> EndianPolicy
            |           |     +-- words.getWords(addr, 2) -> span<uint16_t>
            |           |     +-- decodeFloat(span, policy) -> PlcValue{float}
            |           +-- Axis::updatePosition(getValue<float>(result))
            +-- ISystemDriver::send(cmd)  (独立通路，不在 pollFeedback 内部)
                  +-- CommandFormatter::format(cmd) -> vector<pair<RegisterInfo, PlcValue>>
                  +-- RegisterCodec::encode(PlcValue, reg, profile) -> vector<uint16_t>
                  +-- [Modbus IO: writeCoil / writeRegisters]

### 5.2 关键函数输入输出表

| 函数 | 输入 | 输出 | 调用方 |
|------|------|------|--------|
| RegisterRegistry::addAll | initializer_list | void | main / 初始化 |
| PlcPoller::PlcPoller | const RegisterRegistry& | (构造) | SystemDriver ctor |
| PlcPoller::prepare | (内部 ranges) | PollRequest | pollFeedback |
| PlcPoller::assemble | 2 vectors + uint64_t | PlcSnapshot | pollFeedback |
| PlcPoller::trust | bits, words, ts | PlcSnapshot (complete=true) | 测试 / FakePLC |
| PlcPoller::untrusted | ts | PlcSnapshot (complete=false) | 网络失败兜底 |
| RegisterCodec::resolvePolicy | RegisterInfo, ProtocolProfile | EndianPolicy | decode / encode |
| RegisterCodec::decode | RegisterInfo + Snapshot + Profile | PlcValue | Axis / Safety |
| RegisterCodec::encode | PlcValue + RegisterInfo + Profile | vector<uint16_t> | ISystemDriver::send |
| RawBitSnapshot::getBit | uint16_t address | optional<bool> | RegisterCodec::decode |
| RawWordSnapshot::getWords | address + count | optional<span<uint16_t>> | RegisterCodec::decode |

---

## 6. TDD 测试验证体系

### 6.1 测试金字塔

    /\          test_system_integration  (FakePLC + FakeAxisDriver)
   /  \         联合测试
  /    \
 /------\       test_register_codec_snapshot  (编解码 / 回环)
/        \      test_plc_snapshot  (拷贝 / 可信度 / 边界)
/----------\    test_plc_poller  (地址打包 / 区间合并 / 快照拼装)
/            \  test_plc_value  (variant 构造 / 提取)
/--------------\

### 6.2 各测试组覆盖要点

**test_plc_value**
- 四种类型的默认构造与赋值
- isBool / isInt16 / isFloat / isString 类型判别
- getValue<T> 提取正确性
- bad_variant_access 边界情况

**test_register_codec_snapshot（核心）**
- decodesBoolFromCoil：RawBitSnapshot -> bool
- decodesInt16FromHoldingReg：RawWordSnapshot -> int16_t
- decodesFloat32WithProfileEndian：INOVANCE_PROFILE (CDAB) 解码
- decodesFloat32WithRegisterOverride：寄存器级端序覆盖 > profile
- encodesBoolToCoilFF00：Coil 写 FF00/0000 编码
- encodesFloat32CDAB：Float32 编码为 CDAB 寄存器对
- roundTripFloat32：编码后立即解码 -> 回环一致性
- throwsOnNullWordForHoldingReg：words==nullptr 应抛异常
- throwsOnMissingSnapshotOnDecode：getWords 返回 nullopt 应抛异常

**test_plc_snapshot**
- constructorCompleteness：complete 标志位传递
- isTrustedEqualsComplete：isTrusted() === complete
- timestampPersistence：时间戳不变
- dataCopyIntegrity：数据深拷贝
- accessOutOfRange：越界访问返回 nullopt

**test_plc_poller**
- prepareGeneratesCorrectRanges：验证地址打包算法
- assembleMergesCorrectly：多区间合并到统一 Snapshot
- partialFailureScenarios：部分响应失败时 complete=false
- trustAndUntrustedFactories：工厂方法正确性

---

## 7. 附录：类关系图与依赖拓扑

### 7.1 核心依赖方向

    ProtocolProfile (设备默认配置)
         |
    RegisterInfo ----+-----> RegisterRegistry (声明容器)
         |                    |
         |              PlcPoller (地址打包 + 快照拼装)
         |                    |
         |              PollRequest
         |                    |
         |              PlcSnapshot
         |                    |
         +---- EndianPolicy --+--> RegisterCodec (静态编解码引擎)
         |                         |
         |                    PlcValue (variant 多态载体)
         |                         |
         +---- ISystemDriver <-----+---> 业务层 (Axis / Safety)

### 7.2 编译依赖矩阵（CMake target 层面）

| 模块 | 依赖 |
|------|------|
| EndianPolicy | (无) |
| ProtocolProfile | EndianPolicy |
| RegisterMetadata | EndianPolicy |
| RegisterRegistry | RegisterMetadata |
| MemorySnapshot | (无) |
| PlcSnapshot | MemorySnapshot |
| PlcValue | (无) |
| RegisterCodec | RegisterMetadata, MemorySnapshot, PlcValue, ProtocolProfile |
| PlcPoller | RegisterRegistry, PlcSnapshot |
| ISystemDriver | SystemCommand, CommunicationResult |
| FakePLC | ISystemDriver, PlcPoller, RegisterCodec |

### 7.3 扩展指南

**添加新数据类型（如 Double64）**：
1. 在 RegisterType 枚举添加 Double64
2. 在 PlcValue variant 添加 double
3. 在 RegisterCodec::decode 添加 Double64 case
4. 在 RegisterCodec Level 2 添加 encodeDouble / decodeDouble
5. PlcPoller 构造时正确处理 Double64 地址展开（占 4 字）
6. TDD 添加回环测试

**添加新 PLC Profile**：
1. 在 ProtocolProfile.h 添加新 constexpr profile
2. 确认 defaultEndian / coilUsesFF00 等参数
3. 无需修改编解码代码——端序策略由 profile 自动驱动

**切换驱动实现**：
- ISystemDriver 是纯抽象接口
- 生产环境：ModbusTcpDriver : ISystemDriver
- 测试环境：FakePLC : ISystemDriver
- 切换只需改变 main.cpp 中 driver 的构造参数

---

> 本文档与 ProtocolRuntime v4 规划文档互为补充：
> - v4 文档从「设计思想」出发，阐述管线各阶段的动机与决策
> - 本文档从「代码实现」入手，逐一剖析每个文件的职责与数据流转
> - 两者共同构成 infrastructure protocol runtime 的完整知识体系
