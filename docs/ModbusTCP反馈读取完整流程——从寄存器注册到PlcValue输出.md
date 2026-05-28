# 修改历史

| 版本 | 作者 | 日期       | 备注 |
|------|------|------------|------|
| v1.0 | Cline| 2026-05-28 | 初版，基于 `PlcPoller::prepare()` / `assemble()` 两阶段架构 |
| v2.0 | Cline| 2026-05-28 | 对齐实际代码 — `PlcPoller` 构造时预分析地址、`prune` 已移除、`PlcDevice` 使用 `RegisterCodec::decode(snapshot)` |

**关联测试文件**
`tests/infrastructure/protocol/test_real_feedback_readonly.cpp` — 真实 PLC 反馈只读集成测试

---

# 1. 整体架构概览

反馈读取链路总共涉及 **6 个核心组件**，形成一条自底向上的流水线：

```
 [1] RegisterInfo 常量定义      → RegisterAddressAll.h
     │   (编译期注册：地址 + 类型 + 端序 + 行为)
     ▼
 [2] RegisterRegistry            → RegisterRegistry.h
     │   (运行时容器：批量收集 RegisterInfo)
     ▼
 [3] PlcPoller                   → PlcPoller.h / PlcPoller.cpp
     │   (构造时预分析 → 地址区间打包 → prepare/assemble 两阶段)
     ▼
 [4] IModbusClient (AsioModbusTcpClient) → 网络 IO
     │   (根据 PollRequest 发送 FC01 / FC03，收集字节响应)
     ▼
 [5] PlcSnapshot                 → PlcSnapshot.h / MemorySnapshot.h
     │   (RawBitSnapshot + RawWordSnapshot 原始快照容器)
     ▼
 [6] PlcDevice + RegisterCodec   → PlcDevice.h / RegisterCodec.h
         (从 PlcSnapshot 解码 → PlcValue → 类型化读取)
```

**关键设计思想：**
- **编译期类型安全**：`PlcDevice` 的所有读 API 接收 `const RegisterInfo&`，编译期即可捕获错误
- **信任标记**：`PlcSnapshot` 携带 `m_trusted` 标记 — 所有子请求全部成功才标记为 true
- **两阶段轮询**：`prepare()` 生成请求清单，`assemble()` 将响应拼装为统一快照 — 调用方完全掌控网络 IO 时机

---

# 2. 阶段 1：寄存器注册（编译期）

## 2.1 RegisterInfo 数据结构

```cpp
// infrastructure/plc/protocol/RegisterMetadata.h
struct RegisterInfo {
    RegisterArea area;           // Coil / HoldingReg（决定 FC 函数码）
    uint16_t     address;        // Modbus 地址（0-based 逻辑地址）
    RegisterType type;           // Bool / Int16 / Float32
    RegisterAccess access;       // ReadOnly / ReadWrite
    RegisterBehavior behavior;   // Continuous / Level / Latch / Edge
    RegisterGroup group;         // Command / Feedback / Alarm / Parameter
    const char*  unit;           // 物理单位（"mm", "mm/s", "deg" …）
    const char*  description;    // 中文描述
    uint16_t     timeoutMs;      // 边沿触发型指令的超时复位毫秒（Continuous 类填 0）
    std::optional<EndianPolicy> endianOverride;  // 寄存器级端序覆盖（可选）
};
```

## 2.2 寄存器常量定义

所有寄存器以 `constexpr RegisterInfo` 形式定义在 `RegisterAddressAll.h` 中，按 **轴命名空间** 和 **命令/反馈子命名空间** 组织：

```cpp
// --- 示例：X 轴绝对位置反馈 ---
namespace plc::reg::x_axis::feedback {
    constexpr RegisterInfo ABS_POSITION = {
        RegisterArea::HoldingReg,   // 区域：保持寄存器
        120,                        // 地址：D120
        RegisterType::Float32,      // 类型：32位浮点
        RegisterAccess::ReadOnly,   // 访问：只读
        RegisterBehavior::Continuous, // 行为：连续变化
        RegisterGroup::Feedback,    // 分组：状态反馈
        "mm",                       // 单位
        "轴X当前绝对位置",           // 描述
        0                           // 超时（不适用）
    };
}

// --- 示例：X 轴急停激活反馈（Coil 类型） ---
namespace plc::reg::system_global::feedback {
    constexpr RegisterInfo ESTOP_ACTIVE = {
        RegisterArea::Coil,
        130,
        RegisterType::Bool,
        RegisterAccess::ReadOnly,
        RegisterBehavior::Continuous,
        RegisterGroup::Alarm,
        "",
        "设备急停中",
        0
    };
}
```

