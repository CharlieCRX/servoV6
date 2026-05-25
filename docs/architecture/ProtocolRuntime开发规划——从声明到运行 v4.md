# ProtocolRuntime 开发规划 —— 从声明到运行 v4

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-25 | 初版：基于当前实现，制定 L2c~L2e TDD 开发路线图 |
| v2.0 | 2026-05-25 | 修正：类型安全化、RegisterBlock 命名精确化、五维度架构评审 |
| v3.0 | 2026-05-25 | 架构升级：引入物理/逻辑分离。PLC 只有 bit/word，float 是软件解释。确立 Snapshot → Codec → PlcValue 三层管线 |
| **v4.0** | **2026-05-25** | **引入 PlcSnapshot：将 RawBitSnapshot 与 RawWordSnapshot 绑定为"同一时刻的完整PLC现场照片"，解决多轮次网络读取导致的数据不一致问题** |

---

## 〇、核心哲学：PLC 不认 Float

### 0.1 必须刻在脑海里的真相

PLC 内部没有 `float`。PLC 只有两种物理实体：

```
┌──────────────────────────────┐
│      PLC 世界                 │
│  bit   ← FC01/FC02/FC05/FC0F │
│  word  ← FC03/FC04/FC06/FC10 │
│  没有 float/int32/position   │
└──────────────────────────────┘
```

你在业务代码里看到的 `150.5f`、`true`、`alarm`——全部是你的软件**解释**出来的。

### 0.2 五个世界的映射

```
PLC世界     Raw快照世界     PlcSnapshot       Decode世界         业务世界
(物理存在)  (零拷贝快照)    (一致现场照片)     (语义翻译)          (强类型)
──────────────────────────────────────────────────────────────────────────
bit         RawBitSnapshot  ┐               RegisterCodec       bool
│           │               │               │                    │
│ Coil101=1 │ getBit(101)  │ PlcSnapshot   │ decodeBool→true    │ MOVE_DONE=true
│           │ →true         │ bits  ────────┤                    │
│           │               │ words ────┐   │                    │
word        RawWordSnapshot │           │   RegisterCodec       float
│           │               │ timestamp │   │                    │
│ D124=     │ getWords      │ complete  │   │ decodeFloat+        │ ABS_POSITION
│ 0x4316    │ (124,2)       ┘           │   │ BigEndian→150.0f    │ = 150.0f
│ D125=     │ → span[2]                 │   │                    │
│ 0x0000    │                           │   │                    │
│           │                           │   RegisterCodec       int16
│           │                           │   decodeUint16→1      │ STATE=Standby
│           │                           │                       │
│           │                           │                       │ PlcValue
│           │                           │                       │ variant<bool,
│           │                           │                       │  int16,float>
```

**关键洞察**：
- **Raw 快照层**不知道"数据类型"——只知地址和 bit/word 数组。
- **PlcSnapshot** 不知道"数据类型"——但它保证 bit 和 word 来自同一次完整采集周期。
- **Codec 层**不知道"业务含义"——只知元数据（Float32占2字，大端），执行纯数学转换。
- **PlcValue** 是翻译成果的标准载体。

### 0.3 读取链路的完整穿越

以 `float pos = plc.readFloat("Y_Axis.ABS_POSITION")` 为例：

```
Layer              Component                Action              Data Shape
──────              ─────────                ──────              ──────────
PLC                汇川H5U                  D124=0x4316,        bit/word
                                            D125=0x0000
Transport          IModbusClient            FC01→payload         vector<uint8_t>
                                            FC03→payload         vector<uint16_t>
Raw 快照           RawBitSnapshot           store(101,payload)  span<const uint8>
                   RawWordSnapshot          store(124,payload)  span<const uint16>
打包               PlcSnapshot              bind(bits,words,    一次完整现场
                                            timestamp,complete)
(业务调用)         PlcDevice               查Registry→          RegisterInfo
                                           "ABS_POSITION"
Codec              RegisterCodec            getWords(124,2)     span<2>
                                           decodeFloat→150.0f
Value              PlcValue                variant holds 150.0f std::variant
业务层             Y轴控制算法              std::get<float>      float
```

**整个过程，PLC 不知道自己在传"位置=150.0mm"，它只知道在传 2 个 word。**

---

## 一、当前状态回顾

### 已完成的基石

