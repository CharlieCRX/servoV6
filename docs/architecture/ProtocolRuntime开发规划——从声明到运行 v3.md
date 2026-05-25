# ProtocolRuntime 开发规划 —— 从声明到运行 v3

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-25 | 初版：基于当前实现，制定 L2c~L2e TDD 开发路线图 |
| v2.0 | 2026-05-25 | 修正：类型安全化、RegisterBlock 命名精确化、五维度架构评审 |
| **v3.0** | **2026-05-25** | **架构升级：引入物理/逻辑分离。PLC 只有 bit/word，float 是软件解释。确立 Snapshot → Codec → PlcValue 三层管线。** |

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

### 0.2 四个世界的映射

```
PLC世界        Snapshot世界         Decode世界          业务世界
(物理存在)     (零拷贝快照)         (语义翻译)           (强类型)
────────────────────────────────────────────────────────────────
bit            RawBitSnapshot       RegisterCodec        bool
│              │                    │                    │
│ Coil101=1    │ getBit(101)→true  │ decodeBool→true    │ MOVE_DONE=true
│              │                    │                    │
word           RawWordSnapshot      RegisterCodec        float
│              │                    │                    │
│ D124=0x4316  │ getWords(124,2)   │ decodeFloat+        │ ABS_POSITION
│ D125=0x0000  │ → span[2]         │ BigEndian→150.0f    │ = 150.0f
│              │                    │                    │
│              │                    │ RegisterCodec      │ int16
│              │                    │ decodeUint16→1     │ STATE=Standby
│              │                    │                    │
│              │                    │                    │ PlcValue
│              │                    │                    │ variant<bool,
│              │                    │                    │  int16,float>
```

**关键洞察**：
- **Snapshot 层**不知道"数据类型"——只知地址和 bit/word 数组。
- **Codec 层**不知道"业务含义"——只知元数据（Float32占2字，大端），执行纯数学转换。
- **PlcValue** 是翻译成果的标准载体。

### 0.3 读取链路的完整穿越

以 `float pos = plc.readFloat("Y_Axis.ABS_POSITION")` 为例：

```
Layer              Component                Action              Data Shape
──────              ─────────                ──────              ──────────
PLC                汇川H5U                  D124=0x4316,        bit/word
                                            D125=0x0000
Transport          IModbusClient            返回payload          vector<uint16_t>
Snapshot           RawWordSnapshot          store(D124,payload) span<const uint16>
                   零拷贝存储
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

> Snapshot 已就绪，Codec 未适配 Snapshot 接口。PlcValue 未定义。PlcDevice 未实现。需打通：**Snapshot → Codec → PlcValue → PlcDevice**。

---

## 二、v3 架构全景图

### 2.1 v2 的问题

v2 中 Codec 直接操作 `vector<uint16_t>` → bit/word 概念混淆、无法零拷贝、无法批量连续读。

### 2.2 v3 分层架构

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
       │          ┌───────┴───────┐
       │          ▼               ▼
       │  ┌────────────┐ ┌──────────────┐
       │  │RawBitSnap  │ │RawWordSnap   │
       │  │(Coil物理)  │ │(Reg物理)     │
       │  │vector<uint8│ │vector<uint16│
       │  └──────┬─────┘ └──────┬───────┘
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
| 1 | PLC→Transport→Snapshot | FC01/FC03拉回payload，零拷贝存储 |
| 2 | Snapshot→Codec→PlcValue | 划出span，结合EndianPolicy拼装 |
| 3 | 业务代码→Codec→Transport | encode成Modbus值，FC05/FC10发出 |
| 4 | PlcDevice脉冲管理 | EdgeTrigger: FC05(ON)→定时→FC05(OFF) |

---

## 三、v3 总体路线图

```
P0 ────→ P1 ────→ P2 ────→ P3 ────→ P4
 │         │         │         │         │
 ▼         ▼         ▼         ▼         ▼
PlcValue  Validator  Codec重构 PlcPoller PlcDevice
variant   4条规则   Snapshot→ Address   完整门面
          不变      Value     Packer    read/write
```

**核心原则**：
1. 先定义 PlcValue — Codec/PlcDevice 的标准货币
2. Validator 不变 — V2 设计完全兼容 v3
3. Codec 重构 — L1/L2 保留，L3 从 Snapshot 解码
4. TDD 先行 — 每步先写测试

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

## 七、P3：AddressPacker + PlcPoller

### 7.1 打包的必要性

Y 轴 Feedback 区：
```
Coil:  101(MOVE_DONE), 113(ABS_MOVING), 114(REL_MOVING), 115(JOGGING)
Word:  101(STATE), 111(ALARM_CODE), 124-125(ABS_POSITION),
       126-127(REL_POSITION), 138-139(REL_ZERO_OFFSET), 1022-1023(...)
```

逐读：**6 次 Modbus 事务**。打包后：**5 次**（Coil 101~115一次FC01 + 4次FC03）。

### 7.2 合并算法

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

### 7.3 PlcPoller.h

**文件**：`infrastructure/plc/protocol/PlcPoller.h`

```cpp
class PlcPoller {
public:
    PlcPoller(uint16_t maxGap, uint16_t maxBlockSize);
    std::vector<RegisterBlock> buildPollPlan(
        const RegisterRegistry& registry, RegisterGroup group) const;
private:
    uint16_t m_maxGap, m_maxBlockSize;
    std::vector<RegisterBlock> packByArea(
        std::vector<const RegisterInfo*>, RegisterArea) const;
};
```

### 7.4 TDD 测试

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
```

---

## 八、P4：PlcDevice — 终极门面

### 8.1 地位