**注册数据覆盖范围：**

| 命名空间                | 内容                                           |
|-------------------------|------------------------------------------------|
| `system_global`         | 设备级急停、龙门报警（Coil 80-180）               |
| `x_axis` / `y_axis` / `z_axis` / `r_axis` | 各轴命令 Coil / 命令 HoldingReg / 反馈 Coil / 反馈 HoldingReg |

**设计要点：**
- `constexpr` 确保完全编译期求值，零运行时开销
- 命名空间层级 ＝ 业务语义层级，调用方代码自文档化
- `endianOverride` 默认为 `std::nullopt`，绝大部分寄存器走设备 Profile 全局端序

---

# 3. 阶段 2：RegisterRegistry 运行时组织

```cpp
// infrastructure/plc/protocol/RegisterRegistry.h
class RegisterRegistry {
public:
    void add(const RegisterInfo& reg);
    void addAll(std::span<const RegisterInfo> regs);
    void addAll(std::initializer_list<RegisterInfo> regs); // 支持 {A, B, C} 语法

    const std::vector<RegisterInfo>& all() const;
    std::vector<RegisterInfo> findByGroup(RegisterGroup) const;
    std::vector<RegisterInfo> findByArea(RegisterArea) const;
    const RegisterInfo* findByAddress(RegisterArea, uint16_t) const;
};
```

**典型使用模式（来自测试和业务代码）：**

```cpp
// 批量注册 X 轴反馈线圈
registry.addAll({
    plc::reg::x_axis::feedback::MOVE_DONE,
    plc::reg::x_axis::feedback::ABS_MOVING,
    plc::reg::x_axis::feedback::REL_MOVING,
    plc::reg::x_axis::feedback::JOGGING,
    plc::reg::x_axis::feedback::TOLERANCE_FLAG,
    plc::reg::x_axis::feedback::TOLERANCE_TIMEOUT,
    plc::reg::x_axis::feedback::LINKAGE_STATE,
    plc::reg::x_axis::feedback::SOFT_LIMIT_STATE,
});

// 批量注册 X 轴反馈保持寄存器
registry.addAll({
    plc::reg::x_axis::feedback::STATE,
    plc::reg::x_axis::feedback::ABS_POSITION,
    plc::reg::x_axis::feedback::REL_POSITION,
    // …
});
```

**关键点：** Registry 仅作线性容器的薄封装，不涉及地址分析 — 地址分析与打包由 `PlcPoller` 负责。

---

# 4. 阶段 3：PlcPoller 构造时预分析（地址区间打包）

**这是此前文档漏洞最大的环节。** 实际代码中，`PlcPoller` 在 **构造时（而非 polling 时）** 完成全部地址分析。

## 4.1 构造流程

```cpp
// PlcPoller.cpp 构造函数（简化版）
PlcPoller::PlcPoller(const RegisterRegistry& registry)
  : m_registry(registry)
{
    // === 步骤 1：提取并排序 Coil 地址 ===
    auto coilRegs = m_registry.findByArea(RegisterArea::Coil);
    m_coilAddresses.clear();
    for (const auto& reg : coilRegs) {
        m_coilAddresses.push_back(reg.address);
    }
    std::sort(m_coilAddresses.begin(), m_coilAddresses.end());

    // 步骤 2：打包 Coil 地址区间（容忍间隙 ≤ 200）
    m_coilRanges = AddressPacker::pack(m_coilAddresses, 200);

    // === 步骤 3：提取 HoldingReg 地址并按位宽展开 ===
    auto wordRegs = m_registry.findByArea(RegisterArea::HoldingReg);
    m_wordAddresses.clear();
    for (const auto& reg : wordRegs) {
        m_wordAddresses.push_back(reg.address);
        if (reg.type == RegisterType::Float32) {
            m_wordAddresses.push_back(reg.address + 1); // Float32 占 2 字
        }
    }
    std::sort(m_wordAddresses.begin(), m_wordAddresses.end());

    // 步骤 4：去重
    m_wordAddresses.erase(
        std::unique(m_wordAddresses.begin(), m_wordAddresses.end()),
        m_wordAddresses.end());

    // 步骤 5：打包 HoldingReg 地址区间（容忍间隙 ≤ 50）
    m_wordRanges = AddressPacker::pack(m_wordAddresses, 50);
}
```