| 文件 | 状态 | 分层 |
|------|------|------|
| `RegisterMetadata.h` | ✅ | L2a — 全部枚举 + RegisterInfo |
| `EndianPolicy.h` | ✅ | L2a — ByteOrder × WordOrder |
| `ProtocolProfile.h` | ✅ | L2a — 汇川 H5U 预设 |
| `RegisterAddressY.h` | ✅ | L2a — Y轴寄存器完整定义 |
| `RegisterRegistry.h` | ✅ | L2b — 集中注册 + 多维查询 |
| `MemorySnapshot.h` | ✅ | 物理层 — RawBitSnapshot + RawWordSnapshot |
| `RegisterCodec.h` | ⚠️ | L2d — 需重构（老接口用裸vector） |
| `ISystemDriver.h` | ✅ | 统一接口 |
| `FakePLC.h` | ✅ | 物理引擎仿真 |
| `SystemCommand.h` | ✅ | variant 命令集合 |

### 当前间隙

> RawBitSnapshot 与 RawWordSnapshot 已就绪，但它们是**独立对象**，无法表达"同一次采集周期"。Codec 未适配 Snapshot 接口。PlcValue 未定义（v3 已补）。PlcDevice 未实现。需打通：**Raw 快照 → PlcSnapshot → Codec → PlcValue → PlcDevice**。

---

## 二、v4 架构全景图

### 2.1 v3 的问题

v3 中 `PlcDevice` 直接持有两个独立的 `unique_ptr<RawBitSnapshot>` 和 `unique_ptr<RawWordSnapshot>`。但现实世界的 Modbus 轮询是**多轮次**的：

```
T0: FC01 成功 → 读到 Coil 101~130
T1: FC03 成功 → 读到 D100~D130
```

如果 FC01 成功、FC03 超时，系统里 bit 是最新的、word 是旧的——**数据不一致**。

### 2.2 v4 分层架构

```
                  ┌─────────────────────────┐
                  │  PlcDevice (门面层)       │
                  │  readFloat/writeBool      │
                  └──────────┬──────────────┘
                             │
         ┌───────────────────┼───────────────────┐
         ▼                   ▼                    ▼
┌─────────────┐  ┌──────────────────┐  ┌──────────────────┐
│ PlcValue    │  │ RegisterCodec    │  │ RegisterRegistry │
│ variant<    │◄─│ decode/encode    │◄─│ name→RegisterInfo│
│  bool,int16,│  │ Snapshot→业务值   │  └──────────────────┘
│  float>     │  └────────┬─────────┘
└──────┬──────┘           │
       │                  │ decode(reg, snapshot, profile)
       │          ┌───────┴───────────┐
       │          │    PlcSnapshot    │  ← 一次完整的PLC现场照片
       │          │  ┌─────────────┐  │
       │          │  │RawBitSnap   │  │  bits (Coil)
       │          │  │RawWordSnap  │  │  words (HoldingReg)
       │          │  │bool complete│  │  ← 本轮所有FC成功才为true
       │          │  │uint64_t ts  │  │  ← 采集时间戳
       │          │  └─────────────┘  │
       │          └───────────────────┘
       │                   │
       │          ┌────────┴────────┐
       │          ▼                 ▼
       │  ┌────────────┐  ┌──────────────┐
       │  │RawBitSnap  │  │RawWordSnap   │
       │  │(Coil物理)  │  │(Reg物理)     │
       │  └──────┬─────┘  └──────┬───────┘
       │         │               │
       │  ┌──────┴───────────────┴──┐
       │  │  IModbusClient (传输层) │
       │  │  FC01/FC03/FC05/FC10   │
       │  └─────────────────────────┘
  ┌────┴──────────┐
  │ 业务层         │
  │ float pos=...  │
  │ bool done=...  │
  └───────────────┘
```

### 2.3 数据流向四阶段

| 阶段 | 路径 | 关键动作 |
|------|------|---------|
| 1 | PLC→Transport→Raw Snapshots | FC01/FC03拉回payload，零拷贝存储 |
| 2 | RawSnapshots→PlcSnapshot | 绑定 bit/word + 时间戳 + complete 标记 |
| 3 | PlcSnapshot→Codec→PlcValue | 划出span，结合EndianPolicy拼装 |
| 4 | 业务代码→Codec→Transport | encode成Modbus值，FC05/FC10发出 |
| 5 | PlcDevice脉冲管理 | EdgeTrigger: FC05(ON)→定时→FC05(OFF) |

