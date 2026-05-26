# ProtocolRuntime 开发规划 —— 从声明到运行 v5

## 文档版本

| 版本 | 日期 | 说明 |
|------|------|------|
| v1.0 | 2026-05-25 | 初版：基于当前实现，制定 L2c~L2e TDD 开发路线图 |
| v2.0 | 2026-05-25 | 修正：类型安全化、RegisterBlock 命名精确化、五维度架构评审 |
| v3.0 | 2026-05-25 | 架构升级：引入物理/逻辑分离。PLC 只有 bit/word，float 是软件解释。确立 Snapshot → Codec → PlcValue 三层管线 |
| v4.0 | 2026-05-25 | 引入 PlcSnapshot：将 RawBitSnapshot 与 RawWordSnapshot 绑定为"同一时刻的完整PLC现场照片"，解决多轮次网络读取导致的数据不一致问题 |
| **v5.0** | **2026-05-26** | **PlcDevice 接口类型安全化：使用 `const RegisterInfo&` 替代 `std::string` 索引，消除运行时字符串查找，编译期保证寄存器引用正确性** |

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
```

**关键洞察**：
- **Raw 快照层**不知道"数据类型"——只知地址和 bit/word 数组。
- **PlcSnapshot** 不知道"数据类型"——但它保证 bit 和 word 来自同一次完整采集周期。
- **Codec 层**不知道"业务含义"——只知元数据（Float32占2字，大端），执行纯数学转换。
- **PlcValue** 是翻译成果的标准载体。

---

## 一、当前状态回顾

| 文件 | 状态 | 分层 |
|------|------|------|
| `RegisterMetadata.h` | ✅ | L2a |
| `EndianPolicy.h` | ✅ | L2a |
| `ProtocolProfile.h` | ✅ | L2a |
| `RegisterAddressAll.h` | ✅ | L2a |
| `RegisterRegistry.h` | ✅ | L2b |
| `MemorySnapshot.h` | ✅ | 物理层 |
| `RegisterCodec.h` | ⚠️ | L2d — 需重构 |

### 当前间隙

需打通：**Raw 快照 → PlcSnapshot → Codec → PlcValue → PlcDevice**。

---

## 二、v5 架构全景图

### 2.1 v4 的问题：string 索引破坏类型安全

v4 中 `PlcDevice` 使用 `readFloat(const std::string& name)` 接口：

```
问题 1：RegisterInfo 没有 name 字段
         └── v4 假设 m_registry.findByName(name) 存在，
             但实际 Registry 只有 findByGroup/Area/Address

问题 2：命名空间冲突
         └── 四个轴各有 ABS_POSITION / MOVE_DONE，
             字符串 "ABS_POSITION" 无法区分是哪个轴

问题 3：运行时错误替代编译期检查
         └── readFloat("ABS_POSITON")   ← 拼写错误，编译通过，运行时崩
             readFloat(y_axis::feedback::ABS_POSITION) ← 编译错误

问题 4：与 constexpr 设计哲学矛盾
         └── 60+ constexpr RegisterInfo 的零开销抽象，
             引入 string 查找是纯运行时开销
```

### 2.2 v5 分层架构

```
                  ┌─────────────────────────┐
                  │  PlcDevice (门面层)       │
                  │  readFloat(reg)          │  ← const RegisterInfo&
                  │  readBool(reg)           │     编译期类型安全
                  │  writeValue(reg, val)    │
                  └──────────┬──────────────┘
                             │
         ┌───────────────────┼───────────────────┐
         ▼                   ▼                    ▼
┌─────────────┐  ┌──────────────────┐  ┌──────────────────────────┐
│ PlcValue    │  │ RegisterCodec    │  │ RegisterRegistry          │
│ variant<    │◄─│ decode/encode    │  │ (为 Poller / Validator    │
│  bool,int16,│  │ Snapshot→业务值   │  │  服务，不参与 Device 读)  │
│  float>     │  └────────┬─────────┘  └──────────────────────────┘
└──────┬──────┘           │
       │          ┌───────┴───────────┐
       │          │    PlcSnapshot    │  ← 一次完整的PLC现场照片
       │          └───────────────────┘
       │                   │
       │          ┌────────┴────────┐
       │          ▼                 ▼
       │  ┌────────────┐  ┌──────────────┐
       │  │RawBitSnap  │  │RawWordSnap   │
       │  └──────┬─────┘  └──────┬───────┘
       │         │               │
       │  ┌──────┴───────────────┴──┐
       │  │  IModbusClient (传输层) │
       │  └─────────────────────────┘
  ┌────┴──────────┐
  │ 业务层         │
  │ float pos =   │
  │   device      │
  │   .readFloat( │
  │   y_axis::    │
  │   feedback::  │
  │   ABS_POSITION│
  │  );           │
  └───────────────┘