## 4.2 AddressPacker::pack() 算法

```cpp
/**
 * @brief 将有序地址列表打包为连续或可容忍合并的 AddressRange 列表
 * @param addresses 已排序 uint16_t 地址列表
 * @param maxGap    容忍的最大地址间隙（≤ 此值 → 合并为一个区间）
 *
 * 逻辑：
 *   如果 addresses[i] - addresses[i-1] - 1 ≤ maxGap
 *      → 扩展当前区间长度以覆盖 addresses[i]
 *   否则
 *      → 提交当前区间，新开一个起始于 addresses[i] 的区间
 */
std::vector<AddressRange> AddressPacker::pack(
    const std::vector<uint16_t>& addresses, uint16_t maxGap);
```

**举例：** 假设有以下 HoldingReg 地址：
```
[100, 101, 120, 121, 122, 130]
```
当 `maxGap = 50` 时：
- `100 → 101` 间隙 0 ≤ 50 → 合并
- `101 → 120` 间隙 18 ≤ 50 → 合并（区间扩展为 start=100, count=23）
- `120 → 121` 间隙 0 ≤ 50 → 合并（count 扩展为 23）
- `121 → 122` 间隙 0 ≤ 50 → 合并（count 扩展为 23）
- `122 → 130` 间隙 7 ≤ 50 → 合并（count 扩展为 31）

**结果：** `[{start=100, count=31}]` — 一次 FC03 即可读取全部

## 4.3 prepare() — 生成 PollRequest

```cpp
PollRequest PlcPoller::prepare() const {
    PollRequest req;
    for (const auto& range : m_coilRanges) {
        req.coilRequests.push_back(CoilReadRequest{range});
    }
    for (const auto& range : m_wordRanges) {
        req.wordRequests.push_back(WordReadRequest{range});
    }
    return req;
}
```

`PollRequest` 结构（来自 `PlcPoller.h`）：
```cpp
struct CoilReadRequest { AddressRange range; };
struct WordReadRequest { AddressRange range; };
struct PollRequest {
    std::vector<CoilReadRequest> coilRequests;
    std::vector<WordReadRequest> wordRequests;
};
```

**关键设计决策：** `prepare()` 只读不写，可在任意线程调用而无需考虑并发问题。

---

# 5. 阶段 4：网络 IO — 发送 Modbus 读取指令

此阶段由调用方（`AsioModbusTcpClient`）完成。`PlcPoller` 不持有 transport 引用 — 它只负责生成请求并拼装响应。

## 5.1 典型发送流程（参照测试代码）

```pseudo
// 伪代码 — 参照 test_real_feedback_readonly.cpp
PollRequest req = poller.prepare();

std::vector<std::vector<uint8_t>>  coilResponses;
std::vector<std::vector<uint16_t>> wordResponses;

// 1. 顺序发送 Coil 读取 (FC01)
for (auto& c : req.coilRequests) {
    auto bytes = client->readCoils(c.range.startAddress, c.range.count);
    coilResponses.push_back(std::move(bytes));
}

// 2. 顺序发送 HoldingReg 读取 (FC03)
for (auto& w : req.wordRequests) {
    auto words = client->readHoldingRegisters(w.range.startAddress, w.range.count);
    wordResponses.push_back(std::move(words));
}

// 3. 拼装
PlcSnapshot snap = poller.assemble(coilResponses, wordResponses, timestamp);
```

**Modbus 函数码对照：**

| RegisterArea | 函数码 | IModbusClient 方法             | 响应格式               |
|--------------|--------|-------------------------------|------------------------|
| `Coil`       | FC01   | `readCoils(addr, count)`       | `std::vector<uint8_t>`  |
| `HoldingReg` | FC03   | `readHoldingRegisters(addr, c)`| `std::vector<uint16_t>` |

---

# 6. 阶段 5：PlcPoller::assemble() — 拼装 PlcSnapshot

```cpp
PlcSnapshot PlcPoller::assemble(
    const std::vector<std::vector<uint8_t>>&  coilResponses,
    const std::vector<std::vector<uint16_t>>& wordResponses,
    uint64_t timestamp) const;
```

## 6.1 Coil 响应拼装逻辑

1. **校验响应数量** 与 `m_coilRanges.size()` 是否一致（不一致 → `complete = false`）
2. **计算总跨度：** `firstStartAddr` 到 `lastEndAddr`，得出 `totalBits`
3. **创建合并缓冲区：** `mergedPayload[ceil(totalBits/8)]` 全零
4. **逐区间拷贝：** 对于每个 `m_coilRanges[i]`：
   - 计算该区间在合并缓冲区中的 bit 偏移量
   - 按位将 `coilResponses[i]` 拷贝到 `mergedPayload` 对应位置