---

## 三、v4 总体路线图

```
P0 ────→ P1 ────→ P2 ────→ P3 ────→ P4
 │         │         │         │         │
 ▼         ▼         ▼         ▼         ▼
PlcValue  Validator  Codec重构 PlcSnapshot PlcDevice
variant   4条规则   Snapshot→  AddressPacker 完整门面
          不变      Value      PlcPoller     read/write
```

**核心原则**：
1. 先定义 PlcValue — Codec/PlcDevice 的标准货币
2. Validator 不变 — V2 设计完全兼容 v3/v4
3. Codec 重构 — L1/L2 保留，L3 从 Snapshot 解码
4. **P3 引入 PlcSnapshot** — 将 RawBitSnapshot + RawWordSnapshot 绑定为"一次完整采集"
5. TDD 先行 — 每步先写测试

---

## 四、P0：PlcValue — 标准货币

### 4.1 为什么是 P0

PlcValue 是 Snapshot 和业务层之间的标准契约类型，所有翻译管线产出都装在这个盒子里。必须在 Codec 重构前定义。

### 4.2 PlcValue.h

**文件**：`infrastructure/plc/protocol/PlcValue.h`

```cpp
#pragma once
#include <variant>
#include <cstdint>
#include <string>

namespace plc::protocol {

using PlcValue = std::variant<
    bool,        // Coil 状态 (MOVE_DONE, ALARM...)
    int16_t,     // 16位状态字 (STATE, ALARM_CODE...)
    float,       // 物理量 (ABS_POSITION: 150.5 mm)
    std::string  // 字符串预留
>;

template<typename T>
T getValue(const PlcValue& value) { return std::get<T>(value); }

inline bool isBool(const PlcValue& v)   { return std::holds_alternative<bool>(v); }
inline bool isInt16(const PlcValue& v)  { return std::holds_alternative<int16_t>(v); }
inline bool isFloat(const PlcValue& v)  { return std::holds_alternative<float>(v); }
inline bool isString(const PlcValue& v) { return std::holds_alternative<std::string>(v); }

} // namespace plc::protocol
```

### 4.3 TDD 测试

**文件**：`tests/infrastructure/protocol/test_plc_value.cpp`

```
Test: construct_bool → getValue<bool> = true
Test: construct_float → getValue<float> ≈ 150.5f
Test: construct_int16 → getValue<int16_t> = 1
Test: isBool(true) = true, isFloat = false
Test: wrong_type → std::bad_variant_access
```

---

## 五、P1：ProtocolConstraintValidator（与 v2 一致）

| 文件 | 路径 |
|------|------|
| ProtocolViolation.h | `infrastructure/plc/protocol/validator/` |
| ProtocolConstraintValidator.h + .cpp | 同上 |
| 测试 | `tests/.../test_protocol_validator.cpp` |

### 四条规则

| ID | 检查 | 严重性 | 触发条件 |
|----|------|--------|---------|
| R01 | 地址重叠 | Error | 同一 Area 内范围重叠 |
| R02 | Coil 类型限制 | Error | area==Coil 且 type!=Bool |
| R03 | Feedback 权限 | Error | group==Feedback 且 access!=ReadOnly |
| R04 | 脉冲宽度缺失 | Error | ManualResetEdgeTrigger 且 pulseWidthMs==0 |

（详细实现见 v2 文档第四节。）

---

## 六、P2：RegisterCodec 重构

### 6.1 老接口问题

```cpp
// 老接口：调用方须自取数据、每读必拷贝、Coil/HoldingReg 混在一起
static float decodeFloat(const std::vector<uint16_t>& regs, ...);
```

### 6.2 新接口

```cpp
// 新接口：Codec 直接从 Snapshot 读取
static PlcValue decode(
    const RegisterInfo& reg,
    const RawBitSnapshot* bits,    // 可为 nullptr
    const RawWordSnapshot* words,  // 可为 nullptr
    const ProtocolProfile& profile);

// 便捷重载：直接从 PlcSnapshot 解码
static PlcValue decode(
    const RegisterInfo& reg,
    const PlcSnapshot& snapshot,
    const ProtocolProfile& profile);
```

两个 Snapshot 指针：Codec 根据 `reg.area` 自动选择用哪个。

### 6.3 实现逻辑摘要

> **L1/L2 保留不变**（纯数学编解码，供 encode 复用）。

**L3 新增**：