```

### 2.3 数据流向五阶段

| 阶段 | 路径 | 关键动作 |
|------|------|---------|
| 1 | PLC→Transport→Raw Snapshots | FC01/FC03拉回payload，零拷贝存储 |
| 2 | RawSnapshots→PlcSnapshot | 绑定 bit/word + 时间戳 + complete 标记 |
| 3 | PlcSnapshot→Codec→PlcValue | 划出span，结合EndianPolicy拼装 |
| 4 | 业务代码→Codec→Transport | encode成Modbus值，FC05/FC10发出 |
| 5 | PlcDevice脉冲管理 | EdgeTrigger: FC05(ON)→定时→FC05(OFF) |

---

## 三、v5 总体路线图

```
P0 ────→ P1 ────→ P2 ────→ P3 ────→ P4
 PlcValue  Validator  Codec重构 PlcSnapshot PlcDevice
 variant   4条规则   Snapshot→  AddressPacker ★ const RegisterInfo&
          不变      Value      PlcPoller       替代 std::string
```

---

## 四、P0：PlcValue — 标准货币

**文件**：`infrastructure/plc/protocol/PlcValue.h`

```cpp
#pragma once
#include <variant>
#include <cstdint>

namespace plc::protocol {

using PlcValue = std::variant<bool, int16_t, float>;

template<typename T>
T getValue(const PlcValue& value) { return std::get<T>(value); }

} // namespace plc::protocol
```

**TDD**：`tests/infrastructure/protocol/test_plc_value.cpp`

---

## 五、P1：ProtocolConstraintValidator

（与 v4 一致，四条规则：R01 地址重叠、R02 Coil 类型限制、R03 Feedback 权限、R04 脉冲宽度缺失）

---

## 六、P2：RegisterCodec 重构

```cpp
static PlcValue decode(const RegisterInfo& reg, const PlcSnapshot& snapshot,
    const ProtocolProfile& profile);
```

---

## 七、P3：PlcSnapshot + AddressPacker + PlcPoller

（与 v4 一致，无变更）

---

## 八、P4：PlcDevice — 终极门面（★ v5 核心变更）

### 8.1 v4 → v5 设计决策：为什么不能用 string 索引

#### 8.1.1 命名空间冲突是致命伤

`RegisterAddressAll.h` 中四个轴各有完全同名的寄存器：

```cpp
namespace plc::reg {
  namespace x_axis::feedback { constexpr RegisterInfo ABS_POSITION = {...}; }
  namespace y_axis::feedback { constexpr RegisterInfo ABS_POSITION = {...}; }
  namespace z_axis::feedback { constexpr RegisterInfo ABS_POSITION = {...}; }
  namespace r_axis::feedback { constexpr RegisterInfo ABS_POSITION = {...}; }
}
```

仅凭 `"ABS_POSITION"` 无法确定是哪个轴。必须用 `"y_axis.feedback.ABS_POSITION"` 字符串模拟命名空间——完全冗余。

#### 8.1.2 基础设施不存在

`RegisterInfo` 没有 `name` 字段。`RegisterRegistry` 没有 `findByName()` 方法。v4 建立在虚构的 API 之上。

#### 8.1.3 类型安全对比

| 场景 | v4: `std::string` | v5: `const RegisterInfo&` |
|------|-------------------|---------------------------|
| 正确调用 | `readFloat("y_axis.ABS_POSITION")` | `readFloat(y_axis::feedback::ABS_POSITION)` |
| 拼写错误 | 运行时 `std::invalid_argument` | **编译错误** |
| 命名冲突 | 需手动拼接命名空间字符串 | C++ 编译器保证唯一性 |
| 重构 | 字符串全局搜索替换 | IDE 自动重命名 |
| 性能 | 运行时 string 构造 + map 查找 | 编译期常量，零开销 |

### 8.2 PlcDevice.h（v5 正确设计）

```cpp
#pragma once
#include "PlcValue.h"
#include "PlcSnapshot.h"
#include "RegisterMetadata.h"
#include "RegisterCodec.h"
#include "ProtocolProfile.h"

namespace plc::protocol {

class PlcDevice {
public:
    explicit PlcDevice(const ProtocolProfile& profile);

    // ═══ 读取（★ const RegisterInfo&，编译期类型安全） ═══
    PlcValue readValue(const RegisterInfo& reg) const;