5. **构造 `RawBitSnapshot(startAddr, totalBits, mergedPayload)`**

## 6.2 Word 响应拼装逻辑

1. **校验响应数量**
2. **计算合并 `uint16_t` 缓冲区大小** 并 `std::copy` 逐区间填充
3. **构造 `RawWordSnapshot(startAddr, mergedPayload)`**

## 6.3 PlcSnapshot 最终形态

```cpp
// PlcSnapshot.h
class PlcSnapshot {
public:
    RawBitSnapshot bits;    // Coil 快照
    RawWordSnapshot words;  // HoldingReg 快照
    bool trusted;           // 所有子请求均成功 → true
    uint64_t timestamp;

    bool isTrusted() const { return trusted; }
};
```

其中：
```cpp
// MemorySnapshot.h
class RawBitSnapshot {
    std::optional<bool> getBit(uint16_t address) const;  // 按位查询
};

class RawWordSnapshot {
    std::optional<std::span<const uint16_t>> getWords(uint16_t address, uint16_t count) const;
};
```

**信任标记语义：** 仅当 **全部** Coil 响应数和 Word 响应数与预期完全匹配且各区间字节数有效时，`trusted = true`。上层应调用 `device.isStateTrusted()` 或 `snap.isTrusted()` 判断是否可用。

---

# 7. 阶段 6：PlcDevice + RegisterCodec — 解码为 PlcValue

## 7.1 快照更新

```cpp
// PlcDevice.h
void PlcDevice::updateSnapshot(PlcSnapshot snap) {
    m_snapshot = std::move(snap);
}
```

`PlcDevice` 持有最近一次轮询的 `PlcSnapshot`，通过 `const PlcSnapshot& snapshot()` 可获取只读引用。

## 7.2 readValue() — 核心解码入口

```cpp
PlcValue PlcDevice::readValue(const RegisterInfo& reg) const {
    return RegisterCodec::decode(reg, m_snapshot, m_profile);
}
```

## 7.3 RegisterCodec::decode() 解码链路

```cpp
// RegisterCodec.h — decode(RegisterInfo, PlcSnapshot, ProtocolProfile)
static PlcValue decode(const RegisterInfo& reg,
                        const PlcSnapshot& snapshot,
                        const ProtocolProfile& profile) {
    return decode(reg, &snapshot.bits, &snapshot.words, profile);
}

// 底层实现 — decode(RegisterInfo, RawBitSnapshot*, RawWordSnapshot*, ProtocolProfile)
static PlcValue decode(const RegisterInfo& reg,
                        const RawBitSnapshot* bits,
                        const RawWordSnapshot* words,
                        const ProtocolProfile& profile) {
    // 1. 快照合规性校验
    //    Coil 必须有 bits 指针；HoldingReg 必须有 words 指针

    // 2. 端序决议：优先寄存器级 override，否则降级设备全局端序
    EndianPolicy policy = resolvePolicy(reg, profile);

    // 3. 按 RegisterType 分发：
    switch (reg.type) {
    case RegisterType::Bool:
        return PlcValue{ bits->getBit(reg.address).value() };

    case RegisterType::Int16:
        return PlcValue{ static_cast<int16_t>((*words->getWords(reg.address, 1))[0]) };

    case RegisterType::Float32: {
        auto span = words->getWords(reg.address, 2);
        std::vector<uint16_t> tmp(span->begin(), span->end());
        float value = decodeFloat(tmp, policy);   // L2 引擎：端序驱动
        return PlcValue{ value };
    }
    }
}
```

## 7.4 类型化读取 API（类型安全门面）

```cpp
// 从 PlcValue 提取具体类型（编译期类型检查）
PlcValue v = device.readValue(x_axis::feedback::ABS_POSITION);

// 或直接使用便捷方法：
bool    moveDone  = device.readBool (x_axis::feedback::MOVE_DONE);
float   absPos    = device.readFloat(x_axis::feedback::ABS_POSITION);
int16_t state     = device.readInt16(x_axis::feedback::STATE);
```

**注意：** 若寄存器类型与调用方法不匹配（例如对 Float32 寄存器调用 `readBool()`），将抛出 `std::bad_variant_access`。

## 7.5 PlcValue 多态容器