```cpp
PlcValue RegisterCodec::decode(const RegisterInfo& reg,
    const RawBitSnapshot* bits, const RawWordSnapshot* words,
    const ProtocolProfile& profile)
{
    if (reg.area == RegisterArea::Coil) {
        if (!bits) throw std::invalid_argument("...");
        return decodeFromCoil(reg, *bits);
    } else {
        if (!words) throw std::invalid_argument("...");
        return decodeFromHoldingReg(reg, *words, profile);
    }
}

// PlcSnapshot 便捷重载
PlcValue RegisterCodec::decode(const RegisterInfo& reg,
    const PlcSnapshot& snapshot, const ProtocolProfile& profile)
{
    return decode(reg, &snapshot.bits, &snapshot.words, profile);
}

// decodeFromCoil: bits.getBit(reg.address) → PlcValue{bool}
// decodeFromHoldingReg: words.getWords(reg.address, n) → span
//   → switch(type){ Bool→data[0]!=0; Int16→int16_t(data[0]);
//     Float32→decodeFloat(temp, policy) }
```

```cpp
std::vector<uint16_t> RegisterCodec::encode(
    const PlcValue& value, const RegisterInfo& reg,
    const ProtocolProfile& profile)
{
    return std::visit([&](const auto& v) -> std::vector<uint16_t> {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>)
            return { static_cast<uint16_t>(v ? (reg.area==Coil ? 0xFF00 : 1) : 0) };
        else if constexpr (std::is_same_v<T, int16_t>)
            return { static_cast<uint16_t>(v) };
        else if constexpr (std::is_same_v<T, float>)
            return encodeFloat(v, policy);
        else throw std::invalid_argument("...");
    }, value);
}
```

### 6.4 TDD 测试

**文件**：`tests/.../test_register_codec_snapshot.cpp`

```
Test: decode bool from Coil snapshot (true/false)
Test: decode int16 from Word snapshot
Test: decode float32 BigEndian (0x4316,0x0000 → 150.0f)
Test: decode out_of_range → std::out_of_range
Test: decode with endianOverride
Test: encode bool→coil → {0xFF00}, bool→holding → {1}
Test: encode float32 BigEndian → {0x4316,0x0000}
Test: encode float32 LittleEndian → {0x0000,0x4316}
Test: decode→PlcValue holds correct alternative
```

---

## 七、P3：PlcSnapshot + AddressPacker + PlcPoller

### 7.1 为什么需要 PlcSnapshot

**核心问题：PLC 读取不是一次完成的。**

后台轮询的实际流程：

```
T0: FC01 读 M100~M130  →  成功 → RawBitSnapshot (最新)
T1: FC03 读 D100~D130  →  失败/超时 → RawWordSnapshot (旧的)
```

如果系统直接持有两个独立的 RawBitSnapshot 和 RawWordSnapshot，就会变成：

| 数据 | 状态 |
|------|------|
| M100 (运动完成) | true（最新） |
| D124 (当前位置) | 旧值 |

UI 上会显示"运动完成=true"但"当前位置=旧值"——**自相矛盾**。

**工业安全场景**：急停状态（M130）已触发，但位置（D124）还是旧值——控制系统可能做出错误判断，认为"位置还安全"。

### 7.2 PlcSnapshot 的定义

**PlcSnapshot = "一次完整 PLC 现场照片"**

它回答一个根本问题：**"系统到底该不该相信这次 PLC 状态？"**

```cpp
// infrastructure/plc/protocol/PlcSnapshot.h
#pragma once
#include <cstdint>
#include "MemorySnapshot.h"

namespace plc::protocol {

struct PlcSnapshot {
    RawBitSnapshot  bits;       // Coil 物理快照
    RawWordSnapshot words;      // HoldingReg 物理快照
    bool            complete;   // 本轮所有 FC 调用全部成功 → true
    uint64_t        timestamp;  // 采集时间戳 (ms)

    PlcSnapshot(RawBitSnapshot b, RawWordSnapshot w, bool cmp, uint64_t ts)
        : bits(std::move(b)), words(std::move(w)), complete(cmp), timestamp(ts) {}

    /// @brief 是否可信（所有子快照都来自成功的网络读取）
    bool isTrusted() const { return complete; }
};

} // namespace plc::protocol
```

### 7.3 complete 字段的工作机制