    bool readBool(const RegisterInfo& reg) const {
        return getValue<bool>(readValue(reg));
    }
    int16_t readInt16(const RegisterInfo& reg) const {
        return getValue<int16_t>(readValue(reg));
    }
    float readFloat(const RegisterInfo& reg) const {
        return getValue<float>(readValue(reg));
    }

    // ═══ 写入 ═══
    void writeValue(const RegisterInfo& reg, const PlcValue& value);
    void writeBool(const RegisterInfo& reg, bool v)   { writeValue(reg, PlcValue{v}); }
    void writeInt16(const RegisterInfo& reg, int16_t v) { writeValue(reg, PlcValue{v}); }
    void writeFloat(const RegisterInfo& reg, float v)   { writeValue(reg, PlcValue{v}); }

    // ═══ Snapshot 管理 ═══
    void updateSnapshot(PlcSnapshot snap);
    const PlcSnapshot& snapshot() const;
    bool isStateTrusted() const;

    // ═══ Transport 绑定 ═══
    void bindTransport(IModbusClient* client);

private:
    const ProtocolProfile& m_profile;
    PlcSnapshot m_snapshot;
    IModbusClient* m_client = nullptr;
};

} // namespace plc::protocol
```

### 8.3 PlcDevice 实现骨架

```cpp
PlcValue PlcDevice::readValue(const RegisterInfo& reg) const {
    // ★ 不需要 Registry.findByName —— 调用方已传入确切的 RegisterInfo
    return RegisterCodec::decode(reg, m_snapshot, m_profile);
}

void PlcDevice::writeValue(const RegisterInfo& reg, const PlcValue& value) {
    if (reg.access == RegisterAccess::ReadOnly)
        throw std::invalid_argument("Cannot write to ReadOnly register");
    if (!m_client)
        throw std::runtime_error("PlcDevice: no transport bound for write");
    auto encoded = RegisterCodec::encode(value, reg, m_profile);
    m_client->writeMultipleRegisters(reg.address, encoded);
}
```

### 8.4 主循环中的使用示例

```cpp
// ── 初始化 ──
RegisterRegistry registry;
registry.addAll({
    plc::reg::y_axis::feedback::ABS_POSITION,
    plc::reg::y_axis::feedback::MOVE_DONE,
});

PlcDevice device(profile);
PlcPoller poller(maxGap, maxBlockSize);
IModbusClient* client = ...;
device.bindTransport(client);

// ── 主循环 ──
void tick() {
    PlcSnapshot snap = poller.poll(*client, registry, RegisterGroup::Feedback);
    device.updateSnapshot(std::move(snap));

    if (device.isStateTrusted()) {
        // ★ 编译期类型安全：写错任何一个字符 = 编译错误
        float absPos  = device.readFloat(y_axis::feedback::ABS_POSITION);
        bool moveDone = device.readBool(y_axis::feedback::MOVE_DONE);
        int16_t state = device.readInt16(y_axis::feedback::STATE);

        // 写入示例
        device.writeFloat(y_axis::command::TARGET_POSITION, 200.0f);
        device.writeBool(y_axis::command::MOVE_START, true);
    }
}
```

### 8.5 表现层字符串映射（解耦方案）

如果 QML 或命令行界面确实需要字符串来指定寄存器，应在**表现层**独立实现：

```cpp
// presentation/RegisterNameResolver.h —— 表现层专用
// 不污染 plc::protocol 核心接口

class RegisterNameResolver {
public:
    using RegRef = const RegisterInfo&;

    static RegRef resolve(const std::string& path) {
        static const std::unordered_map<std::string, RegRef> map = {
            {"y_axis.feedback.ABS_POSITION",  plc::reg::y_axis::feedback::ABS_POSITION},
            {"y_axis.feedback.MOVE_DONE",     plc::reg::y_axis::feedback::MOVE_DONE},
            {"x_axis.feedback.ABS_POSITION",  plc::reg::x_axis::feedback::ABS_POSITION},
            // ... 60+ 映射由脚本自动生成，不影响核心接口类型安全
        };
        auto it = map.find(path);
        if (it == map.end()) throw std::invalid_argument("Unknown register: " + path);
        return it->second;
    }
};

// UI 层使用：
void onQmlQuery(const std::string& regPath) {
    auto& reg = RegisterNameResolver::resolve(regPath);
    PlcValue val = m_device.readValue(reg);  // ★ 仍然走 const RegisterInfo&
}
```

**关键**：字符串查找仅存在于表现层适配器中，核心 `PlcDevice` 接口保持纯 `const RegisterInfo&`。

### 8.6 TDD 测试

**文件**：`tests/infrastructure/protocol/test_plc_device.cpp`

```cpp
TEST(PlcDeviceRead, readFloat_yAxisAbsPosition) {
    PlcSnapshot snap = buildSnapWithFloat(124, 150.5f); // D124=0x4316, D125=0x0000
    PlcDevice device(profile);
    device.updateSnapshot(std::move(snap));

    float pos = device.readFloat(plc::reg::y_axis::feedback::ABS_POSITION);
    EXPECT_FLOAT_EQ(pos, 150.5f);
}