```cpp
// PlcValue.h
using PlcValue = std::variant<bool, int16_t, float>;
// 辅助工具函数：
bool        isBool (const PlcValue&);
bool        isInt16(const PlcValue&);
bool        isFloat(const PlcValue&);
template<T> T getValue(const PlcValue&);
```

---

# 8. 完整数据流一图总结

```
                                    ┌─────────────────────────────────┐
                                    │  RegisterAddressAll.h           │
                                    │  constexpr RegisterInfo 常量     │
                                    │  x_axis::feedback::ABS_POSITION │
                                    └──────────────┬──────────────────┘
                                                   │ addAll({ … })
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  RegisterRegistry               │
                                    │  线性存储 vector<RegisterInfo>   │
                                    └──────────────┬──────────────────┘
                                                   │ 传入构造函数
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  PlcPoller (构造)               │
                                    │  ┌───────────────────────────┐  │
                                    │  │ ① 提取 Coil 地址并排序     │  │
                                    │  │ ② AddressPacker::pack()   │  │
                                    │  │ ③ 提取 Word 地址展开 Float│  │
                                    │  │ ④ 排序 + 去重 + pack      │  │
                                    │  └───────────────────────────┘  │
                                    │  结果：m_coilRanges / m_wordRanges│
                                    └──────────────┬──────────────────┘
                                                   │ prepare()
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  PollRequest                    │
                                    │  ├─ coilRequests: [Range, Range]│
                                    │  └─ wordRequests: [Range, …]    │
                                    └──────────────┬──────────────────┘
                                                   │ 调用方逐条发送
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  AsioModbusTcpClient (网络 IO)  │
                                    │  FC01 → vector<vector<uint8_t>> │
                                    │  FC03 → vector<vector<uint16_t>>│
                                    └──────────────┬──────────────────┘
                                                   │ assemble(responses, ts)
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  PlcSnapshot                    │
                                    │  ├─ RawBitSnapshot   (coils)    │
                                    │  ├─ RawWordSnapshot  (words)    │
                                    │  ├─ trusted: bool               │
                                    │  └─ timestamp                   │
                                    └──────────────┬──────────────────┘
                                                   │ device.updateSnapshot()
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  PlcDevice                      │
                                    │  readValue(reg)                 │
                                    │    → RegisterCodec::decode()    │
                                    │      ├─ Bool: bits.getBit()     │
                                    │      ├─ Int16: words.getWords() │
                                    │      └─ Float32: words.getWords │
                                    │              + decodeFloat(endian│
                                    └──────────────┬──────────────────┘
                                                   │
                                                   ▼
                                    ┌─────────────────────────────────┐
                                    │  PlcValue                       │
                                    │  variant<bool, int16_t, float>  │
                                    └─────────────────────────────────┘
```

---

# 9. 信任（Trusted）语义与鲁棒性

## 9.1 Trusted 的含义

- `PlcSnapshot::trusted = true` ⇔ **本轮轮询的所有子请求（Coil + Word）全部通过校验**
- 校验内容包括：响应向量数量匹配、各子响应的字节数不小于预期
- 任何子请求失败（超时、CRC 错误等），`trusted = false`，调用 `PlcPoller::untrusted(ts)` 生成占位快照

## 9.2 上层使用模式

```cpp
void tick() {
    // 1. 生成请求
    PollRequest req = poller.prepare();

    // 2. 发送所有 Coil 读取 (FC01)
    std::vector<std::vector<uint8_t>> coilResponses;
    for (auto& c : req.coilRequests) {
        coilResponses.push_back(client.readCoils(c.range.startAddress, c.range.count));
    }

    // 3. 发送所有 HoldingReg 读取 (FC03)
    std::vector<std::vector<uint16_t>> wordResponses;
    for (auto& w : req.wordRequests) {
        wordResponses.push_back(client.readHoldingRegisters(w.range.startAddress, w.range.count));
    }

    // 4. 拼装快照
    PlcSnapshot snap = poller.assemble(coilResponses, wordResponses, now());
    device.updateSnapshot(std::move(snap));

    if (device.isStateTrusted()) {
        // 所有寄存器数据信任 — 安全读取
        float xPos = device.readFloat(x_axis::feedback::ABS_POSITION);
        bool  done = device.readBool (x_axis::feedback::MOVE_DONE);
        // …
    } else {
        // 本轮数据不可信 — 保持上一轮的值，不更新 UI
    }
}
```

---

# 10. 与测试文件的对应关系