```
Poll 循环:

  FC01 成功 ✓ → bits = RawBitSnapshot(101, 30, payload_coil)
  FC03 失败 ✗ → (words未更新)

  → PlcSnapshot(bits, oldWords, complete=false, ts=now)
                                    ─────────────
                                    业务层据此判断：
                                    "这次PLC状态不可信"
```

业务层使用：

```cpp
void updateAxis(const PlcSnapshot& snap) {
    if (!snap.isTrusted()) {
        // 跳过本轮——使用上一轮可信的旧值
        return;
    }
    // 正常解码和使用
    float pos = device->readFloat("ABS_POSITION"); // 从 snap 解码
}
```

### 7.4 PlcSnapshot 与 Raw 快照的关系

```
PlcSnapshot 不是多余结构，它是"设备当前状态照片"的容器。

RawBitSnapshot  = "一张黑白照片"（Coil 世界）
RawWordSnapshot = "一张彩色照片"（Register 世界）
PlcSnapshot     = "同一时刻的完整现场"（黑白+彩色绑定）
              + 可信度标记（complete）
              + 时间戳
```

### 7.5 PlcSnapshot TDD 测试

**文件**：`tests/infrastructure/protocol/test_plc_snapshot.cpp`

```
Test: construct_complete_snapshot → isTrusted=true
Test: construct_incomplete_snapshot → isTrusted=false
Test: timestamp_correctly_stored
Test: bit_snapshot_accessible
Test: word_snapshot_accessible
Test: move_semantics_preserve_data
```

---

### 7.6 打包的必要性

Y 轴 Feedback 区：
```
Coil:  101(MOVE_DONE), 113(ABS_MOVING), 114(REL_MOVING), 115(JOGGING)
Word:  101(STATE), 111(ALARM_CODE), 124-125(ABS_POSITION),
       126-127(REL_POSITION), 138-139(REL_ZERO_OFFSET), 1022-1023(...)
```

逐读：**6 次 Modbus 事务**。打包后：**5 次**（Coil 101~115一次FC01 + 4次FC03）。

### 7.7 合并算法

```
输入: 按Area分组的RegisterInfo*列表（按地址排序）
参数: maxGap（允许最大间隙），maxBlockSize（单次最大寄存器数）
输出: vector<RegisterBlock>

1. 遍历排序列表
2. 若当前地址 ≤ 上一块endAddress + maxGap → 合并
3. 否则 → 封存当前块，新建
4. 应用 maxBlockSize 拆分超长块
```

`RegisterBlock{area, startAddress, totalRegisterCount}`。

### 7.8 PlcPoller.h

**文件**：`infrastructure/plc/protocol/PlcPoller.h`

```cpp
class PlcPoller {
public:
    PlcPoller(uint16_t maxGap, uint16_t maxBlockSize);
    std::vector<RegisterBlock> buildPollPlan(
        const RegisterRegistry& registry, RegisterGroup group) const;

    /// @brief 执行一轮完整轮询，产出 PlcSnapshot
    /// @param client Modbus 传输客户端
    /// @param registry 寄存器注册表
    /// @param group 轮询组（Feedback / Config / Command）
    /// @return PlcSnapshot（complete=true 仅当所有 FC 调用成功）
    PlcSnapshot poll(
        IModbusClient& client,
        const RegisterRegistry& registry,
        RegisterGroup group) const;

private:
    uint16_t m_maxGap, m_maxBlockSize;
    std::vector<RegisterBlock> packByArea(
        std::vector<const RegisterInfo*>, RegisterArea) const;
};
```

### 7.9 PlcPoller::poll 的实现逻辑

```cpp
PlcSnapshot PlcPoller::poll(
    IModbusClient& client,
    const RegisterRegistry& registry,
    RegisterGroup group) const
{
    auto blocks = buildPollPlan(registry, group);
    bool allSuccess = true;
    RawBitSnapshot bits(...);   // 累积
    RawWordSnapshot words(...); // 累积
    uint64_t ts = now();

    for (auto& block : blocks) {
        if (block.area == RegisterArea::Coil) {
            auto payload = client.readCoils(block.startAddress, block.totalRegisterCount);
            if (!payload) { allSuccess = false; continue; }
            bits = RawBitSnapshot(block.startAddress, block.totalRegisterCount, *payload);
        } else {
            auto payload = client.readHoldingRegisters(block.startAddress, block.totalRegisterCount);
            if (!payload) { allSuccess = false; continue; }
            words = RawWordSnapshot(block.startAddress, *payload);
        }
    }

    return PlcSnapshot(std::move(bits), std::move(words), allSuccess, ts);
}
```