PlcDevice 是所有上游代码**唯一需打交道的对象**，封装：
- RegisterRegistry（查元数据）
- RawBitSnapshot + RawWordSnapshot（物理快照）
- RegisterCodec（编解码）
- PlcPoller（轮询规划）
- IModbusClient（传输，独立阶段）

### 8.2 PlcDevice.h

**文件**：`infrastructure/plc/protocol/PlcDevice.h`

```cpp
class PlcDevice {
public:
    PlcDevice(const RegisterRegistry&, const ProtocolProfile&);
    PlcValue readValue(const std::string& name) const;
    void writeValue(const std::string& name, const PlcValue& value);

    bool readBool(const std::string& n) const { return getValue<bool>(readValue(n)); }
    int16_t readInt16(const std::string& n) const { return getValue<int16_t>(readValue(n)); }
    float readFloat(const std::string& n) const { return getValue<float>(readValue(n)); }

    void updateSnapshots(RawBitSnapshot bits, RawWordSnapshot words);
    const RawBitSnapshot& bitSnapshot() const;
    const RawWordSnapshot& wordSnapshot() const;
    const RegisterRegistry& registry() const;

private:
    const RegisterRegistry& m_registry;
    const ProtocolProfile& m_profile;
    std::unique_ptr<RawBitSnapshot> m_bits;
    std::unique_ptr<RawWordSnapshot> m_words;
};
```

### 8.3 PlcDevice 实现骨架

```cpp
PlcValue PlcDevice::readValue(const std::string& name) const {
    const RegisterInfo* reg = m_registry.findByName(name);
    if (!reg) throw std::invalid_argument("Register not found: " + name);
    return RegisterCodec::decode(*reg, m_bits.get(), m_words.get(), m_profile);
}

void PlcDevice::writeValue(const std::string& name, const PlcValue& value) {
    const RegisterInfo* reg = m_registry.findByName(name);
    if (!reg) throw std::invalid_argument("Register not found: " + name);
    if (reg->access == RegisterAccess::ReadOnly)
        throw std::invalid_argument("ReadOnly register");
    auto encoded = RegisterCodec::encode(value, *reg, m_profile);
    // → Transport: m_client->write(reg->area, reg->address, encoded);
}

void PlcDevice::updateSnapshots(RawBitSnapshot bits, RawWordSnapshot words) {
    m_bits = std::make_unique<RawBitSnapshot>(std::move(bits));
    m_words = std::make_unique<RawWordSnapshot>(std::move(words));
}
```

### 8.4 TDD 测试

**文件**：`tests/.../test_plc_device.cpp`

```
Test: read_bool_from_coil → PlcValue{true}
Test: read_float32_from_holding_reg → 150.0f
Test: read_nonexistent_throws → std::invalid_argument
Test: write_readonly_throws → std::invalid_argument
Test: snapshot_update_replaces_old_data
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
IModbusClient::readBlocks(blocks)
    ▼  RawBitSnapshot + RawWordSnapshot
PlcDevice::updateSnapshots(bits, words)     ← 零拷贝
    ▼  (业务层调用)
PlcDevice::readValue("ABS_POSITION") → decode → PlcValue{150.0f}
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
| 9 | `.../PlcPoller.h` | P3 | 新增 |
| 10 | `.../PlcPoller.cpp` | P3 | 新增 |
| 11 | `tests/.../test_plc_poller.cpp` | P3 | 新增测试 |
| 12 | `.../PlcDevice.h` | P4 | 新增 |
| 13 | `.../PlcDevice.cpp` | P4 | 新增 |
| 14 | `tests/.../test_plc_device.cpp` | P4 | 新增测试 |

**14 个文件：10 新增 + 1 修改 + 5 测试。**

---

## 十一、v2 → v3 核心变更

| 维度 | v2 | v3 | 理由 |
|------|-----|-----|------|
| 核心哲学 | IR多阶段Lowering | **PLC只有bit/word** | 物理/逻辑分离 |
| P0 | Validator | **PlcValue** | 先定义标准货币 |
| 数据载体 | 未定义 | **variant\<bool,int16,float\>** | 明确类型契约 |
| Codec接口 | decodeFloat(vector) | **decode(reg,bits\*,words\*,profile)→PlcValue** | 对接Snapshot |
| 轮询 | BatchWritePlan | **PlcPoller+打包算法** | 先解高频Poll |
| 门面 | 概念提及 | **完整PlcDevice设计** | 脉冲管理+便捷接口 |

---

## 十二、五维度评审

| 维度 | 评价 |
|------|------|
| 物理/逻辑分离 | ✅ PLC bit/word → Snapshot → Codec → PlcValue |
| 零拷贝性能 | ✅ span 直接划出内存视图 |
| 类型安全 | ✅ std::variant 编译期保证 |
| TDD可测性 | ✅ PlcValue→全链路每层独立测试 |
| 架构可演进性 | ✅ Codec L1/L2/L3 三层扩展只需加case |
| 兼容性 | ✅ Validator/Metadata/Registry/Snapshot 不变 |

---

## 十三、实施注意事项

1. **CMakeLists.txt**：新增 .cpp 时同步更新
2. **命名空间**：统一 `namespace plc::protocol`
3. **TDD 纪律**：先测试→编译失败→实现→测试通过
4. **Transport 层后置**：P0~P4 完成前不引入 IModbusClient/Socket/Network

---

> **v3 核心精神**：把"PLC 没有 float"焊进每一层。
> - **RawBitSnapshot / RawWordSnapshot**：PLC 的真实物理形态
> - **RegisterCodec**：纯数学翻译，不关心业务含义
> - **PlcValue**：翻译成果的标准容器
> - **PlcDevice**：业务层唯一入口
>
> 从 PLC 的 bit/word，到业务层的 float/bool/position，每一步都有明确的边界和契约。