## 10.1 `test_real_feedback_readonly.cpp` 关键流程

```
1. IModbusClient 创建 (AsioModbusTcpClient → 真实 PLC IP)
2. ProtocolProfile 配置 (端序: BigEndian + HighWordFirst)
3. RegisterRegistry 批量注册 X 轴全部 ReadOnly 反馈寄存器
4. PlcPoller 构造 (内部完成地址分析)
5. PlcDevice 构造 + bindTransport
6. 主循环 (如 20 次)：
   a. PollRequest req = poller.prepare()
   b. for CoilReadRequest : client.readCoils() → coilResponses
   c. for WordReadRequest : client.readHoldingRegisters() → wordResponses
   d. PlcSnapshot snap = poller.assemble(coilResponses, wordResponses, now())
   e. device.updateSnapshot(snap)
   f. if (device.isStateTrusted()) 读取各寄存器值并打印/断言
   g. optional: std::this_thread::sleep_for(polling_interval)
```

## 10.2 `test_register_codec_snapshot.cpp` 验证的链路

- `RegisterCodec.decode(reg, bits, words, profile)` — 直接使用 `RawBitSnapshot` / `RawWordSnapshot`
- `RegisterCodec.decode(reg, snapshot, profile)` — 使用 `PlcSnapshot` 便捷重载
- Float32 大小端编解码正确性
- `PlcDevice.readValue()` + `PlcDevice.readFloat()` 的端到端集成

---

# 11. 设计决策 FAQ

**Q: 为什么 `PlcPoller` 在构造时预分析地址而非在 `prepare()` 中动态分析？**
A: 寄存器集合在生产环境几乎恒定不变（硬件点位表固定）。预分析避免每次轮询都重新执行排序/去重/pack，将 CPU 开销从 O(N·poll) 降到 O(N·construct)。

**Q: 为什么 Coil 容忍间隙设为 200，而 HoldingReg 设为 50？**
A: Modbus FC01 单次最多可读 2000 bits（≈250 字节），而 FC03 单次最多读 125 个 16-bit 寄存器（250 字节）。Coil 带宽更充裕，但实际点位较稀疏，设大间隙可减少请求数量而不增加太多传输浪费。HoldingReg 是核心数据通道，间隙设 50 是折中。

**Q: Float32 为什么要"展开"地址？**
A: Float32 占用 2 个连续的 16-bit 寄存器（address 和 address+1）。`PlcPoller` 在构造时主动将 `address+1` 加入地址列表并排序去重，确保 `AddressPacker` 不会因为在区间中间缺少被占用的第二个字而提前结束区间合并。

**Q: 端序决议为什么分两个层级？**
A: 绝大多数 PLC 设备使用统一的全局端序（如三菱的 BigEndian + HighWordFirst），但有极少数特殊寄存器可能例外（如第三方从站使用相反端序）。`RegisterInfo::endianOverride` 允许对单个寄存器精确指定，未指定时自动回落至 `ProtocolProfile::defaultEndian`。

**Q: `IModbusClient` 返回的 Coil 响应是 `uint8_t` 向量，为什么不是 `bool`？**
A: Modbus FC01 响应体是原始字节流，每个字节承载 8 个线圈状态。如果转换为 `vector<bool>` 再在 `assemble()` 中再反转回位运算，将产生两次无意义的数据拷贝。保留 `uint8_t` 可在 `assemble()` 中直接按位拼接。

**Q: PlcDevice 为什么不直接持有 IModbusClient 并自己调用 read？**
A: 遵循单一职责原则（SRP）：
- `PlcPoller::prepare()` / `assemble()`：请求编排 + 响应拼装（纯算法，无需网络）
- `IModbusClient`：网络 IO
- `PlcDevice`：快照持有 + Codec 门面

这种分层允许上层灵活编排网络请求（例如异步并发读取 Coil 和 HoldingReg），且各组件完全可独立单元测试。

---

# 12. 可扩展点

| 扩展点                  | 当前状态                     | 未来可能方向                   |
|-------------------------|-----------------------------|-------------------------------|
| Multi-axis 并行读取     | 串行 `prepare()` + `assemble()` | 同一 `PlcPoller` 实例支持所有轴 |
| 增量轮询 (Delta Polling) | 不支持                       | 仅读取最近变化的寄存器         |
| 异步 assemble           | 同步                         | `assembleAsync()` + callback  |
| 寄存器分组分频          | 不支持                       | 不同 `RegisterGroup` 使用不同 polling 频率 |

---

**文档结束**