### 7.10 TDD 测试

**文件**：`tests/.../test_plc_poller.cpp`

```
Test: single_register → 1 block
Test: adjacent_registers → merge to 1 block
Test: gap_exceeds_maxGap → split to 2 blocks
Test: gap_within_maxGap → merge to 1 block
Test: overflow_maxBlockSize → split
Test: mixed_areas → never merged
Test: empty_registry → 0 blocks
Test: output_sorted_by_address
Test: poll_all_success → complete=true
Test: poll_partial_fail → complete=false
```

---

## 八、P4：PlcDevice — 终极门面

### 8.1 地位

PlcDevice 是所有上游代码**唯一需打交道的对象**，封装：
- RegisterRegistry（查元数据）
- PlcSnapshot（一致现场照片）← v4 用 PlcSnapshot 替代独立的 Raw 快照
- RegisterCodec（编解码）
- PlcPoller（轮询规划）
- IModbusClient（传输，独立阶段）

### 8.2 PlcDevice.h

**文件**：`infrastructure/plc/protocol/PlcDevice.h`

```cpp
class PlcDevice {
public:
    PlcDevice(const RegisterRegistry&, const ProtocolProfile&);

    // ── 读取（从当前 Snapshot 解码）──
    PlcValue readValue(const std::string& name) const;
    bool readBool(const std::string& n) const { return getValue<bool>(readValue(n)); }
    int16_t readInt16(const std::string& n) const { return getValue<int16_t>(readValue(n)); }
    float readFloat(const std::string& n) const { return getValue<float>(readValue(n)); }

    // ── 写入（编码后通过 Transport 下发）──
    void writeValue(const std::string& name, const PlcValue& value);

    // ── v4 关键变更：直接操作 PlcSnapshot ──
    void updateSnapshot(PlcSnapshot snap);       // Poller 产出后整包替换
    const PlcSnapshot& snapshot() const;          // 获取当前一致现场照片
    bool isStateTrusted() const;                  // 当前快照是否可信

    const RegisterRegistry& registry() const;

private:
    const RegisterRegistry& m_registry;
    const ProtocolProfile& m_profile;
    PlcSnapshot m_snapshot;   // ← v4: 单个 PlcSnapshot 替代 v3 的两个 unique_ptr
};
```

### 8.3 PlcDevice 实现骨架

```cpp
PlcValue PlcDevice::readValue(const std::string& name) const {
    const RegisterInfo* reg = m_registry.findByName(name);
    if (!reg) throw std::invalid_argument("Register not found: " + name);
    // v4: 直接从 PlcSnapshot 解码
    return RegisterCodec::decode(*reg, m_snapshot, m_profile);
}

void PlcDevice::writeValue(const std::string& name, const PlcValue& value) {
    const RegisterInfo* reg = m_registry.findByName(name);
    if (!reg) throw std::invalid_argument("Register not found: " + name);
    if (reg->access == RegisterAccess::ReadOnly)
        throw std::invalid_argument("ReadOnly register");
    auto encoded = RegisterCodec::encode(value, *reg, m_profile);
    // → Transport: m_client->write(reg->area, reg->address, encoded);
}

void PlcDevice::updateSnapshot(PlcSnapshot snap) {
    m_snapshot = std::move(snap);  // 整包替换，保证一致性
}

const PlcSnapshot& PlcDevice::snapshot() const {
    return m_snapshot;
}

bool PlcDevice::isStateTrusted() const {
    return m_snapshot.isTrusted();
}
```

### 8.4 PlcDevice + PlcPoller 协作流程

```cpp
// 主循环 (每 10ms 一次)
void tick() {
    // 1. Poller 执行所有 FC 调用，产出完整的 PlcSnapshot
    PlcSnapshot snap = m_poller.poll(*m_client, m_registry, RegisterGroup::Feedback);

    // 2. Device 接受整张照片
    m_device.updateSnapshot(std::move(snap));

    // 3. 业务层读取——保证一致性
    if (m_device.isStateTrusted()) {
        float pos = m_device.readFloat("ABS_POSITION");
        bool done = m_device.readBool("MOVE_DONE");
        // pos 和 done 来自同一轮采集，不会矛盾
        m_axis.update(pos, done);
    } else {
        // 本轮不可信，跳过更新，使用上一轮值
    }
}
```