TEST(PlcDeviceRead, readBool_yAxisMoveDone) {
    PlcSnapshot snap = buildSnapWithCoil(101, true);
    PlcDevice device(profile);
    device.updateSnapshot(std::move(snap));

    bool done = device.readBool(plc::reg::y_axis::feedback::MOVE_DONE);
    EXPECT_TRUE(done);
}

TEST(PlcDeviceRead, untrustedSnapshot_returnsLastKnownGood) {
    // 设计决策：isStateTrusted()==false 时，返回上一次可信快照的值
}

TEST(PlcDeviceWrite, writeReadOnly_throws) {
    PlcDevice device(profile);
    EXPECT_THROW(
        device.writeFloat(plc::reg::y_axis::feedback::ABS_POSITION, 100.0f),
        std::invalid_argument
    );
}

TEST(PlcDeviceWrite, writeNoTransport_throws) {
    PlcDevice device(profile); // no bindTransport
    EXPECT_THROW(
        device.writeBool(plc::reg::y_axis::command::MOVE_START, true),
        std::runtime_error
    );
}
```

---

## 九、Runtime 联动与完整数据流

### 9.1 完整时序图

```
Time      PlcPoller              PlcDevice          业务层
────────────────────────────────────────────────────────────
t=0ms     poll(client,registry,  ←── tick() 触发
          Feedback)
          │
          ├─ FC01 → client       (读 Coil)
          ├─ FC03 → client       (读 HoldingReg 块1)
          ├─ FC03 → client       (读 HoldingReg 块2)
          │
t=5ms     PlcSnapshot{bits,      ──► updateSnapshot()
          words, complete=true}
                                   │
t=10ms                             ├─ readFloat(y_axis::
                                   │  feedback::ABS_POSITION)
                                   │  → Codec::decode()
                                   │  → 150.0f
                                   │
t=10ms    poll(...) ──►            │  ──► 150.0f → 控制算法
```

### 9.2 与现有 ISystemDriver 的关系

```
ISystemDriver (现有接口)
    │
    ├─ FakeAxisDriver  ──► FakePLC (仿真)
    │
    └─ ModbusAxisDriver ──► IModbusClient ──► PlcPoller ──► PlcDevice
                                                                 │
                                                      registerReadInt16()
                                                      registerReadFloat()
                                                      registerWriteCoil()
```

`PlcDevice` 是 `ISystemDriver` 之下、传输层之上的新抽象层。现有 `registerRead*()` 调用最终将通过 `PlcDevice` + `PlcSnapshot` 提供服务。

---

## 十、v4 → v5 变更总结

| 维度 | v4 设计 | v5 设计 | 理由 |
|------|---------|---------|------|
| 接口签名 | `readFloat(const std::string& name)` | `readFloat(const RegisterInfo& reg)` | 编译期类型安全 |
| 查询机制 | `m_registry.findByName(name)` (不存在) | 无查询——调用方直接传引用 | 不依赖虚构 API |
| 命名冲突 | 需 `"y_axis.ABS_POSITION"` 字符串 | `y_axis::feedback::ABS_POSITION` | C++ 命名空间天然唯一 |
| 拼写错误 | 运行时 `std::invalid_argument` | 编译错误 | 错误前移 |
| 性能 | 运行时 `std::string` 构造 + map O(log n) | 编译期常量 O(1) | 零开销 |
| UI 层需求 | 污染核心接口 | `RegisterNameResolver` 适配器 | 关注点分离 |
| 对现有代码影响 | 需给 60+ RegisterInfo 加 name 字段 | 零修改 | 寄存器定义无需改动 |

核心设计原则：**C++ 的 constexpr 命名空间体系已经是完美的寄存器索引系统，不需要再用字符串重新发明一遍。**

### 影响范围

- ✅ `RegisterAddressAll.h` — 无需修改
- ✅ `RegisterMetadata.h` — 无需修改（不添加 name 字段）
- ✅ `RegisterRegistry.h` — 无需修改
- ✅ P0~P3 — 与 v4 完全一致
- ⚠️ `PlcDevice.h` — 接口从 `std::string` 改为 `const RegisterInfo&`
- ➕ `presentation/RegisterNameResolver.h` — 新增（表现层字符串适配器）