### 8.5 TDD 测试

**文件**：`tests/.../test_plc_device.cpp`

```
Test: read_bool_from_coil → PlcValue{true}
Test: read_float32_from_holding_reg → 150.0f
Test: read_nonexistent_throws → std::invalid_argument
Test: write_readonly_throws → std::invalid_argument
Test: snapshot_update_replaces_old_data
Test: isTrusted_true_when_complete_snapshot
Test: isTrusted_false_when_incomplete_snapshot
Test: read_after_incomplete_snapshot_still_works (fallback to data)
Test: read_int16_from_holding_reg → int16_t{3}
```

---

## 九、完整 Pipeline 总览

### 9.1 反馈读取链路（高频 Poll）

```
Polling Loop (10ms)
    ▼
PlcPoller::buildPollPlan(registry, Feedback)
    ▼  vector<RegisterBlock>
[ FC01: Coil 101~130 ]  ──成功──▶  RawBitSnapshot
[ FC03: D100~D180  ]  ──成功──▶  RawWordSnapshot
    │                                    │
    └──────── 绑定 ──────────────────────┘
                    ▼
    PlcSnapshot {bits, words, complete=true, ts}
                    ▼
    PlcDevice::updateSnapshot(snap)
                    ▼  (业务层调用)
    PlcDevice::readValue("ABS_POSITION") → Codec::decode → PlcValue{150.0f}
                    ▼
    Y轴控制算法
```

### 9.2 命令下发链路

```
moveAbsolute(Y, 150.0)
    ▼
writeValue("ABS_TARGET",{150.0f}) → encode → FC10(D124=[0x4316,0x0000])
writeValue("ABS_MOVE_TRIGGER",{true}) → encode → FC05(M42=ON)
    ... 50ms pulseWidthMs ...
定时器 → FC05(M42=OFF)
```

---

## 十、文件清单汇总

| # | 文件 | 阶段 | 类型 |
|---|------|------|------|
| 1 | `infrastructure/plc/protocol/PlcValue.h` | P0 | 新增 |
| 2 | `tests/.../test_plc_value.cpp` | P0 | 新增测试 |
| 3 | `.../validator/ProtocolViolation.h` | P1 | 新增 |
| 4 | `.../validator/ProtocolConstraintValidator.h` | P1 | 新增 |
| 5 | `.../validator/ProtocolConstraintValidator.cpp` | P1 | 新增 |
| 6 | `tests/.../test_protocol_validator.cpp` | P1 | 新增测试 |
| 7 | `.../RegisterCodec.h` | P2 | **修改** |
| 8 | `tests/.../test_register_codec_snapshot.cpp` | P2 | 新增测试 |
| 9 | **`.../PlcSnapshot.h`** | **P3** | **新增** ★ |
| 10 | `tests/.../test_plc_snapshot.cpp` | P3 | **新增测试** ★ |
| 11 | `.../PlcPoller.h` | P3 | 新增 |
| 12 | `.../PlcPoller.cpp` | P3 | 新增 |
| 13 | `tests/.../test_plc_poller.cpp` | P3 | 新增测试 |
| 14 | `.../PlcDevice.h` | P4 | 新增 |
| 15 | `.../PlcDevice.cpp` | P4 | 新增 |
| 16 | `tests/.../test_plc_device.cpp` | P4 | 新增测试 |

**16 个文件：12 新增 + 1 修改 + 7 测试。**（v3 为 14 个文件，v4 新增 PlcSnapshot.h 及其测试）

---

## 十一、v3 → v4 核心变更

| 维度 | v3 | v4 | 理由 |
|------|-----|-----|------|
| PlcDevice 持有 | `unique_ptr<RawBitSnapshot>` + `unique_ptr<RawWordSnapshot>` | **单个 `PlcSnapshot` 值对象** | 保证 bit/word 来自同一轮采集 |
| 一致性保证 | 无（两个独立指针可被分别替换） | **complete 标志 + 整包替换** | FC01成功+FC03失败时标记不可信 |
| Codec 接口 | `decode(reg, bits*, words*, profile)` | **新增 `decode(reg, snapshot, profile)`** | 便捷重载，减少参数传递 |
| 业务层判断 | 无法知道数据是否一致 | **`snapshot.isTrusted()`** | UI/UseCase 可据此决定是否使用本轮数据 |
| P3 产物 | PlcPoller + AddressPacker | **PlcSnapshot + PlcPoller + AddressPacker** | Snapshot 是一致性契约的核心 |
| 时间戳 | 无 | **`uint64_t timestamp`** | 可追溯采集时间，支持 TTL 和延迟告警 |

---

## 十二、PlcSnapshot 核心价值总结

### 12.1 它不是"多余的数据结构"

```
RawBitSnapshot   = "一张黑白照片"（Coil 世界）
RawWordSnapshot  = "一张彩色照片"（Register 世界）
PlcSnapshot      = "同一时刻的完整现场"（黑白+彩色绑定+可信度标记+时间戳）
```

### 12.2 它解决了什么问题

| 场景 | 没有 PlcSnapshot | 有 PlcSnapshot |
|------|-----------------|----------------|
| FC01成功 FC03失败 | bit最新，word旧的 → UI自相矛盾 | complete=false → 业务层跳过本轮 |
| 急停已触发但位置旧 | 控制系统误判"位置安全" | isTrusted()=false → 不做判断 |
| 3个FC调用部分成功 | 无法追溯哪些数据是一起的 | 整包 accept/reject |

### 12.3 它在系统中的地位

```
PlcSnapshot = Runtime 的"世界状态"
              ─────────────────────
Poller  负责：不断从PLC拉数据，产出 PlcSnapshot
Snapshot 负责：保存当前设备状态的完整一致照片
Codec   负责：把原始数据翻译成业务值
Device  负责：对外提供读写的统一门面
```

### 12.4 P3 的真正含义

P3 的本质是**让 PLC Runtime 活起来**：

```
PlcPoller (每20ms)
    →  读PLC
    →  产出 PlcSnapshot
    →  更新 PlcDevice
    →  UI 读 Snapshot
    →  UseCase 读 Snapshot
    →  Alarm 系统读 Snapshot
```

每一个消费者读到的都是**同一张完整的设备状态照片**。

---

## 十三、五维度评审

| 维度 | v3 | v4 |
|------|-----|-----|
| 物理/逻辑分离 | ✅ PLC bit/word → Snapshot → Codec → PlcValue | ✅ 不变 + PlcSnapshot 增加一致性层 |
| 零拷贝性能 | ✅ span 直接划出内存视图 | ✅ 不变 |
| 类型安全 | ✅ std::variant 编译期保证 | ✅ 不变 |
| 数据一致性 | ⚠️ bits/words 独立，无保证 | ✅ complete 标志 + 整包替换 |
| TDD可测性 | ✅ 每层独立测试 | ✅ 新增 test_plc_snapshot.cpp |
| 架构可演进性 | ✅ Codec L1/L2/L3 三层扩展 | ✅ 不变 + TTL/延迟告警可基于 timestamp |
| 兼容性 | ✅ Validator/Metadata/Registry/Snapshot 不变 | ✅ 所有 v3 接口向前兼容 |

---

## 十四、实施注意事项

1. **CMakeLists.txt**：新增 .cpp 时同步更新
2. **命名空间**：统一 `namespace plc::protocol`
3. **TDD 纪律**：先测试→编译失败→实现→测试通过
4. **Transport 层后置**：P0~P4 完成前不引入 IModbusClient/Socket/Network
5. **PlcSnapshot 构造时机**：在 PlcPoller::poll() 内部构造，外部不应直接构造
6. **向后兼容**：已有的 RegisterCodec::decode(reg, bits*, words*, profile) 保留，新增重载版本

---

> **v4 核心精神**：把"PLC 没有 float"焊进每一层，把"一次完整的设备状态照片"焊进 Runtime。
>
> **五层契约**：
> - **RawBitSnapshot / RawWordSnapshot**：PLC 的真实物理形态
> - **PlcSnapshot**：bind(bits, words) + complete + timestamp —— 一致性契约
> - **RegisterCodec**：纯数学翻译，不关心业务含义
> - **PlcValue**：翻译成果的标准容器
> - **PlcDevice**：业务层唯一入口
>
> 从 PLC 的 bit/word 到业务层的 float/bool/position，每一步都有明确的边界和契约。
>
> **PlcSnapshot 一句话**：它不是多余的数据结构，而是**整个 PLC 当前状态的统一照片**。没有它，bit 是新的、word 是旧的、状态互相打架。有了它，系统才能真正"认为自己看到了一个完整设备"。
